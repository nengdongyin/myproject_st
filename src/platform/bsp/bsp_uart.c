/**
 * @file bsp_uart.c
 * @brief 板级串口实现 —— 波特率 / 中断接收 + StreamBuffer / 两路裸写出口
 *
 * @details
 * 本文件是 STM32F103 上"物理串口"的唯一落点 (见 bsp_uart.h 的分层说明)。
 * 刻意**不**包含任何 `core/` 或 `app/` 头: 这里只有 HAL 与 FreeRTOS。
 *
 * @par 两个出口的区别只在"目的地"
 *   - bsp_comm_write: 通讯口 (USART3)。协议帧专用, 不做模式判断 —— 若这里也按
 *     模式静默, 协议在 PROTO 模式下就发不出去了;
 *   - bsp_dbg_write : 调试口 (USART1)。恒定输出。
 *     **加/换专用调试串口只改顶部 BSP_DBG_UART 一行**。
 *
 * @par 接收通路为什么用 StreamBuffer
 * Zephyr 版是 `K_PIPE_DEFINE` + `k_pipe_write(K_NO_WAIT)`（满则丢字节，由协议层
 * 重传兜底）。FreeRTOS 侧最贴近的对应物是 StreamBuffer：单写(ISR)单读(任务)、
 * 带超时、满时同样丢弃新数据并返回。因此上层行为完全一致。
 *
 * @par 中断频率的取舍（重要）
 * 当前实现是 **HAL 单字节中断**（每字节一次中断）。460800 baud 满负荷约
 * 46 k IRQ/s，在 72 MHz 的 M3 上约占十几个百分点 CPU，对启动阶段的联调够用，
 * 且不依赖 DMA 通道分配、不受 CubeMX 重新生成影响。
 * 若后续要压这个开销，替换点只有本文件的 `HAL_UART_RxCpltCallback`：
 * 改成 DMA 环形 + IDLE 空闲中断（`HAL_UARTEx_ReceiveToIdle_DMA`，
 * 本工程的 HAL 已提供该 API），`bsp_uart_read()` 的语义不需要动。
 *
 * @par 换板子要改的地方
 * 只有顶部 `BSP_COMM_UART` / `BSP_DBG_UART` 两个宏。
 */

#include "bsp_uart.h"

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"

/* ================================================================
 *  板级绑定（换板子只改这两行）
 *
 *  USART3: PB10 = TX / PB11 = RX   —— 通讯口（协议 / shell，460800）
 *  USART1: PA9  = TX / PA10 = RX   —— 调试口（日志恒输出）
 *  两个口都在 CubeMX 里配好，句柄由 main.c 的 MX_USARTx_UART_Init 创建。
 *
 *  @par 分工（谁负责哪一半，避免两处配置打架）
 *   - **CubeMX 管硬件侧**：时钟 / 引脚复用 / `HAL_UART_Init` / NVIC 使能与
 *     优先级（`.ioc` 的 NVIC 页勾了 USART3 global interrupt，Preemption = 5）
 *     / `USART3_IRQHandler`（生成在 `stm32f1xx_it.c`）/ `USART3_MspInit` 里的
 *     `HAL_NVIC_SetPriority+EnableIRQ`。
 *   - **本层只做业务包装**：建 StreamBuffer（ISR→任务的字节通道）、
 *     `HAL_UART_Receive_IT` 武装接收、在 `HAL_UART_RxCpltCallback` 里把字节塞进
 *     StreamBuffer、在 `HAL_UART_ErrorCallback` 里清 ORE 并重新武装。
 *
 *  @note 本层**不再**自己定义 ISR、也不再调 `HAL_NVIC_*` —— 那是 CubeMX 的活。
 *        曾经因为 `.ioc` 没勾 NVIC，这两件事只好由本层代做；现在勾上了，
 *        代做就变成 `multiple definition of 'USART3_IRQHandler'`。
 *  @note NVIC 优先级必须 ≥ 5（= `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`），
 *        否则 ISR 里调 `xStreamBufferSendFromISR` / `portYIELD_FROM_ISR` 会破坏
 *        FreeRTOS 的临界区语义。改 `.ioc` 时别把它调小。
 * ================================================================ */

#ifndef BSP_COMM_UART
#define BSP_COMM_UART huart3
#endif

#ifndef BSP_DBG_UART
#define BSP_DBG_UART huart1
#endif

