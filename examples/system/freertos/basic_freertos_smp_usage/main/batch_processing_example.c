/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "basic_freertos_smp_usage.h"

#define DATA_BATCH_SIZE 5 //����������Ĵ�С

// static TaskHandle_t proc_data_task_hdl;
static QueueHandle_t msg_queue;
static const uint8_t msg_queue_len = 10; //��Ϣ���е���󳤶�
static SemaphoreHandle_t s_mutex;      // mutex to protect shared resource "s_rcv_item_num" ����������������Դ
static volatile int s_rcv_item_num;     // received data item number �����������
static volatile bool timed_out;        // ����Ƿ�ʱ
const static char *TAG = "batch processing example"; //������ʾ��


//��ʾ���ʹ������֪ͨ��ʵ����������
//������ �� ��Ϣ����
/* This example describes a realistic scenario where there are 2 tasks, one of them receives irregularly arrived external data,
and the other task is responsible for processing the received data items. For some reason, every 5 data items form a batch
and they are meant to be processed together. Once the receiving data obtains a data item, it will increment a global variable
named s_rcv_item_num by 1, then push the data into a queue, of which the maximal size is 10; when s_rcv_item_num is not less
than 5, the receiving thread sends a task notification to the processing thread, which is blocked waiting for this signal to
proceed. Processing thread dequeues the first 5 data items from the queue and process them, and finally decrease the s_rcv_item_num by 5.
Please refer to README.md for more details.
*/
/* �������������һ����ʵ�ĳ�����������������������һ�����ղ����򵽴���ⲿ���ݣ�
��һ������������յ������������ĳЩԭ��ÿ5���������γ�һ��������
����Ӧ��һ����һ���������ݻ��һ���������������һ��ȫ�ֱ���
��s_rcv_item_num����Ϊ1��Ȼ������ѹ����У����е�����СΪ10;
��s_rcv_item_num��С��ʱ
Ȼ�󣬽����߳������̷߳�������֪ͨ�������߳������ȴ����ź�
�����������̴߳Ӷ�����ȡ��ǰ5��������������ǣ����s_rcv_item_num����5��
��ο�README��Md�˽�������顣
*/



/**
* ������ں���������������ݲ�����������
*/
static void rcv_data_task(void *arg)
{
    int random_delay_ms;
    int data;
    TaskHandle_t proc_data_task_hdl = (TaskHandle_t)arg;

    while (!timed_out) {
        //ģ����̲߳�����������ݵ�����ӳ�
        data = rand() % 100; //����ӳ���ģ�����ݵĲ����򵽴�
        random_delay_ms = (rand() % 500 + 200);
        vTaskDelay(random_delay_ms / portTICK_PERIOD_MS);
        //���ӽ�����Ŀ����1
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) { //��ȡ������ portMAX_DELAY���õȴ�
            s_rcv_item_num += 1;
            xSemaphoreGive(s_mutex);//�ͷŻ�����
        }
        //�����յ��������Ŷӡ�������Ϣ���еķ���
        (void)xQueueGenericSend(msg_queue, (void *)&data, portMAX_DELAY, queueSEND_TO_BACK);//�����յ�����ʱ���������� s_rcv_item_num ������������
        ESP_LOGI(TAG, "enqueue data = %d", data);//�Ŷӵ�����

        //��� s_rcv_item_num ���ڵ���������С����������������֪ͨ��
        if (s_rcv_item_num >= DATA_BATCH_SIZE) {
            xTaskNotifyGive(proc_data_task_hdl);//����֪ͨ �Ѿ����㹻�����ݿ��Խ��д�����
        }
    }

    vTaskDelete(NULL);
}

/**
* ������ں�������������յ�������
*/
static void proc_data_task(void *arg)
{
    int rcv_data_buffer[DATA_BATCH_SIZE] ;
    int rcv_item_num;
    int data_idx;
    while (!timed_out) {
        //�����ȴ�����֪ͨ
        while (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            //ÿ�δ������յ�֪ͨʱ�����ý��յ����������
            rcv_item_num = 0;
            for (data_idx = 0; data_idx < DATA_BATCH_SIZE; data_idx++) {
                //������ȡ��Ϣ���У�ֱ����Ϊ�� ��������Ϣ���еĽ���
                if (xQueueReceive(msg_queue, (void *)&rcv_data_buffer[data_idx], 0) == pdTRUE) {
                    ESP_LOGI(TAG, "dequeue data = %d", rcv_data_buffer[data_idx]);//��������
                    rcv_item_num += 1;
                } else {
                    break;
                }
            }

            // ģ�⴦�������е����ݣ�Ȼ��������
            for (data_idx = 0; data_idx < rcv_item_num; data_idx++) {
                rcv_data_buffer[data_idx] = 0;//���s_rcv_item_num��С���������С����������Ϊ0
            }

            //��ȡ������
            if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
                s_rcv_item_num -= rcv_item_num;//�������
                xSemaphoreGive(s_mutex);//�ͷŻ�����
                ESP_LOGI(TAG, "decrease s_rcv_item_num to %d", s_rcv_item_num);//���ٽ����������s_rcv_item_num
            }
        }
    }

    vTaskDelete(NULL);
}

