/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "basic_freertos_smp_usage.h"

static volatile bool timed_out;//标记是否超时
const static char *TAG = "task notify example";

/* 在本例中，有一个线程在开始处理之前等待来自另一个线程的同步信号任务同步；
*  xTaskNotifyGive(向指定任务发送一个通知)和ulTaskNotifyTake(等待一个或多个通知)
* 也可以通过' xSemaphoreTake '来实现，但是FreeRTOS建议使用任务通知
* 作为一种更快、更轻量级的替代品。
*/

/**
* 接收任务通知
*/
static void notification_rcv_func(void *arg)
{
    int pending_notification_task_num;
    while (!timed_out) {
        pending_notification_task_num = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);//阻塞等待通知
        {
            ESP_LOGI(TAG, "%d tasks pending", pending_notification_task_num);//%d待处理任务
            while (pending_notification_task_num > 0) {
                //处理收到的通知
                ESP_LOGI(TAG, "rcv_task is processing this task notification");//RCV任务正在处理此任务通知
                pending_notification_task_num--;
            }
        }
    }

    vTaskDelete(NULL);
}

/**
* 发送任务通知
*/
static void notification_send_func(void *arg)
{
    TaskHandle_t rcv_task_hdl = (TaskHandle_t)arg;
    //每1000毫秒发送一个任务通知
    while (!timed_out) {
        xTaskNotifyGive(rcv_task_hdl);//向接收任务发送通知
        ESP_LOGI(TAG, "send_task sends a notification");//发送任务发送通知
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
* 启动任务通知示例
*/
int comp_task_notification_entry_func(int argc, char **argv)
{
    timed_out = false;
    TaskHandle_t rcv_task_hdl;
    //创建了两个任务：一个用于接收通知，另一个用于发送通知。
    xTaskCreatePinnedToCore(notification_rcv_func, NULL, 8192, NULL, TASK_PRIO_3, &rcv_task_hdl, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(notification_send_func, NULL, 8192, rcv_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);

    //超时，5秒后停止运行
    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
    timed_out = true;
    //延迟，让任务完成最后一个循环
    vTaskDelay(500 / portTICK_PERIOD_MS);
    return 0;
}

/**
接收：
xTaskCreatePinnedToCore(notification_rcv_func, NULL, 8192, NULL, TASK_PRIO_3, &rcv_task_hdl, tskNO_AFFINITY);
    notification_rcv_func：这是任务函数的名称，指向 notification_rcv_func 函数。
    NULL：这是任务的名称，用于调试和日志记录。
    8192：这是任务的栈大小，以字为单位。
    NULL：这是传递给任务函数的参数。
    TASK_PRIO_3：这是任务的优先级。
    &rcv_task_hdl：这是指向 TaskHandle_t 类型的指针，用于接收新创建任务的句柄。——》
    tskNO_AFFINITY：这是任务的 CPU 核心 ID，表示任务不会被绑定到任何特定的核心上，而是可以被调度到任何核心上。
发送：
xTaskCreatePinnedToCore(notification_send_func, NULL, 8192, rcv_task_hdl, TASK_PRIO_3, NULL, tskNO_AFFINITY);
    notification_send_func：这是任务函数的名称，指向 notification_send_func 函数。
    NULL：这是任务的名称，用于调试和日志记录。
    8192：这是任务的栈大小，以字为单位。
    rcv_task_hdl：这是传递给任务函数的参数，它是一个 TaskHandle_t 类型的变量，指向接收任务的句柄。——》
    TASK_PRIO_3：这是任务的优先级。
    NULL：这是指向 TaskHandle_t 类型的指针，用于接收新创建任务的句柄。
    tskNO_AFFINITY：这是任务的 CPU 核心 ID，表示任务不会被绑定到任何特定的核心上，而是可以被调度到任何核心上。
这些任务创建语句创建了两个任务，它们被绑定到任何可用的 CPU 核心上。
接收任务 notification_rcv_func 用于接收来自发送任务 notification_send_func 的通知。
发送任务会定期向接收任务发送通知，而接收任务会阻塞等待通知并处理它们。
*/
