#pragma once

/**
 * @brief 协议解析线程入口 (FreeRTOS 任务函数签名)
 *
 * @note 单参数、不得 return —— 本任务常驻 (`for(;;)`), 由 init 任务动态创建。
 */
void protocol_parser_thread_entry(void *arg);
