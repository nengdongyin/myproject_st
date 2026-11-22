/**
 * @file log_init.c
 * @brief 日志子系统装配实现 —— 扇出出口 + 后端启动 + 启动行补打
 *
 * @details
 * 只有两步, 但顺序不能换:
 *   1. `log_sink_set()` 注入出口 —— 后端格式化好一行后把字节交给它;
 *   2. `log_init()` 起后端 (elog_init + elog_start)。
 * 之后才能 `boot_stage_enable()` 补打暂存行 —— 补打的字节也要经 1 装好的
 * 出口, 抢先调用只会打到 NULL sink 上。
 */

/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点无需逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。 */
#define LOG_TAG "log"

#include "log_init.h"

#include "boot_stage.h"   /* 启动阶段日志: [BOOT 3/7] 前缀 */
#include "bsp_uart.h"     /* 两个物理口的裸写能力 (platform/bsp) */
#include "console_mode.h" /* 通讯口那一路的模式判断要用 (见 log_tx) */
#include "log_api.h"      /* 门面: LOG_* / log_sink_set / log_init */

/* ================================================================
 *  日志的字节出口 (应用策略, 不是 bsp 能力)
 * ================================================================ */

/**
 * @brief 日志扇出出口
 *
 *  - 调试口: **恒定输出** —— 与串口模式无关, 排障时总能看到全量;
 *  - 通讯口: 仅在 `CONSOLE_MODE_SHELL` 时输出 —— 协议模式下这些 ASCII 行
 *    会被主机当成帧解析, 只能在 shell 模式下进这条线。
 *
 * @note 为什么选路写在这里而不在 platform/bsp: 这里依赖 core 的 console_mode,
 *       下沉到 bsp 会形成 platform → core 的反向依赖。
 * @note 本函数与 `shell_init.c` 的 `shell_tx` 是**刻意分开的两份**, 不是漏改
 *       的重复: 日志与 shell 对"该往哪个口写"的诉求本来就不同 —— 日志怕污染
 *       协议流, shell 本身就是协议流的替代品。共用一个出口等于把两个组件的
 *       策略焊死。改一处时请顺手看一眼另一处。
 */
static void log_tx(const uint8_t *data, uint32_t len)
{
    bsp_dbg_write(data, len);

    if (console_mode_get() == CONSOLE_MODE_SHELL)
    {
        bsp_comm_write(data, len);
    }
}

/* ================================================================
 *  装配
 * ================================================================ */

bool log_boot_init(void)
{
    /* 1) 注入出口 (必须在 log_init 之前: 后端一起来就会开始吐字节) */
    log_sink_set(log_tx);

    /* 2) 起后端 */
    if (log_init() != 0)
    {
        /* 日志是最底层的报告通道: 起不来就没有任何 LOG_* 可用, 只能裸写
           调试口说一句。这也正是 boot_stage_bare() 存在的场景。 */
        boot_stage_bare("[boot] log init failed\r\n");
        return false;
    }

    /* 3) 补打启动顺序表第 1~2 步暂存的行 (互斥锁 / 串口)。
          必须在后面任何一行 LOG_* 之前完成, 否则顺序会错位。 */
    boot_stage_enable();
    boot_stage(BOOT_STEP_LOG, "log: EasyLogger up");

    return true;
}
