/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点无需逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。 */
#define LOG_TAG "init"

#include "init_thread.h"
#include "protocol_parser_thread.h"
#include "boot_stage.h" /* 启动阶段日志: [BOOT n/7] 前缀 */
#include "bsp_uart.h"
#include "log_init.h"   /* 日志子系统装配 (第 3 步) */
#include "shell_init.h" /* shell 子系统装配 (最后一步) */
#include "protocol_chain_init.h"
#include "filesystem_init.h"
#include "param_manager_init.h"
#include "port.h"
#include "log_api.h"

#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 *  协议解析任务的启动参数 (config.h 可覆盖)
 *
 *  只归本文件用: 任务就在这里创建。
 * ================================================================ */

/** @brief 解析任务栈 (字节)。常驻任务, 永久占用堆 */
#ifndef STACK_SIZE
#define STACK_SIZE 4096
#endif

/** @brief 解析任务优先级。FreeRTOS 数值越大越高, 取值 0..55 */
#ifndef PARSER_TASK_PRIORITY
#define PARSER_TASK_PRIORITY 7
#endif

/** @brief 字节 → FreeRTOS 的字 (4 字节) */
#define APP_STACK_WORDS(bytes) ((uint32_t)((bytes) / (uint32_t)sizeof(uint32_t)))

/* ── 相机 / VSC 管线（当前不参与编译，头文件保留以便后续接回） ──
 * 注意：这些头只提供声明，对应的 .c 在 .cproject 里被排除，
 * 因此下面所有相机调用都保持注释状态，否则会链接失败。 */
#include "vsc_lite.h"
#include "crop_vsc.h"
#include "binning_vsc.h"
#include "decoder_vsc.h"
#include "histogram_vsc.h"
#include "sensor_vsc.h"
#include "sensor_imx296_vsc.h"
#include "vsc_lite_board.h"

/*
 * 协议解析任务的栈与 TCB 用**动态**分配（xTaskCreate），栈来自 FreeRTOS 堆
 * (heap_4)，不占 .bss。
 *
 * 代价：这个任务是常驻的（for(;;) 不退出），所以它的 4KB 会**永久**占着堆，
 * 不像 init 任务那样退出即归还；而且堆不像静态数组那样能在 map 里逐项核对。
 * 换来的好处是 .bss 缩 4KB，静态占用与运行期占用能分开看。
 *
 * 启动峰值堆要同时装下：init 8192（INIT_STACK_SIZE，瞬态，装配完归还）
 * + parser 4096（STACK_SIZE，常驻）+ defaultTask 512（CubeMX 生成）
 * + idle 512 + 定时器任务 1024 + 串口 StreamBuffer 2048 + 互斥锁，
 * 均在 configTOTAL_HEAP_SIZE = 20480 (Core/Inc/FreeRTOSConfig.h) 之内；
 * 每个任务的 TCB 还要额外算一份 newlib _reent（configUSE_NEWLIB_REENTRANT = 1），
 * 实际余量需以运行期数据为准，不宜只看栈字节数相加。
 * 若后续再加任务/StreamBuffer，先确认 heap 余量：FreeRTOS 的
 * uxTaskGetStackHighWaterMark() 已开放（INCLUDE_uxTaskGetStackHighWaterMark=1），
 * 但尚未接成 shell 命令。
 *
 * @note 栈深度用 APP_STACK_WORDS() 把 STACK_SIZE（字节）换成字。
 */

extern void app_sensor_demo(void);

