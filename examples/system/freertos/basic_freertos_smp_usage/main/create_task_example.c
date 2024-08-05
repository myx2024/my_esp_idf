/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "basic_freertos_smp_usage.h"

                                
#define SPIN_ITER   350000  //任务在循环中消耗的实际 CPU 周期的数量
#define CORE0       0
// only define xCoreID CORE1 as 1 if this is a multiple core processor target, else define it as tskNO_AFFINITY
//如果这是一个多核处理器目标，则仅将xCoreID CORE1定义为1，否则将其定义为tskNO_AFFINITY
#define CORE1       ((CONFIG_FREERTOS_NUMBER_OF_CORES > 1) ? 1 : tskNO_AFFINITY)


static volatile bool timed_out;
const static char *TAG = "create task example";

/**
* 执行一个循环，其中包含 spin_iter_num 次 NOP 指令，用于消耗 CPU 周期
*/
static void spin_iteration(int spin_iter_num)
{
    for (int i = 0; i < spin_iter_num; i++) {
        __asm__ __volatile__("NOP");
    }
}

/**
* 在指定核心上执行一个循环，以消耗 CPU 周期
*/
static void spin_task(void *arg)
{
    int task_id = (int)arg;
    ESP_LOGI(TAG, "created task#%d", task_id);//创建任务
    while (!timed_out) {
        int core_id = esp_cpu_get_core_id();
        ESP_LOGI(TAG, "task#%d is running on core#%d", task_id, core_id);//任务#%d在核心#%d上运行
        // consume some CPU cycles to keep Core#0 a little busy, so task3 has opportunity to be scheduled on Core#1
        //消耗一些CPU周期来保持核心#0有点忙，所以task3有机会被安排在核心#1上
        spin_iteration(SPIN_ITER);//消耗 CPU 周期
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    vTaskDelete(NULL);
}

/**
* 创建任务示例:展示如何在CPU内核上创建已绑定和未绑定的任务
* Creating task example: show how to create pinned and unpinned tasks on CPU cores
*/
int comp_creating_task_entry_func(int argc, char **argv)
{
    timed_out = false;
    // pin 2 tasks on same core and observe in-turn execution,
    // and pin another task on the other core to observe "simultaneous" execution
    //将2个任务放在同一个核心上，依次观察执行情况。
    //并将另一个任务固定在另一个核心上，以观察“同时”执行
    //创建了四个任务，两个固定在 CORE0，两个固定在 CORE1，还有一个非固定任务
    int task_id0 = 0, task_id1 = 1, task_id2 = 2, task_id3 = 3;
    xTaskCreatePinnedToCore(spin_task, "pinned_task0_core0", 4096, (void*)task_id0, TASK_PRIO_3, NULL, CORE0);
    xTaskCreatePinnedToCore(spin_task, "pinned_task1_core0", 4096, (void*)task_id1, TASK_PRIO_3, NULL, CORE0);
    xTaskCreatePinnedToCore(spin_task, "pinned_task2_core1", 4096, (void*)task_id2, TASK_PRIO_3, NULL, CORE1);
    // Create a unpinned task with xCoreID = tskNO_AFFINITY, which can be scheduled on any core, hopefully it can be observed that the scheduler moves the task between the different cores according to the workload    
    //使用xCoreID = tskNO_AFFINITY创建一个unpinned任务，可以在任何核心上调度，希望可以观察到调度器根据工作负载在不同核心之间移动任务
    xTaskCreatePinnedToCore(spin_task, "unpinned_task", 4096, (void*)task_id3, TASK_PRIO_2, NULL, tskNO_AFFINITY);

    vTaskDelay(pdMS_TO_TICKS(COMP_LOOP_PERIOD));//超时，5秒后停止运行
    timed_out = true;
    vTaskDelay(500 / portTICK_PERIOD_MS);//延迟，让任务完成最后一个循环
    return 0;
}

/**
程序运行时，这些任务的行为会有以下现象：
    pinned_task0_core0 和 pinned_task1_core0 将被固定在第一个 CPU 核心上，它们将交替执行，因为它们的优先级相同。
    pinned_task2_core1 将被固定在第二个 CPU 核心上，它将独立于其他任务执行。
    unpinned_task 不会被固定在任何特定的 CPU 核心上，因此它可能会在两个核心之间移动，根据系统的调度策略和当前核心的负载情况。
    由于 spin_task 函数在其循环中消耗 CPU 周期，因此所有这些任务都会占用 CPU 资源。spin_task 函数中的 spin_iteration 循环会执行大量的 NOP 指令，这些指令不会执行任何有用的操作，只是消耗 CPU 周期。
*/

