/**
 * @file main.c
 * @brief 应用入口 —— 只做"平台最小初始化 + 起 init 线程"
 *
 * @details
 * 本文件不碰任何外设细节: 串口设备/中断/缓冲归 platform/bsp/bsp_uart.c,
 * 装配顺序归 init_thread.c。留在这里的只有两件事:
 *   1. 启动 banner（直接走 bsp 裸写，此时日志系统还没初始化）;
 *   2. 用 xTaskCreate 创建 init 任务，然后返回。
 *
 * @note 整个工程只有一个 `main()` —— CubeMX 生成的 `Core/Src/main.c`,
 *       它在 `osKernelStart()` 之前调用 `app_start()`。
 *
 * @par init 任务为什么用动态分配
 * init 任务是**一次性**的: 它把系统装配完就 `vTaskDelete(NULL)` 退出。
 * 但静态分配的栈**不会**随任务删除而回收 —— `vTaskDelete` 对
 * `xTaskCreateStatic` 创建的栈只标记任务无效，不会释放（这正是静态分配
 * 的定义: 内存归调用方）。于是那 8KB 栈（INIT_STACK_SIZE） + TCB 会永久白白
 * 占着 SRAM。
 *
 * 用 `xTaskCreate` 则栈与 TCB 来自 FreeRTOS 堆（heap_4），任务被删除后
 * 由空闲任务调用 `prvDeleteTCB` → `vPortFree` 归还给堆，后续
 * `system_malloc` / StreamBuffer / 信号量都能复用这 4KB。
 * 代价：启动峰值多占 8KB 堆（heap_4 会合并相邻空闲块，碎片风险可控）。
 *
 * `protocol_parser` 任务同样用动态分配（见 init_thread.c），但它是常驻的
 * （`for(;;)` 不退出），那 4KB 会一直占着堆。
 *
 * @note 本文件**不碰串口**: "开通道"是 init_thread 顺序表第 2 步
 *       (`bsp_uart_start`), 那里是唯一调用点。波特率由 CubeMX 在
 *       `MX_USART3_UART_Init()` 里配; 运行时改速走 `bsp_uart_set_baudrate()`。
 */

#include "app_config.h"
#include "init_thread.h"
#include "bsp_uart.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * init 任务用**动态**分配（xTaskCreate）：它在装配完成后 vTaskDelete(NULL)
 * 退出，栈与 TCB 由空闲任务归还给 FreeRTOS 堆，可被后续分配复用。
 * 静态分配的话这 4KB 会永久占用（vTaskDelete 不释放静态栈）—— 详见文件头说明。
 *
 * 注意 APP_STACK_WORDS(): INIT_STACK_SIZE 的单位是**字节**，
 * 而 xTaskCreate 的深度单位是**字**。
 */

void app_start(void)
{
    BaseType_t rc;

    /* 此刻 osKernelInitialize 已调用但调度器未启动，日志系统也还没起来，
       所以 banner 走 bsp 的裸写调试口。 */
    static const char banner[] =
        "20260521\r\n"
        "=== Protocol Chain Test (FreeRTOS / STM32F103) ===\r\n"
        "  Protocols: Imperx, Camyu, Ymodem(Rx), Ymodem(Tx)\r\n"
        "\r\n";
    static const char err_msg[] =
        "[app] FATAL: init task create failed (FreeRTOS heap too small)\r\n";

    bsp_dbg_write((const uint8_t *)banner, sizeof(banner) - 1u);

    rc = xTaskCreate(init_thread_entry, "init",
                     APP_STACK_WORDS(INIT_STACK_SIZE), NULL,
                     (UBaseType_t)THREAD_PRIORITY, NULL);
    if (rc != pdPASS)
    {
        /* 堆不足：继续启动调度器只会得到一个空跑的系统，
           还不如在这里把原因说出来（vApplicationMallocFailedHook 也会兜底）。 */
        bsp_dbg_write((const uint8_t *)err_msg, sizeof(err_msg) - 1u);
    }

    /* 其余工作都在 init 任务里；调度器启动后本函数不再被调用 */
}
