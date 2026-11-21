/**
 * @file bsp_uart.h
 * @brief 板级串口支持 —— 波特率 / 中断接收+缓冲 / 两路裸写出口
 *
 * @par 分层位置与边界
 * 本文件属于 **platform/bsp**，因此**只允许**依赖 STM32 HAL 与 FreeRTOS，
 * **不得** include 任何 `core/` 或 `app/` 头 —— bsp 是叶子层，一旦认识业务策略
 * 就变成了 platform → core 的反向依赖，换板子要连框架一起拖。
 *
 * 于是这里只提供"两个物理口的裸写能力"：
 * @code
 *   bsp_comm_write()  → 通讯口。协议帧与"当前该由谁用这个口"的判定
 *                       全部在调用方 (app 层)
 *   bsp_dbg_write()   → 调试口。恒定输出, 与谁在用通讯口无关
 * @endcode
 * "调试口恒输出、通讯口只在 shell 模式输出"这类**选路策略**属于应用层
 * (见 app/init/log_init.c 的 log_tx / shell_init.c 的 shell_tx)，不在本层。
 *
 * @par 从 Zephyr 版迁移的接口变化
 * Zephyr 版直接暴露 `struct k_pipe *bsp_uart_pipe()`，由 app 层调
 * `k_pipe_read(pipe, buf, len, K_MSEC(x))`。本版把"读"封装成
 * `bsp_uart_read()`，理由：
 *  - FreeRTOS 侧用的是 StreamBuffer（单写单读、ISR 写入、带超时），它是
 *    `k_pipe` 的天然对应物，但**句柄类型不该泄漏到 app 层**；
 *  - 上层调用点只关心"给我最多 len 个字节，等不到就超时返回 0"，
 *    这正是 StreamBuffer 的语义，也是将来换 DMA 环形缓冲时唯一不变的东西。
 *
 * @par 换板子要改的地方
 * 只有 bsp_uart.c 顶部的 `BSP_COMM_UART` / `BSP_DBG_UART` 两个宏。
 * 上层调用点（init_thread / log_init / shell_init / protocol_parser_thread）一行都不用动。
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动通讯口: 建接收缓冲 → 武装接收中断
 *
 * @return 0 成功; -1 接收缓冲创建失败 (堆不足) 或接收未能武装
 *
 * @par 与 bsp_uart_set_baudrate() 的分工 (两者解耦)
 *   - **start 是生命周期**: 建 StreamBuffer、`HAL_UART_Receive_IT` 武装接收。
 *     只关心"通道开没开"，不关心波特率是多少 —— 它用句柄里当前的值
 *     (即 CubeMX 在 `MX_USART3_UART_Init()` 里配的那个)。
 *   - **set_baudrate 是参数**: 业务需要改波特率时单独调，**不需要**先停掉再
 *     重启通道 —— 内部会自己把接收挂起、改速、再重新武装。
 *
 * 这样拆开的理由: 旧版 `start(baud)` 把两件事绑在一起，导致"只想改个波特率"
 * 也必须走一遍启动流程，语义上说不通（启动是一次性的，改参数是可重复的），
 * 而且启动带了产品参数，bsp 就被迫认识产品配置。
 *
 * @note 幂等：重复调用直接返回 0，不会重复建缓冲、不会重复武装。
 * @note 中断回调只把收到的字节塞进 StreamBuffer，不做解析，也不创建线程 ——
 *       消费者由 app 层决定。
 */
int bsp_uart_start(void);

/**
 * @brief 设置通讯口波特率（可重复调用，可在 start 之前或之后调）
 *
 * @param baud 目标波特率; 0 = 非法
 * @return 0 成功; -1 参数非法或 HAL 重配失败
 *
 * @par 为什么不能简单地“改 Init.BaudRate 再 HAL_UART_Init”
 * `HAL_UART_Init()` 结尾会把 `huart->RxState` 强置为 READY，**但不关 RXNEIE**
 * (见 `stm32f1xx_hal_uart.c` 的 HAL_UART_Init)。若此时接收正武装着，就会得到
 * “中断使能着、HAL 状态机是 READY”的不一致：每个字节都进 ISR 却不再回调
 * `RxCplt`、也不再重新武装 —— 现象是“改完波特率就再也收不到数据”，且不报错。
 * 所以本函数在已启动的情况下会 `HAL_UART_AbortReceive()` → 改速 →
 * `HAL_UART_Init()` → 清 ORE/FE/NE → 重新武装。
 *
 * @note 任务上下文调用：内部不关中断，但会短暂清掉接收中断使能位。
 * @note 幂等：目标值与当前值相同则立即返回，不扰动已武装的接收。
 */
int bsp_uart_set_baudrate(uint32_t baud);

/**
 * @brief 从通讯口接收缓冲取字节 (阻塞至多 timeout_ms)
 *
 * @param buf        目标缓冲
 * @param len        期望最多读取的字节数
 * @param timeout_ms 超时 (毫秒); 0 = 只取已有数据, 不阻塞
 * @return >0 实际读到的字节数; 0 超时; <0 参数错误
 *
 * @note 语义与 Zephyr 版 `k_pipe_read(..., K_MSEC(timeout_ms))` 等价:
 *       返回 0 表示超时且一个字节都没读到，而不是"读完了"。
 */
int bsp_uart_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

/** @brief 通讯口裸写: 不做任何模式判断, 谁调用谁负责该不该写 */
void bsp_comm_write(const uint8_t *data, uint32_t len);

/** @brief 调试口裸写: 恒定输出, 与谁在用通讯口无关 */
void bsp_dbg_write(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
