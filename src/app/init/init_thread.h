#pragma once

/**
 * @brief 装配线程入口 (FreeRTOS 任务函数签名)
 *
 * @note 从 Zephyr 的 `void entry(void*, void*, void*)` 改成 FreeRTOS 的
 *       `void entry(void*)` —— `k_thread_create` 能传三个参数, `xTaskCreate`
 *       只能传一个。原实现三个参数都是 `ARG_UNUSED`, 所以丢掉两无损失。
 */
void init_thread_entry(void *arg);

/**
 * @brief 创建 init 任务（由 Core/Src/main.c 在调度器启动前调用）
 *
 * @note 声明放在本头而不是 app/main.c 里，是因为调用方是 CubeMX 生成的
 *       main.c —— 它只 include 了 "main.h"，因此用 `extern` 声明更省事；
 *       这里提供声明只是为了让实现与声明的类型严格一致（防止一边改了签名
 *       另一边默默不报错）。
 */
void app_start(void);
