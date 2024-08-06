/* FreeRTOS Real Time Stats Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
//FreeRTOS实时统计示例
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#define NUM_OF_SPIN_TASKS   6       //旋转任务的数量
#define SPIN_ITER           500000  //实际使用的CPU周期将取决于编译器优化
#define SPIN_TASK_PRIO      2       //任务优先级
#define STATS_TASK_PRIO     3       //任务优先级
#define STATS_TICKS         pdMS_TO_TICKS(1000) //统计任务的时间间隔
#define ARRAY_SIZE_OFFSET   5       //如果print_real_time_stats返回ESP_ERR_INVALID_SIZE，则增加此值 任务状态数组的大小偏移

static char task_names[NUM_OF_SPIN_TASKS][configMAX_TASK_NAME_LEN]; //存储任务名称的数组
static SemaphoreHandle_t sync_spin_task;  //同步旋转任务的信号量
static SemaphoreHandle_t sync_stats_task; //同步统计任务的信号量

/**
 * @brief   函数用于打印给定持续时间内任务的CPU使用情况。
 *
 * 此函数将测量并打印指定时间内任务的CPU使用情况
 * 滴答数(即实时统计)。这是通过简单的调用来实现的
 * uxTaskGetSystemState()两次，间隔一个延迟，然后计算
 * 延迟前后任务运行时间的差异。
 * 
 * @note    如果在延迟期间添加或删除任何任务，则这些任务将不会被打印出来。
 * @note    这个函数应该从高优先级任务中调用，以最小化延迟的不准确性。
 * @note    在双核模式下运行时，每个核将对应50%的运行时间。
 *
 * @param   xTicksToWait    统计测量周期
 *
 * @return
 *  - ESP_OK                成功
 *  - ESP_ERR_NO_MEM        分配的内部数组内存不足
 *  - ESP_ERR_INVALID_SIZE  数组大小不足uxTaskGetSystemState. 在增加ARRAY_SIZE_OFFSET
 *  - ESP_ERR_INVALID_STATE 延迟时间过短
 */
static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;        //存储任务状态
    UBaseType_t start_array_size, end_array_size;               //数组大小
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;   //运行时间计数器
    esp_err_t ret;  //返回值

    //分配数组来存储当前任务状态
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;//获取当前任务数量并加上偏移量
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);  //为任务状态数组分配内存
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;//设置返回状态为内存不足错误
        goto exit;
    }
    //获取当前任务状态
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;//设置返回状态为无效大小错误
        goto exit;
    }

    vTaskDelay(xTicksToWait);//让任务延迟指定的时间

    //分配数组来存储延迟后的任务状态
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;//再次获取任务数量并加上偏移量
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);//为结束时的任务状态数组分配内存
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;//设置返回状态为内存不足错误
        goto exit;
    }
    //获取延迟后的任务状态
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;//设置返回状态为无效大小错误
        goto exit;
    }

    //以运行时统计时钟周期为单位计算total_elapsed_time
    uint32_t total_elapsed_time = (end_run_time - start_run_time);//计算总经过时间
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;//设置返回状态为无效状态错误
        goto exit;
    }

    //打印任务名称、运行时间和占用 CPU 百分比。 
    printf("| Task | Run Time | Percentage\n");//打印表头
    //将start_array中的每个任务与end_array中的任务匹配
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //通过覆盖它们的句柄来标记已匹配的任务
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //检查是否找到匹配的任务
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);
            printf("| %s | %"PRIu32" | %"PRIu32"%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //打印不匹配的任务
    for (int i = 0; i < start_array_size; i++) {// 遍历开始时的任务状态数组
        if (start_array[i].xHandle != NULL) {// 如果任务没有被匹配（即没有在开始时的数组中找到）
            printf("| %s | Deleted\n", start_array[i].pcTaskName); //打印任务已被删除的信息
        }
    }
    for (int i = 0; i < end_array_size; i++) {// 遍历结束时的任务状态数组
        if (end_array[i].xHandle != NULL) {// 如果任务没有被匹配（即没有在开始时的数组中找到）
            printf("| %s | Created\n", end_array[i].pcTaskName);//打印任务已被创建的信息
        }
    }
    ret = ESP_OK;

exit:    //共返回
    free(start_array);
    free(end_array);
    return ret;
}

//旋转任务，用于模拟 CPU 活动
static void spin_task(void *arg)
{
    xSemaphoreTake(sync_spin_task, portMAX_DELAY);//获取同步信号量
    while (1) {
        //消耗CPU周期
        for (int i = 0; i < SPIN_ITER; i++) {
            __asm__ __volatile__("NOP");//消耗一个 CPU 周期
        }
        vTaskDelay(pdMS_TO_TICKS(100));//避免过度占用 CPU 资源，并让其他任务有机会运行
    }
}

//启动旋转任务并定期打印实时统计信息
static void stats_task(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);//获取同步统计任务的信号量

    //启动所有旋转任务
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        xSemaphoreGive(sync_spin_task);//获取同步旋转任务的信号量  
    }

    //定期打印实时统计数据
    while (1) {
        printf("\n\nGetting real time stats over %"PRIu32" ticks\n", STATS_TICKS);//获取实时数据 
        if (print_real_time_stats(STATS_TICKS) == ESP_OK) { //打印给定持续时间内任务的CPU使用情况
            printf("Real time stats obtained\n");//获得的实时统计信息
        } else {
            printf("Error getting real time stats\n");//获取实时统计信息出错
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    //允许其他核心完成初始化
    vTaskDelay(pdMS_TO_TICKS(100));

    //创建同步信号量 旋转 统计
    sync_spin_task = xSemaphoreCreateCounting(NUM_OF_SPIN_TASKS, 0);//创建同步旋转任务的信号量  旋转任务的数量
    sync_stats_task = xSemaphoreCreateBinary();//创建同步统计任务的信号量

    //创建旋转任务
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        snprintf(task_names[i], configMAX_TASK_NAME_LEN, "spin%d", i);
        xTaskCreatePinnedToCore(spin_task, task_names[i], 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    }

    //创建并启动统计任务
    xTaskCreatePinnedToCore(stats_task, "stats", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    xSemaphoreGive(sync_stats_task);//释放同步统计任务的信号量
}

/**
程序的运行结果将是：
    程序开始时，stats_task 任务会等待，直到所有的 spin_task 任务被启动。
    之后，stats_task 任务会定期调用 print_real_time_stats 函数，并打印出每个任务的运行时间和占总运行时间的百分比。
    在每次调用 print_real_time_stats 函数时，它都会打印出一个表头，然后遍历 start_array 和 end_array 中的任务，计算每个任务的运行时间，并打印出结果。
    如果 print_real_time_stats 函数调用成功，它会打印出 “Real time stats obtained”；如果失败，它会打印出 “Error getting real time stats”。
    spin_task 任务会一直运行，消耗 CPU 周期，而 stats_task 任务会定期打印统计数据。
*/