/* 句柄定义在 Core/Src/main.c（CubeMX 生成），此处只做外部声明，
   避免为了一个 extern 去改 CubeMX 管理的 main.h */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

/** @brief 接收缓冲长度 (字节)。满时丢新数据, 由协议层重传兜底 */
#ifndef BSP_UART_PIPE_SIZE
#define BSP_UART_PIPE_SIZE 2048
#endif

/** @brief 调试口输出开关 (0 = 丢弃调试输出, 省掉 USART1 的发送时间) */
#ifndef BSP_DBG_TX_ENABLE
#define BSP_DBG_TX_ENABLE 1
#endif

/* ================================================================
 *  接收通道
 * ================================================================ */

static StreamBufferHandle_t s_rx_sb;   /**< ISR → 任务 的单向字节通道 */
static uint8_t s_rx_byte;              /**< HAL 单字节接收缓冲 (ISR 写) */

/**
 * @brief HAL 单字节接收完成回调
 *
 * @note 管道满时 `xStreamBufferSendFromISR` 直接丢弃返回 0，不阻塞 ISR ——
 *       与 Zephyr 版 `k_pipe_write(..., K_NO_WAIT)` 的语义一致。
 * @note 必须重新 arm 接收，否则 HAL 只收一个字节就停。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;

    if (huart->Instance != BSP_COMM_UART.Instance || s_rx_sb == NULL)
    {
        return;
    }

    (void)xStreamBufferSendFromISR(s_rx_sb, &s_rx_byte, 1, &woken);
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
    portYIELD_FROM_ISR(woken);
}

/**
 * @brief 串口错误回调 —— 溢出 (ORE) 时必须清标志并重新 arm
 *
 * @note 460800 下若 ISR 被更高优先级中断长时间挡住就会 ORE。不重新 arm 的话
 *       接收会永久停摆，表现成"串口突然没数据了"。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != BSP_COMM_UART.Instance)
    {
        return;
    }

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);

    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
}

/* ================================================================
 *  启动
 * ================================================================ */

/* ================================================================
 *  启动 / 波特率
 *
 *  生命周期与参数刻意分开:
 *    bsp_uart_start()          只负责开通道 (建缓冲 + 武装接收)
 *    bsp_uart_set_baudrate()   只负责改参数, 自己处理"接收正在武装"的场景
 *  旧版把两者写成 start(baud), 于是改波特率必须先停掉通道, 语义上说不通。
 * ================================================================ */

/** @brief 通道是否已启动（决定 set_baudrate 要不要维护接收状态） */
static uint8_t s_started;

