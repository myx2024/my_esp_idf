/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "basic_freertos_smp_usage.h"

#define SHARE_RES_THREAD_NUM     2      //用于访问共享资源的线程数量
#define ITERATION_NUMBER         100000 //线程在循环中执行的迭代次数


//声明一个静态全局整数作为一个受保护的共享资源，可以被多个任务访问
static volatile int s_global_num = 0;
static atomic_int s_atomic_global_num;
static SemaphoreHandle_t s_mutex;                               //创建互斥锁
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;  //创建自旋锁
static volatile bool timed_out;                                 //标记是否超时
const static char *TAG = "lock example";

/** 互斥锁
* 演示如何使用互斥锁来保护共享资源
*   使用互斥锁来保护共享资源。如果互斥锁已经被占用，这个任务将被阻塞，直到它可用为止;
*   当互斥锁可用时，FreeRTOS会重新调度这个任务，这个任务可以进一步访问共享资源
*/
static void inc_num_mutex_iter(void *arg)
{
    int core_id = esp_cpu_get_core_id();//获取当前任务运行的CPU核心ID
    int64_t start_time, end_time, duration = 0;//存储任务开始、结束、总时间
    start_time = esp_timer_get_time();//获取系统时间单位是微秒（us）
    while (s_global_num < ITERATION_NUMBER) {
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) { //获取互斥锁
            s_global_num++;
            xSemaphoreGive(s_mutex);//释放互斥锁
        }
    }
    end_time = esp_timer_get_time();//获取任务结束执行的时间
    duration = end_time - start_time;//计算任务执行的总时间
    ESP_LOGI(TAG, "mutex task took %lld us on core%d", duration, core_id);//互斥任务执行总时间us  在核心

    vTaskDelete(NULL);//删除当前任务
}

/** 自旋锁
* 演示如何使用自旋锁来保护共享资源
*   进入临界区并使用自旋锁来保护共享资源。如果自旋锁已被占用，则该任务将处于繁忙状态——在此等待，直到它可用为止。
*   与互斥锁相反，当处于临界区时，中断被禁用，这意味着没有任何事情会中断任务，并且freertos调度器永远不会运行和重新调度任务。
*/
static void inc_num_spinlock_iter(void *arg)
{
    int core_id = esp_cpu_get_core_id();//获取当前任务运行的 CPU 核心 ID
    int64_t start_time, end_time, duration = 0;//存储任务开始、结束、总时间
    start_time = esp_timer_get_time();//获取系统时间单位是微秒（us）
    while (s_global_num < ITERATION_NUMBER) {
        portENTER_CRITICAL(&s_spinlock);//进入临界区
        s_global_num++;
        portEXIT_CRITICAL(&s_spinlock);//退出临界区
    }
    end_time = esp_timer_get_time();//获取任务结束执行的时间
    duration = end_time - start_time;//计算任务执行的总时间
    ESP_LOGI(TAG, "spinlock task took %lld us on core%d", duration, core_id);//自旋锁任务已完成总时间us  在核心

    vTaskDelete(NULL);
}

/**
* 使用原子操作来保护共享资源
*/
static void inc_num_atomic_iter(void *arg)
{
    int core_id = esp_cpu_get_core_id();//获取当前任务运行的 CPU 核心 ID
    int64_t start_time, end_time, duration = 0;//存储任务开始、结束、总时间
    start_time = esp_timer_get_time();//获取系统时间单位是微秒（us）
    /*
    * &s_atomic_global_num：这是 s_atomic_global_num 内存位置的地址。
    * 1：这是要加到 s_atomic_global_num 上的增量。
    * 执行 atomic_fetch_add 函数后，s_atomic_global_num 的值将增加 1，并且返回的是增加前的值。
    */
    while (atomic_load(&s_atomic_global_num) < ITERATION_NUMBER) { //原子操作 从内存位置加载当前值
        atomic_fetch_add(&s_atomic_global_num, 1);//将指定内存位置的值增加指定的增量，并返回该位置的原子前值
    }
    end_time = esp_timer_get_time();//获取任务结束执行的时间
    duration = end_time - start_time;//计算任务执行的总时间
    ESP_LOGI(TAG, "atomic task took %lld us on core%d", duration, core_id);//执行原子任务总时间us

    vTaskDelete(NULL);
}

