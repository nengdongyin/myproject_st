#pragma once

#include <stdint.h>
#include "protocol_parser.h"
#include "protocol_chain.h"
#include "protocol_parser_imperx.h"
#include "protocol_parser_camyu.h"

/* ================================================================
 *  应用级常量 —— 全部用 #ifndef 守卫, 以便被工程根目录的 config.h 覆盖
 *  (config.h 由构建系统强制预处理: -include config.h。模块内保留默认值,
 *   所以升级时模块目录可整体覆盖, 产品差异只留在 config.h 一处)
 * ================================================================ */

/* 接收管道长度属于 bsp 内部细节 (BSP_UART_PIPE_SIZE, 见 platform/bsp/bsp_uart.c) */

/* @note 通讯口波特率不在这里 —— CubeMX (.ioc 的 USART3 页, 460800) 生成的
 *       MX_USART3_UART_Init() 是唯一来源, 本工程不重复定义这个值。 */

#ifndef STACK_SIZE
#define STACK_SIZE 12288
#endif

#ifndef INIT_STACK_SIZE
#define INIT_STACK_SIZE 8192
#endif

/** @brief 任务优先级
 *
 * @note FreeRTOS 数值越大优先级越高, 取值 0..configMAX_PRIORITIES-1
 *       (本工程 configMAX_PRIORITIES = 56, 见 Core/Inc/FreeRTOSConfig.h)。
 *       7 属于偏低的中等优先级。 */
#ifndef THREAD_PRIORITY
#define THREAD_PRIORITY 7
#endif

/** @brief 把栈字节数换算成 FreeRTOS 的 uxStackDepth (单位: 字)
 *
 * @note 上面的 STACK_SIZE / INIT_STACK_SIZE 单位是**字节**, 而
 *       `xTaskCreate/xTaskCreateStatic` 的 ulStackDepth 是**字**(32-bit,
 *       即 4 字节)。创建任务时用本宏转换, 避免两套单位混用写错 ——
 *       把 12288 当成字就是 48KB, 会直接吃掉本板 64KB SRAM 的一大半。
 */
#define APP_STACK_WORDS(bytes) ((uint32_t)((bytes) / (uint32_t)sizeof(uint32_t)))

#ifndef IMPERX_RX_BUF_SIZE
#define IMPERX_RX_BUF_SIZE 32
#endif

#ifndef IMPERX_TX_BUF_SIZE
#define IMPERX_TX_BUF_SIZE 32
#endif

#ifndef CAMYU_RX_BUF_SIZE
#define CAMYU_RX_BUF_SIZE 320
#endif

#ifndef CAMYU_TX_BUF_SIZE
#define CAMYU_TX_BUF_SIZE 320
#endif

/**
 * @brief 应用上下文
 *
 * @note 只承载 imperx / camyu 与全局收发统计。ymodem 的状态、文件句柄、
 *       收发缓冲区全部归 src/core/file_transfer 组件所有 —— 传输相关配置
 *       走参数系统 (PID_FT_*)。
 */
typedef struct
{
    protocol_chain *chain;

    imperx_protocol_parser_t *imperx_parser;
    camyu_protocol_parser_t *camyu_parser;

    uint8_t imperx_rx_buf[IMPERX_RX_BUF_SIZE];
    uint8_t imperx_tx_buf[IMPERX_TX_BUF_SIZE];
    uint8_t camyu_rx_buf[CAMYU_RX_BUF_SIZE];
    uint8_t camyu_tx_buf[CAMYU_TX_BUF_SIZE];

    uint32_t total_rx_bytes;
} app_ctx_t;

extern app_ctx_t app_ctx;
/* 串口设备/接收管道不暴露给 app: 它们属于 platform/bsp, app 层只经
   bsp_uart.h 的接口取用。 */
