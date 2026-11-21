#pragma once

/**
 * @brief 协议解析线程入口 (FreeRTOS 任务函数签名)
 *
 * @note 与 init_thread_entry 同理: Zephyr 三参数签名 → FreeRTOS 单参数。
 */
void protocol_parser_thread_entry(void *arg);