//�����ں���������������������ʾ��
//������ʾ��:��ʾ���ʹ������֪ͨʵ��������
//ʹ�ö���������֮�䴫�����ݣ���ʹ�û����������������ȫ������
int comp_batch_proc_example_entry_func(int argc, char **argv)
{
    timed_out = false;

    s_mutex = xSemaphoreCreateMutex();//����������
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, SEM_CREATE_ERR_STR);
        return 1;
    }
    msg_queue = xQueueGenericCreate(msg_queue_len, sizeof(int), queueQUEUE_TYPE_SET);//������Ϣ����
    if (msg_queue == NULL) {
        ESP_LOGE(TAG, QUEUE_CREATE_ERR_STR);
        return 1;
    }
    TaskHandle_t proc_data_task_hdl;
    xTaskCreatePinnedToCore(proc_data_task, "proc_data_task", 4096, NULL, TASK_PRIO_3, &proc_data_task_hdl, tskNO_AFFINITY);//��������
    xTaskCreatePinnedToCore(rcv_data_task, "rcv_data_task", 4096, proc_data_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);//��������

    //��ʱ����COMP_LOOP_PERIOD�����ֹͣ����
    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
    timed_out = true;
    //�ӳ�������������һ��ѭ��
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    return 0;
}









/**
��ʱ�����ַ�ʽ��
vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
vTaskDelay(1500 / portTICK_PERIOD_MS);
�� FreeRTOS �У�`vTaskDelay()` ������������������ִͣ��һ��ʱ�䡣��������ж���������ʽ��
�������ֳ�������ʽ��ʹ�� `TickType_t` ���͵Ĳ�����ֱ��ʹ�ú���������������ʽ����������������δ���ʱ�䵥λת����
1. **ʹ�� `TickType_t` ���͵Ĳ���**��
   ```c
   vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
   ```
   - `pdMS_TO_TICKS()`������һ���꣬���������ĺ�����ת��Ϊ FreeRTOS ��ʱ�ӽ�������ticks����FreeRTOS ��ʱ�ӽ����ǻ���ϵͳʱ�ӵģ���ϵͳʱ�ӵ�Ƶ������Ӳ�������þ����ġ�
   - `vTaskDelay()`�������������һ�� `TickType_t` ���͵Ĳ�������ʾ����Ӧ����ͣ��ʱ�䳤�ȡ��������ϵͳʱ�ӵ�Ƶ�ʽ�������ת��Ϊʱ�ӽ���������ʹ������ͣ��Ӧ��ʱ�䡣
   ʹ�����ַ�ʽ��������ȷ�� `vTaskDelay()` �ܹ���ȷ����ͬ��ϵͳʱ��Ƶ�ʣ���Ϊ����ͨ��ʱ�ӽ�������������ͣʱ��ġ�
2. **ֱ��ʹ�ú�����**��
   ```c
   vTaskDelay(1500 / portTICK_PERIOD_MS);
   ```
   - `1500`������һ����������ʾ����Ҫ��ͣ������ʱ�䣬��λ�Ǻ��롣
   - `portTICK_PERIOD_MS`������һ���꣬���� `FreeRTOSConfig.h` �ļ��ж��壬��ʾϵͳʱ�ӽ��ĵĳ��ȣ��Ժ���Ϊ��λ�������ֵͨ���Ǹ���Ӳ��ʱ��Ƶ�ʺ� FreeRTOS ��������ȷ���ġ�
   ֱ��ʹ�ú�������Ϊ����ʱ��`vTaskDelay()` �Ὣ���ṩ�ĺ�����ֱ��ת��Ϊʱ�ӽ���������ʹ������ͣ��Ӧ��ʱ�䡣���ַ����Ƚ�ֱ�ӣ�����Ҫע����ǣ�������ϵͳʱ�ӽ��ĵĳ����ǹ̶��ģ�����ܻᵼ���ڲ�ͬ��ϵͳ�����³���ʱ��ƫ�
*��ʵ��Ӧ���У�����ʹ�õ�һ�ַ�������ʹ�� `TickType_t` ���͵Ĳ�������Ϊ���ܹ���׼ȷ�ش���ͬ��ϵͳʱ��Ƶ�ʡ�
���ϵͳʱ��Ƶ�ʷ����仯����������Ҫȷ���ڲ�ͬӲ��ƽ̨�ϴ�����ȶ��ԺͿ���ֲ�ԣ�ʹ��ʱ�ӽ������Ǹ��õ�ѡ��


��������
TaskHandle_t proc_data_task_hdl;
xTaskCreatePinnedToCore(proc_data_task, "proc_data_task", 4096, NULL, TASK_PRIO_3, &proc_data_task_hdl, tskNO_AFFINITY);//��������
xTaskCreatePinnedToCore(rcv_data_task, "rcv_data_task", 4096, proc_data_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);//��������

&proc_data_task_hdl������ָ�� proc_data_task_hdl ������ָ�룬���ڽ����´�������ľ����
tskNO_AFFINITY����������� CPU ���� ID����ʾ���񲻰󶨵��ض����ġ�

proc_data_task_hdl�����Ǵ��ݸ��������Ĳ��������� proc_data_task ����ľ����
ʹ�þɵ�����Լ����Ϊ�˷�ֹ�ƻ��ں˸�֪�ĵ�������



*/