int bsp_uart_set_baudrate(uint32_t baud)
{
    UART_HandleTypeDef *h = &BSP_COMM_UART;

    if (baud == 0u)
    {
        return -1;
    }

    /* 幂等: 值没变就什么都不做 —— 既避开无谓的重配, 也保住已武装的接收
       (重配一次会让接收短暂停一下, 没必要) */
    if (h->Init.BaudRate == baud)
    {
        return 0;
    }

    /* ---- 未启动: 没有接收状态要维护, 直接改参数重配 ---- */
    if (!s_started)
    {
        h->Init.BaudRate = baud;
        return (HAL_UART_Init(h) == HAL_OK) ? 0 : -1;
    }

    /* ---- 已启动: 必须先把接收停下来 ----
       HAL_UART_Init() 结尾会把 RxState 强置为 READY, 但不关 RXNEIE:
       若接收还武装着就重配, 会留下"中断使能着、状态机是 READY"的不一致,
       结果是每个字节照旧进 ISR 却既不再回调 RxCplt 也不再重新武装 ——
       表现得像"改完波特率就彻底收不到数据", 且不报任何错。
       所以这里显式 AbortReceive, 它会把 RXNEIE/PEIE/EIE 一并清掉。 */
    if (HAL_UART_AbortReceive(h) != HAL_OK)
    {
        return -1;
    }

    h->Init.BaudRate = baud;
    if (HAL_UART_Init(h) != HAL_OK)
    {
        return -1;
    }

    /* 换速前线上可能有半截字节或 ORE 挂着, 清掉再武装,
       否则一开中断就先撞一次错误回调 */
    __HAL_UART_CLEAR_OREFLAG(h);
    __HAL_UART_CLEAR_FEFLAG(h);
    __HAL_UART_CLEAR_NEFLAG(h);

    if (HAL_UART_Receive_IT(h, &s_rx_byte, 1) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

int bsp_uart_start(void)
{
    UART_HandleTypeDef *h = &BSP_COMM_UART;

    if (s_started)
    {
        return 0;
    }

    /* 1) 接收缓冲：只需建一次 (重复调用 start 不应泄漏) */
    if (s_rx_sb == NULL)
    {
        s_rx_sb = xStreamBufferCreate(BSP_UART_PIPE_SIZE, 1);
        if (s_rx_sb == NULL)
        {
            return -1;
        }
    }

    /* 2) 武装接收

       @note 波特率不在这里设 —— 用句柄里当前的值 (CubeMX 在
       MX_USART3_UART_Init 里配的)。要改就走 bsp_uart_set_baudrate()。

       @note NVIC 的使能与优先级由 **CubeMX 管**（`.ioc` → NVIC → USART3 global
       interrupt，Preemption Priority = 5），CubeMX 会在 `USART3_MspInit()` 里生成
       `HAL_NVIC_SetPriority/EnableIRQ`、在 `stm32f1xx_it.c` 里生成
       `USART3_IRQHandler`。本层**不**碰 NVIC，避免两处配置打架。
       优先级必须 ≥ 5（= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY），
       否则在该 ISR 里调 xStreamBufferSendFromISR / portYIELD_FROM_ISR 会破坏
       FreeRTOS 的临界区语义（高于此优先级的 ISR 不允许调内核 API）。 */
    if (HAL_UART_Receive_IT(h, &s_rx_byte, 1) != HAL_OK)
    {
        return -1;
    }

    s_started = 1;
    return 0;
}

/* ================================================================
 *  读取 (等价于 Zephyr 版 k_pipe_read)
 * ================================================================ */

int bsp_uart_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    size_t n;

    if (buf == NULL || len == 0 || s_rx_sb == NULL)
    {
        return -1;
    }

    /* timeout_ms == 0 → 不阻塞只取已有数据; pdMS_TO_TICKS(0) 即为 0 tick,
       StreamBuffer 在 0 tick 下走"能拿多少拿多少"的立即返回路径 */
    n = xStreamBufferReceive(s_rx_sb, buf, (size_t)len, pdMS_TO_TICKS(timeout_ms));
    return (int)n;
}

/* ================================================================
 *  两路裸写出口
 * ================================================================ */

/**
 * @brief 按波特率估算一次阻塞发送的超时 (10 位/字节 + 100ms 余量)
 *
 * @note 用 HAL 的阻塞发送而不是 DMA：本层写入发生在任务上下文，且发送量远小于
 *       接收量（接收才是 460800 的瓶颈）。用计算出的超时而不是 HAL_MAX_DELAY，
 *       是为了避免串口异常时把任务永久卡死在发送里。
 */
static uint32_t tx_timeout_ms(UART_HandleTypeDef *h, uint32_t len)
{
    uint32_t baud = h->Init.BaudRate;
    uint32_t us_per_byte;

    if (baud == 0)
    {
        baud = 9600; /* 未知时按最慢口估算，宁可等久一点也不误判超时 */
    }
    us_per_byte = 10000000u / baud; /* 10 bit / byte */
    return (uint32_t)(((uint64_t)len * us_per_byte) / 1000u) + 100u;
}

/**
 * @brief 分块阻塞发送
 *
 * @note HAL 的 Size 参数是 uint16_t，直接强转会截断超过 65535 的写入，
 *       因此这里按 64KB 分块循环。
 */
static void uart_write_blocking(UART_HandleTypeDef *h, const uint8_t *data, uint32_t len)
{
    while (len > 0u)
    {
        uint16_t chunk = (len > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)len;
        (void)HAL_UART_Transmit(h, (uint8_t *)data, chunk, tx_timeout_ms(h, chunk));
        data += chunk;
        len -= chunk;
    }
}

void bsp_comm_write(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0u)
    {
        return;
    }

    uart_write_blocking(&BSP_COMM_UART, data, len);
}

void bsp_dbg_write(const uint8_t *data, uint32_t len)
{
#if BSP_DBG_TX_ENABLE
    if (data == NULL || len == 0u)
    {
        return;
    }

    uart_write_blocking(&BSP_DBG_UART, data, len);
#else
    (void)data;
    (void)len;
#endif
}
