/**
 * @file log_init.h
 * @brief 日志子系统装配 —— 字节出口 + 后端启动 + 启动行补打
 *
 * @par 与 core/log 的分工
 * core/log (`log_api.h` / `log_elog_port.c`) 是**门面与后端**: 它不认识任何
 * 物理口, 只认一个注入进来的 sink。本文件是**应用层装配**: 决定"日志往哪几个
 * 口吐", 然后把出口注入进去。与 shell 的装配 (`app/init/shell_init.c`)
 * 彼此独立 —— 两个组件各自持有自己的出口策略。
 *
 * @par 为什么日志与 shell 分成两个文件装配
 * 这两件事的生命周期不同: 日志要在第 3 步就起来 (后面每一步都要用), 而 shell
 * 必须是**最后一步** (见 shell_init.h 的解释)。合在一个"诊断设施"里就只能
 * 同进同退, 提示符会被插在启动主线中间。分开放, 各自在自己该在的位置被调用。
 *
 * @note 函数名为什么是 `log_boot_init()` 而不是 `log_init()`:
 *       `core/log/log_api.h` 已经声明了运行时的 `log_init()`, 重名会撞。
 *       "boot" 在这里是"启动期装配"的意思, 与 boot_stage 同一语境。
 */

#ifndef LOG_INIT_H
#define LOG_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 装配日志子系统: 注入扇出出口 → log_init() → 补打启动暂存行
 *
 * @return true 成功;
 *         false 后端起不来 —— 此时**没有任何日志通道**, 已用裸写向调试口报过
 *               一句 (`boot_stage_bare`), 调用方只能放弃整个启动。
 *
 * @note 前置条件: `system_mutex_init()` 已调用 (elog 的输出锁依赖它)、
 *       `bsp_uart_start()` 已成功 (字节出口依赖它)。
 * @note 成功后 `boot_stage_enable()` 已被调用 —— 启动顺序表第 1~2 步
 *       (互斥锁 / 串口) 暂存的行会在这一刻补打出来。
 *
 * @note **没有对称的 deinit, 这是有意的**: 日志是退出路径上唯一的报告通道,
 *       失败回滚时把它关掉, 后面就没人说话了。真要静音请用后端自己的
 *       `elog_set_output_enabled()`, 而不是拆掉装配。
 */
bool log_boot_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_INIT_H */
