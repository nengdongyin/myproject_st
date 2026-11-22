/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点无需逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。 */
#define LOG_TAG "rx"

#include "protocol_parser_thread.h"
#include "app_config.h"
#include "bsp_uart.h" /* bsp_uart_read(): 接收缓冲由 bsp 提供 */
#include "ymodem_common.h"
#include "file_transfer.h"
#include "console_mode.h"
#include "shell_api.h"
#include "log_api.h"

/** @brief 接收超时 (毫秒)。超时返回 0 意味着"这批没数据", 用来驱动协议超时轮询 */
#ifndef BSP_UART_RX_TIMEOUT_MS
#define BSP_UART_RX_TIMEOUT_MS 100
#endif
uint8_t data_buf[YMODEM_STX_FRAME_LEN_BYTE];
void protocol_parser_thread_entry(void *arg)
{
    (void)arg;

    app_ctx_t *ctx = &app_ctx;
    for (;;)
    {
        int n;

        /* ymodem 接收态: 单帧最多 1029 字节, 一次读满比逐字节读快两个数量级;
           其余状态逐字节读, 便于协议链按首字节匹配。
           bsp_uart_read() 超时返回 0; 接收缓冲的句柄留在 bsp 层, 不到 app。 */
        if (file_transfer_wants_bulk_rx())
        {
            n = bsp_uart_read(data_buf, sizeof(data_buf), BSP_UART_RX_TIMEOUT_MS);
        }
        else
        {
            n = bsp_uart_read(data_buf, 1, BSP_UART_RX_TIMEOUT_MS);
        }

        if (n > 0)
        {
            ctx->total_rx_bytes += (uint32_t)n;

            /* 通信串口是时分复用: 同一批字节只能交给一个消费者。
               shell 模式下 shellHandler 逐字节解析并回显, 再喂协议链就会
               同时踩回显污染 / 0x0A 触发命令行执行 / 0x52,0x57 撞 Imperx 帧头。 */
            if (console_mode_get() == CONSOLE_MODE_SHELL)
            {
                shell_rx_feed(data_buf, (uint32_t)n);
            }
            else
            {
                (void)protocol_chain_feed(ctx->chain, data_buf, (uint32_t)n);
            }
        }
        else
        {
            /* 协议超时重传 / 握手重试 / 会话收尾全部由各 parser 的 poll 钩子
               驱动, file_transfer 组件不参与本循环的任何调度。 */
            protocol_chain_check_timeout_poll(ctx->chain);
        }
    }
}
