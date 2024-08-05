/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_console.h"
#include "basic_freertos_smp_usage.h"
#include "sdkconfig.h"

/**
* 运行演示如何创建和运行固定和未固定任务的示例
*/
static void register_creating_task(void)
{
    const esp_console_cmd_t creating_task_cmd = {
        .command = "create_task",
        .help = "Run the example that demonstrates how to create and run pinned and unpinned tasks",
        .hint = NULL,
        .func = &comp_creating_task_entry_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&creating_task_cmd));
}

/**
* 运行演示如何使用队列在任务之间通信的示例
*/
static void register_queue(void)
{
    const esp_console_cmd_t queue_cmd = {
        .command = "queue",
        .help = "Run the example that demonstrates how to use queue to communicate between tasks",
        .hint = NULL,
        .func = &comp_queue_entry_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&queue_cmd));
}

/**
* 运行演示如何使用互斥锁和自旋锁来保护共享资源的示例
*/
static void register_lock(void)
{
    const esp_console_cmd_t lock_cmd = {
        .command = "lock",
        .help = "Run the example that demonstrates how to use mutex and spinlock to protect a shared resource",
        .hint = NULL,
        .func = &comp_lock_entry_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lock_cmd));
}

/**
* 运行演示如何使用任务通知同步任务的示例
*/
static void register_task_notification(void)
{
    const esp_console_cmd_t task_notification_cmd = {
        .command = "task_notification",
        .help = "Run the example that demonstrates how to use task notifications to synchronize tasks",
        .hint = NULL,
        .func = &comp_task_notification_entry_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&task_notification_cmd));
}


/**
* 运行将队列、互斥锁、任务通知组合在一起的示例
*/
static void register_batch_proc_example(void)
{
    const esp_console_cmd_t batch_proc_example_cmd = {
        .command = "batch_processing",
        .help = "Run the example that combines queue, mutex, task notification together",
        .hint = NULL,
        .func = &comp_batch_proc_example_entry_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&batch_proc_example_cmd));
}

/**
* 初始化和配置控制台 REPL 环境，允许用户通过命令行与系统交互
*/
static void config_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.  每行之前打印的提示符。
     * This can be customized, made dynamic, etc.  这可以是定制的、动态的等等。
     */
    repl_config.prompt = PROMPT_STR ">"; //提示符
    repl_config.max_cmdline_length = 1024; //命令行的最大长度
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    esp_console_register_help_command();//注册一个帮助命令，允许用户通过输入 help 命令来获取帮助信息

    // register entry functions for each component 为每个组件注册入口函数
    register_creating_task();
    register_queue();
    register_lock();
    register_task_notification();
    register_batch_proc_example();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    printf("\n"
           "Please type the component you would like to run.\n");//请键入要运行的组件
}

void app_main(void)
{
    config_console();
}