/**
* 使用互斥锁来保护共享资源，并实现任务之间的顺序执行
*/
static void inc_num_mutex(void *arg)
{
    int task_index = *(int*)arg;//指针解引用操作 将 arg 指针指向的 int 类型的值解引用并存储在task_index变量中
    ESP_LOGI(TAG, "mutex task %d created", task_index);//已创建互斥任务%d

    while (!timed_out) {
        xSemaphoreTake(s_mutex, portMAX_DELAY); // == pdTRUE  获取互斥锁 无限期地等待互斥锁

        int core_id = esp_cpu_get_core_id();//获取当前任务运行的 CPU 核心 ID
        ESP_LOGI(TAG, "task%d read value = %d on core #%d", task_index, s_global_num, core_id);//任务%d读取值=核心#%d上的%d
        s_global_num++;
        // delay for 500 ms
        vTaskDelay(pdMS_TO_TICKS(500));
        xSemaphoreGive(s_mutex);//释放互斥锁
        ESP_LOGI(TAG, "task%d set value = %d", task_index, s_global_num);//任务%d设置值= %d
    }

    vTaskDelete(NULL);//删除当前任务
}


/** 锁的例子:展示如何使用互斥锁和自旋锁来保护共享资源
首先，共享资源' s_global_num '由互斥锁保护，有2个任务，其任务函数为' inc_num_mutex_iter '，依次访问并增加该数字。
当number值达到100000时，从开始运行到运行的持续时间测量和记录当前的时间，然后这两个任务都将被删除。

接下来，重置' s_global_num '，还有2个任务，调用任务函数' inc_num_spinlock_iter '，访问并增加此共享资源，直到达到100000，在自旋锁的保护下。
预期的结果是这两个任务将具有与前两个任务相比，时间开销更少，因为它们涉及的上下文更少切换任务执行。

之后，另外2个任务被创建来完成相同的任务添加作业，但共享资源是原子类型整数。
它应该有一个更短的运行时间比自旋锁任务长，因为原子操作是一种无外观的实现节省了进出临界区的时间。

注意:如果这个示例在单核上运行，则每种类型只创建一个任务。

最后，它说明了共享资源' s_global_num '是由互斥锁保护的然后被多个任务访问。
*/
int comp_lock_entry_func(int argc, char **argv)
{
    s_global_num = 0;
    int thread_id; //存储线程 ID
    int core_id;   //CPU 核心 ID

    timed_out = false;

    //创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, SEM_CREATE_ERR_STR);
        return 1;
    }

    //创建2个任务访问受互斥锁保护的共享资源 ——》互斥锁
    for (core_id = 0; core_id < CONFIG_FREERTOS_NUMBER_OF_CORES; core_id++) {
        xTaskCreatePinnedToCore(inc_num_mutex_iter, NULL, 4096, NULL, TASK_PRIO_3, NULL, core_id);
    }

    //重置s_global_num
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    s_global_num = 0;
    //创建2个访问共享资源的任务 ——》自旋锁
    for (core_id = 0; core_id < CONFIG_FREERTOS_NUMBER_OF_CORES; core_id++) {
        xTaskCreatePinnedToCore(inc_num_spinlock_iter, NULL, 4096, NULL, TASK_PRIO_3, NULL, core_id);
    }

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    //创建2个访问原子共享资源的任务
    atomic_init(&s_atomic_global_num, 0); //它们将访问一个由原子操作保护的共享资源
    for (core_id = 0; core_id < CONFIG_FREERTOS_NUMBER_OF_CORES; core_id++) {
        xTaskCreatePinnedToCore(inc_num_atomic_iter, NULL, 4096, NULL, TASK_PRIO_3, NULL, core_id);
    }

    //重置s_global_num
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    s_global_num = 0;
    //创建2个任务来增加共享数 ——》互斥锁
    // 任务不会被绑定到任何特定的核心上，而是可以被调度到任何核心上.这些任务将顺序增加一个共享数字。
    // 由于任务没有绑定到特定的 CPU 核心上，它们可以被调度到任何核心上执行，从而演示了任务如何在不同核心之间移动。
    for (thread_id = 0; thread_id < SHARE_RES_THREAD_NUM; thread_id++) {
        xTaskCreatePinnedToCore(inc_num_mutex, NULL, 4096, &thread_id, TASK_PRIO_3, NULL, tskNO_AFFINITY);
    }

    //超时，5秒后停止运行
    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));
    timed_out = true;
    //延迟，让任务完成最后一个循环
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    return 0;
}


/** 结果：
* 互斥锁、自旋锁和原子操作任务将分别增加全局变量 s_global_num 的值。
* 顺序增加共享数字的任务将交替执行，每次增加 1。
* 在超时之前，所有任务应该完成其循环，s_global_num 的最终值将是所有任务增加的总和。
*/

