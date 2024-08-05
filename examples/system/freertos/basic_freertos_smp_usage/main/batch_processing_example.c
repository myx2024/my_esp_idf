/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "basic_freertos_smp_usage.h"

#define DATA_BATCH_SIZE 5 //数据批处理的大小

// static TaskHandle_t proc_data_task_hdl;
static QueueHandle_t msg_queue;
static const uint8_t msg_queue_len = 10; //消息队列的最大长度
static SemaphoreHandle_t s_mutex;      // mutex to protect shared resource "s_rcv_item_num" 互斥锁保护共享资源
static volatile int s_rcv_item_num;     // received data item number 接收数据项号
static volatile bool timed_out;        // 标记是否超时
const static char *TAG = "batch processing example"; //批处理示例


//演示如何使用任务通知来实现批量处理
//互斥锁 和 消息队列
/* This example describes a realistic scenario where there are 2 tasks, one of them receives irregularly arrived external data,
and the other task is responsible for processing the received data items. For some reason, every 5 data items form a batch
and they are meant to be processed together. Once the receiving data obtains a data item, it will increment a global variable
named s_rcv_item_num by 1, then push the data into a queue, of which the maximal size is 10; when s_rcv_item_num is not less
than 5, the receiving thread sends a task notification to the processing thread, which is blocked waiting for this signal to
proceed. Processing thread dequeues the first 5 data items from the queue and process them, and finally decrease the s_rcv_item_num by 5.
Please refer to README.md for more details.
*/
/* 这个例子描述了一个现实的场景，其中有两个任务，其中一个接收不规则到达的外部数据，
另一个任务负责处理接收到的数据项。由于某些原因，每5个数据项形成一个批处理
它们应该一起处理。一旦接收数据获得一个数据项，它将增加一个全局变量
将s_rcv_item_num命名为1，然后将数据压入队列，队列的最大大小为10;
当s_rcv_item_num不小于时
然后，接收线程向处理线程发送任务通知，处理线程阻塞等待此信号
继续。处理线程从队列中取出前5个数据项并处理它们，最后将s_rcv_item_num减少5。
请参考README。Md了解更多详情。
*/



/**
* 任务入口函数，负责接收数据并将其放入队列
*/
static void rcv_data_task(void *arg)
{
    int random_delay_ms;
    int data;
    TaskHandle_t proc_data_task_hdl = (TaskHandle_t)arg;

    while (!timed_out) {
        //模拟此线程不规则接收数据的随机延迟
        data = rand() % 100; //随机延迟来模拟数据的不规则到达
        random_delay_ms = (rand() % 500 + 200);
        vTaskDelay(random_delay_ms / portTICK_PERIOD_MS);
        //增加接收项目数量1
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) { //获取互斥锁 portMAX_DELAY永久等待
            s_rcv_item_num += 1;
            xSemaphoreGive(s_mutex);//释放互斥锁
        }
        //将接收到的数据排队——》消息队列的发送
        (void)xQueueGenericSend(msg_queue, (void *)&data, portMAX_DELAY, queueSEND_TO_BACK);//当接收到数据时，它会增加 s_rcv_item_num 并将其放入队列
        ESP_LOGI(TAG, "enqueue data = %d", data);//排队的数据

        //如果 s_rcv_item_num 大于等于批量大小，它会向处理任务发送通知。
        if (s_rcv_item_num >= DATA_BATCH_SIZE) {
            xTaskNotifyGive(proc_data_task_hdl);//发出通知 已经有足够的数据可以进行处理了
        }
    }

    vTaskDelete(NULL);
}

/**
* 任务入口函数，负责处理接收到的数据
*/
static void proc_data_task(void *arg)
{
    int rcv_data_buffer[DATA_BATCH_SIZE] ;
    int rcv_item_num;
    int data_idx;
    while (!timed_out) {
        //阻塞等待任务通知
        while (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            //每次此任务收到通知时，重置接收到的数据项号
            rcv_item_num = 0;
            for (data_idx = 0; data_idx < DATA_BATCH_SIZE; data_idx++) {
                //继续读取消息队列，直到它为空 ——》消息队列的接收
                if (xQueueReceive(msg_queue, (void *)&rcv_data_buffer[data_idx], 0) == pdTRUE) {
                    ESP_LOGI(TAG, "dequeue data = %d", rcv_data_buffer[data_idx]);//出列数据
                    rcv_item_num += 1;
                } else {
                    break;
                }
            }

            // 模拟处理缓冲区中的数据，然后清理它
            for (data_idx = 0; data_idx < rcv_item_num; data_idx++) {
                rcv_data_buffer[data_idx] = 0;//如果s_rcv_item_num不小于批处理大小，则将其设置为0
            }

            //获取互斥锁
            if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
                s_rcv_item_num -= rcv_item_num;//处理完成
                xSemaphoreGive(s_mutex);//释放互斥锁
                ESP_LOGI(TAG, "decrease s_rcv_item_num to %d", s_rcv_item_num);//减少接收数据项号s_rcv_item_num
            }
        }
    }

    vTaskDelete(NULL);
}

