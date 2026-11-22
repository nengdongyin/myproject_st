/**
 * @file shell_init.c
 * @brief shell 子系统装配实现 —— 扇出出口 + letter-shell 实例 + md/mw 白名单
 *
 * @details
 * 顺序即依赖:
 *   1. md/mw 地址白名单 —— 必须早于任何一条命令被执行, 否则第一次 md 会被
 *      白名单判成"未配置"而拒绝;
 *   2. 注入出口 —— shellInit() 一起就会打 banner, 那时 s_write 必须已经能写;
 *   3. shell_init() —— 实例化 letter-shell 并打印 banner + 提示符。
 *
 * @note 输出的公告行 (`shell: letter-shell starting`) 打在 shell_init() **之前**:
 *       shellInit() 自己会打 banner 和 `root:/$`, 若等它返回再报一句, 那句就会
 *       挂在提示符后面。
 */

/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点无需逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。 */
#define LOG_TAG "shell"

#include "shell_init.h"

#include "boot_stage.h"   /* 启动后那一行公告 */
#include "bsp_uart.h"     /* 两个物理口的裸写能力 (platform/bsp) */
#include "log_api.h"      /* 门面: LOG_* */
#include "mem_regions.h"  /* md/mw 地址白名单 (core/debug) */
#include "shell_api.h"    /* 门面: shell_sink_set / shell_init */

/* ================================================================
 *  shell 的字节出口 (应用策略, 不是 bsp 能力)
 * ================================================================ */

/**
 * @brief shell 扇出出口
 *
 *  - 调试口: **恒定输出** —— 敲命令那个口若被协议占着, 还能在调试口看到回显;
 *  - 通讯口: **恒定输出** —— 操作者就是在这个口上敲命令, 回显必须回到这里。
 *
 * @note 与 `log_init.c` 的 `log_tx` 是刻意分开的两份策略: 日志要按模式收窄
 *       (协议模式下 ASCII 行会污染帧流), shell 不需要 —— 它本身就是协议流的
 *       替代品。改一处时请顺手看一眼另一处。
 */
static void shell_tx(const uint8_t *data, uint32_t len)
{
    bsp_dbg_write(data, len);
    bsp_comm_write(data, len);
}

/* ================================================================
 *  装配
 * ================================================================ */

bool shell_boot_init(void)
{
    /* 1) md/mw 地址白名单: 板级表 → core/debug。失败不中断启动,
          两条命令保持拒绝 (fail-closed) —— 这是有意的, 不做"默认全放开"。 */
    if (mem_regions_configure() != 0)
    {
        boot_stage_warn(0, "shell: mem whitelist missing, md/mw refused");
    }

    /* 2) 注入出口 (必须在 shell_init 之前: banner 立刻要写) */
    shell_sink_set(shell_tx);

    /* 3) 公告 + 起实例。
          公告与 banner 的先后顺序是有讲究的, 见文件头的 @note。 */
    boot_stage(0, "shell: letter-shell starting (WRITE 0xF003 -> shell)");

    if (shell_init() != 0)
    {
        boot_stage_fail(0, "shell: letter-shell init failed");
        return false;
    }

    return true;
}
