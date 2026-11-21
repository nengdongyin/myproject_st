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

/* @note 通讯口波特率不在这里 —— 已回归 CubeMX (.ioc 的 USART3 页, 460800),
 *       由 MX_USART3_UART_Init() 作为唯一来源。原先这里是 TEST_BAUD_RATE 的
 *       第二份定义, 与 config.h 重复, 改一处会静默失效。 */

#ifndef STACK_SIZE
#define STACK_SIZE 12288
#endif

#ifndef INIT_STACK_SIZE
#define INIT_STACK_SIZE 8192
#endif

/** @brief 任务优先级
 *
 * @note 两套 RTOS 的方向相反: Zephyr 数值越小优先级越高(0..15 抢占式),
 *       FreeRTOS 数值越大优先级越高(0..configMAX_PRIORITIES-1 = 0..55)。
 *       这里保留原值 7 —— 两个系统里它都是"偏低的中等优先级", 但如果你在
 *       Zephyr 侧想表达的是高优先级(如 0~2), 这里必须反过来写大数。 */
#ifndef THREAD_PRIORITY
#define THREAD_PRIORITY 7
#endif

/** @brief 把栈字节数换算成 FreeRTOS 的 uxStackDepth (单位: 字)
 *
 * @note Zephyr 的 `K_THREAD_STACK_DEFINE(name, N)` 里 N 是**字节**,
 *       而 `xTaskCreate/xTaskCreateStatic` 的 ulStackDepth 是**字**(32-bit,
 *       即 4 字节)。上面的 STACK_SIZE / INIT_STACK_SIZE 沿用字节语义(与原
 *       工程一致, 不改动数值), 创建任务时用本宏转换, 避免两套单位混用写错 ——
 *       "12288 字"= 48KB 会直接吃掉本板 64KB SRAM 的一大半。
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
 *       走参数系统 (PID_FT_*), 不再进本结构体。
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
/* 串口设备/接收管道 不再暴露给 app: 它们属于 platform/bsp (见 bsp_uart.h 的
   bsp_uart_pipe())。app 层只经 bsp_uart.h 的接口取用。 */