//组件入口函数，用于启动批量处理示例
//批处理示例:演示如何使用任务通知实现批处理
//使用队列在任务之间传输数据，并使用互斥锁来保护共享的全局数字
int comp_batch_proc_example_entry_func(int argc, char **argv)
{
    timed_out = false;

    s_mutex = xSemaphoreCreateMutex();//创建互斥锁
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, SEM_CREATE_ERR_STR);
        return 1;
    }
    msg_queue = xQueueGenericCreate(msg_queue_len, sizeof(int), queueQUEUE_TYPE_SET);//创建消息队列
    if (msg_queue == NULL) {
        ESP_LOGE(TAG, QUEUE_CREATE_ERR_STR);
        return 1;
    }
    TaskHandle_t proc_data_task_hdl;
    xTaskCreatePinnedToCore(proc_data_task, "proc_data_task", 4096, NULL, TASK_PRIO_3, &proc_data_task_hdl, tskNO_AFFINITY);//接收数据
    xTaskCreatePinnedToCore(rcv_data_task, "rcv_data_task", 4096, proc_data_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);//处理数据

    //超时并在COMP_LOOP_PERIOD毫秒后停止运行
    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
    timed_out = true;
    //延迟让任务完成最后一个循环
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    return 0;
}









/**
延时的两种方式：
vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
vTaskDelay(1500 / portTICK_PERIOD_MS);
在 FreeRTOS 中，`vTaskDelay()` 函数用于在任务中暂停执行一段时间。这个函数有多种重载形式，
其中两种常见的形式是使用 `TickType_t` 类型的参数和直接使用毫秒数。这两种形式的区别在于它们如何处理时间单位转换。
1. **使用 `TickType_t` 类型的参数**：
   ```c
   vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
   ```
   - `pdMS_TO_TICKS()`：这是一个宏，它将给定的毫秒数转换为 FreeRTOS 的时钟节拍数（ticks）。FreeRTOS 的时钟节拍是基于系统时钟的，而系统时钟的频率是由硬件和配置决定的。
   - `vTaskDelay()`：这个函数接受一个 `TickType_t` 类型的参数，表示任务应该暂停的时间长度。它会根据系统时钟的频率将毫秒数转换为时钟节拍数，并使任务暂停相应的时间。
   使用这种方式，您可以确保 `vTaskDelay()` 能够正确处理不同的系统时钟频率，因为它是通过时钟节拍数来计算暂停时间的。
2. **直接使用毫秒数**：
   ```c
   vTaskDelay(1500 / portTICK_PERIOD_MS);
   ```
   - `1500`：这是一个整数，表示您想要暂停的任务时间，单位是毫秒。
   - `portTICK_PERIOD_MS`：这是一个宏，它在 `FreeRTOSConfig.h` 文件中定义，表示系统时钟节拍的长度（以毫秒为单位）。这个值通常是根据硬件时钟频率和 FreeRTOS 的配置来确定的。
   直接使用毫秒数作为参数时，`vTaskDelay()` 会将您提供的毫秒数直接转换为时钟节拍数，并使任务暂停相应的时间。这种方法比较直接，但需要注意的是，它假设系统时钟节拍的长度是固定的，这可能会导致在不同的系统配置下出现时间偏差。
*在实际应用中，建议使用第一种方法，即使用 `TickType_t` 类型的参数，因为它能够更准确地处理不同的系统时钟频率。
如果系统时钟频率发生变化，或者您想要确保在不同硬件平台上代码的稳定性和可移植性，使用时钟节拍数是更好的选择。


创建任务
TaskHandle_t proc_data_task_hdl;
xTaskCreatePinnedToCore(proc_data_task, "proc_data_task", 4096, NULL, TASK_PRIO_3, &proc_data_task_hdl, tskNO_AFFINITY);//接收数据
xTaskCreatePinnedToCore(rcv_data_task, "rcv_data_task", 4096, proc_data_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);//处理数据

&proc_data_task_hdl：这是指向 proc_data_task_hdl 变量的指针，用于接收新创建任务的句柄。
tskNO_AFFINITY：这是任务的 CPU 核心 ID，表示任务不绑定到特定核心。

proc_data_task_hdl：这是传递给任务函数的参数，它是 proc_data_task 任务的句柄。
使用旧的命名约定是为了防止破坏内核感知的调试器。



*/



