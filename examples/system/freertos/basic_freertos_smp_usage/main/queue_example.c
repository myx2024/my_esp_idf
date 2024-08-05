/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h" //包含FreeRTOS实时操作系统核心头文件，这个文件包含了FreeRTOS的基础类型定义、宏定义以及API函数原型。
#include "esp_log.h"//包含ESP-IDF日志库头文件，这个文件提供了日志记录功能，允许在程序运行时打印调试信息。
#include "basic_freertos_smp_usage.h"//包含基本FreeRTOS SMP（对称多处理）使用示例的头文件，可能包含了某些宏定义或者额外的函数原型。


static QueueHandle_t msg_queue;//队列句柄
static const uint8_t msg_queue_len = 40;//消息队列的长度（即队列可以存储的最大项数）
static volatile bool timed_out;//静态易变布尔变量timed_out，用于标记是否超时，这个变量将在多个任务间共享，以同步它们的终止
const static char *TAG = "queue example";

//输出接收到的消息
static void print_q_msg(void *arg)
{
    int data;  // 存储从队列中接收到的数据data type should be same as queue item type
    int to_wait_ms = 1000;  // 从队列接收数据时的最大阻塞等待时间（毫秒）the maximal blocking waiting time of millisecond
    const TickType_t xTicksToWait = pdMS_TO_TICKS(to_wait_ms);//将毫秒转换为系统时钟节拍数,用于队列接收函数的阻塞时间

    while (!timed_out) {//timed_out控制任务停止
        if (xQueueReceive(msg_queue, (void *)&data, xTicksToWait) == pdTRUE) {//从队列中接收数据
            ESP_LOGI(TAG, "received data = %d", data);
        } else {//在指定的等待时间内没有接收到数据
            ESP_LOGI(TAG, "Did not received data in the past %d ms", to_wait_ms);
        }
    }

    vTaskDelete(NULL);//删除当前任务，终止它的执行
}

//向队列发送消息
static void send_q_msg(void *arg)
{
    int sent_num  = 0;//存储要发送到队列的数据

    while (!timed_out) {
        //将数据发送到队列，如果队列已满，则打印队列已满的消息 Try to add item to queue, fail immediately if queue is full
        if (xQueueGenericSend(msg_queue, (void *)&sent_num, portMAX_DELAY, queueSEND_TO_BACK) != pdTRUE) {
            ESP_LOGI(TAG, "Queue full\n");
        }
        ESP_LOGI(TAG, "sent data = %d", sent_num);//已发送的数据
        sent_num++;//递增sent_num变量的值，以便下一次发送时使用不同的数据

        // 使当前任务延迟250毫秒 send an item for every 250ms
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

//队列示例:演示如何使用队列在任务之间进行同步
//	接收命令行参数的数量和参数数组	
// Queue example: illustrate how queues can be used to synchronize between tasks
int comp_queue_entry_func(int argc, char **argv)
{
    timed_out = false;//表示程序尚未达到超时状态
	//创建一个队列
	//	长度为msg_queue_len
	//	每个队列项的大小为int类型的大小
	//	队列类型为queueQUEUE_TYPE_SET
	//	创建成功后，队列的句柄存储在msg_queue变量中
    msg_queue = xQueueGenericCreate(msg_queue_len, sizeof(int), queueQUEUE_TYPE_SET);
    if (msg_queue == NULL) {
        ESP_LOGE(TAG, QUEUE_CREATE_ERR_STR);//创建失败
        return 1;
    }
	//创建一个任务来执行print_q_msg函数。任务栈大小为4096字节，任务名为"print_q_msg"，优先级为TASK_PRIO_3，不指定参数，并且没有指定特定的CPU核心（tskNO_AFFINITY）。
    xTaskCreatePinnedToCore(print_q_msg, "print_q_msg", 4096, NULL, TASK_PRIO_3, NULL, tskNO_AFFINITY);
	//创建一个任务来执行send_q_msg函数。任务栈大小为4096字节，任务名为"send_q_msg"，优先级为TASK_PRIO_3，不指定参数，并且没有指定特定的CPU核心（tskNO_AFFINITY）。
	xTaskCreatePinnedToCore(send_q_msg, "send_q_msg", 4096, NULL, TASK_PRIO_3, NULL, tskNO_AFFINITY);

    // 使当前任务延迟COMP_LOOP_PERIOD毫秒 time out and stop running after 5 seconds
    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
	//将timed_out变量设置为true，这会导致之前创建的任务停止执行。
    timed_out = true;
    // 再次延迟500毫秒，以确保所有任务都有足够的时间完成它们的最后循环并安全退出delay to let tasks finish the last loop
    vTaskDelay(500 / portTICK_PERIOD_MS);
    return 0;
}