void init_thread_entry(void *arg)
{
    (void)arg;

    /* 本任务只做装配: 内部状态全在各自模块里, 没有应用级上下文要清。 */

    /* ================================================================
     *  装配顺序表 (改顺序前请逐条确认)
     *
     *  1. 平台互斥锁 —— param/file/vfs 的 system_lock(id) 依赖它;
     *  2. 串口就绪   —— log/shell 的字节出口指向这个口, 中断也开始往缓冲灌字节;
     *  3. 日志       —— 必须早于后面任何 LOG_*, 也是第 1~2 步暂存行的补打点;
     *  4. 协议链     —— 建链 + 挂 ymodem/imperx/camyu (内部会打日志);
     *  5. 存储挂载   —— littlefs 可用之后参数才能读旧值;
     *  6. 参数初始化 —— 读参数缓存/备份分区 (shell 的 ft 命令依赖参数);
     *  7. 协议解析线程 —— 它消费第 2 步开始灌入缓冲的字节。
     *
     *                          ── 到这里启动完成 ──
     *
     *  8. shell      —— **最后一步, 且不计入上面的 7 步**。
     *                   它的字节入口就是第 7 步那个解析任务 (纯推送模型,
     *                   shell 自己不 read), 所以提前初始化并不会让它提前可用;
     *                   放到后面才能让提示符出现在启动主线之后, 并且保证敲命令
     *                   时 fs / param 都已就绪。详见 shell_init.h。
     *
     *  失败路径: 任何一步失败都不回滚日志 (它是唯一的报告通道), 但会拆掉
     *  已挂上的协议链 —— 只有它持有解析器实例与串口出口。
     * ================================================================ */

    /* 1) 平台互斥锁 */
    system_mutex_init();
    boot_stage(BOOT_STEP_LOCKS, "sys: %d system mutexes ready",
               (int)SYS_LOCK_COUNT);

    /* 2) 串口通道。失败就没必要往下走 —— 没有出口可报错。
          波特率**不在这里设** —— 它由 CubeMX 在 `MX_USART3_UART_Init()` 里配
          (`.ioc` 的 USART3 页, 460800), 是唯一来源。本步只"开通道":
          建 StreamBuffer + 武装接收。
          运行时若要改速, 用 bsp_uart_set_baudrate() 单独调, 不必重启通道。 */
    if (bsp_uart_start() != 0)
    {
        /* 这一行是**裸写**: 日志在第 3 步才起来, 此处 LOG_* 会被静默丢弃;
           串口起不来时现场只能靠这句看到原因。 */
        boot_stage_bare("[boot] uart channel start failed (bsp_uart_start)\r\n");
        goto exit_task;
    }
    boot_stage(BOOT_STEP_UART, "uart: comm/dbg channel open");

    /* 3) 日志子系统 (同时补打第 1~2 步暂存的启动行) */
    if (!log_boot_init())
    {
        /* 此时已无任何日志通道可选, log_boot_init 已裸写报过一句 */
        goto exit_task;
    }

    /* 4) 协议链 (内部逐条报告: ymodem / imperx / camyu) */
    if (!protocol_chain_init())
    {
        goto exit_task;
    }

    /* 5) 存储挂载 (内部逐卷报告: /lfs、/raw 及跳过项) */
    if (!filesystem_init())
    {
        goto fail_fp;
    }

    /* 6) 参数加载/初始化 */
    param_manager_init();

    /* ── VSC Lite pipeline（相机子系统当前不参与编译，保持注释） ──
    vsc_lite_pipeline_t isp_pipe;
    int rc = vsc_lite_pipeline_init(&isp_pipe, vsc_lite_board_stages,
                                    vsc_lite_board_num_stages);
    if (rc != VSC_OK) {
        LOG_E("VSC Lite pipeline init failed: %d", rc);
    }
    */

    /* 7) 协议解析任务 —— 最后一个起来，它消费第 2 步开始灌入的字节。
          动态分配（与 init 任务一致）：栈与 TCB 来自 FreeRTOS 堆，不占 .bss。
          链对象经 pvParameters 传给它 —— 比再开一个全局句柄干净。
          创建失败通常是堆不足 —— 打日志而不静默继续，否则系统“跑着但不收数据”。 */
    if (xTaskCreate(protocol_parser_thread_entry, "proto",
                    APP_STACK_WORDS(STACK_SIZE),
                    (void *)protocol_chain_handle(),
                    (UBaseType_t)PARSER_TASK_PRIORITY, NULL) != pdPASS)
    {
        boot_stage_fail(BOOT_STEP_TASK,
                        "task: protocol parser create failed (heap too small?)");
    }
    else
    {
        boot_stage(BOOT_STEP_TASK, "task: protocol parser started (prio %u, stack %u B)",
                   (unsigned)PARSER_TASK_PRIORITY, (unsigned)STACK_SIZE);
    }

    boot_stage_done();

    /* 操作提示 —— 不是"状态回显" (状态由上面各步骤自己报告), 只列两个操作者
       真的会用到的命令字。进 shell 的 0xF003 由下一行给出, 这里不重复。 */
    boot_stage(0, "hint: ft recv <- Imperx WRITE 0xF000, ft send <- 0xF001");

    /* 8) shell —— **启动顺序的最后一步**。理由见 shell_init.h:
          shell 是纯推送模型, 字节入口就是第 7 步刚起的解析任务, 提前初始化
          并不会让它提前可用, 只会让提示符把启动日志切成两截。
          shell 起不来不影响协议面 (已在内部报错), 所以不中断。 */
    (void)shell_boot_init();

    goto exit_task;

fail_fp:
    protocol_chain_cleanup();

exit_task:
    /* FreeRTOS 的任务函数**不允许 return**（会跳到非法地址）。
       装配完成后本任务无事可做，自删除。 */
	while(1){
		vTaskDelay(100);
	}
    //vTaskDelete(NULL);
}
