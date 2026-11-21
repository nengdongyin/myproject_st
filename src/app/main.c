/**
 * @file main.c
 * @brief 应用入口 —— 只做"平台最小初始化 + 起 init 线程"
 *
 * @details
 * 本文件**不再**认识任何外设细节: 串口设备/波特率/中断/缓冲 都搬到了
 * platform/bsp/bsp_uart.c, 装配顺序都归 init_thread.c。留在这里的只有三件事:
 *   1. 启动 banner（直接走 bsp 裸写，此时日志系统还没初始化）;
 *   2. 创建 init 任务;
 *   3. `app_start()`: 起 init 任务然后返回 —— 真正的 `main()` 在
 *      `Core/Src/main.c`（CubeMX 生成），由它在 `vTaskStartScheduler()` 之前
 *      调用 `app_start()`。
 *
 * @par 与 Zephyr 版的差异
 *   - 原文件的 `system_get_time_ms()`（`k_uptime_get_32()`）**已删除** ——
 *     它的 FreeRTOS 实现现在在 `platform/port/port_impl.c` 的
 *     `PORT_OS_FREERTOS` 分支里（`xTaskGetTickCount() * portTICK_PERIOD_MS`）。
 *     留在这里会和 port 层重复定义。
 *   - 原文件有 `main()`，本版**没有** —— 整个工程只能有一个 `main()`,
 *     由 CubeMX 生成的 `Core/Src/main.c` 提供。
 *   - `k_thread_create` → `xTaskCreate`（**动态**，理由见下）。
 *
 * @par init 任务为什么用动态分配，而协议解析任务用静态
 * init 任务是**一次性**的: 它把系统装配完就 `vTaskDelete(NULL)` 退出。
 * 但静态分配的栈**不会**随任务删除而回收 —— `vTaskDelete` 对
 * `xTaskCreateStatic` 创建的栈只标记任务无效，不会释放（这正是静态分配
 * 的定义: 内存归调用方）。于是那 4KB 栈 + TCB 会永久白白占着 SRAM。
 *
 * 换成 `xTaskCreate` 后，栈与 TCB 来自 FreeRTOS 堆（heap_4），任务被删除后
 * 由空闲任务调用 `prvDeleteTCB` → `vPortFree` 归还给堆，后续
 * `system_malloc` / StreamBuffer / 信号量都能复用这 4KB。
 * 代价：启动峰值多占 4KB 堆（heap_4 会合并相邻空闲块，碎片风险可控）。
 *
 * 反之 `protocol_parser` 任务是常驻的（`for(;;)` 永远不退出），静态分配
 * 更合适：栈直接进 `.bss` 可在 map 里核对，且不占堆。见 init_thread.c。
 *
 * @note 本文件**不碰串口**: 串口的"设波特率 + 开通道"是 init_thread 顺序表
 *       第 2 步 (`bsp_uart_set_baudrate` + `bsp_uart_start`), 那里是唯一调用点。
 *       两者都已幂等, 但把调用点收敛在一处仍是有意的 —— 否则"波特率到底谁设的"
 *       会变成第二个说不清的问题。
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
 * 注意 APP_STACK_WORDS(): INIT_STACK_SIZE 沿用 Zephyr 的**字节**语义，
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
