/**
 * @file shell_init.h
 * @brief shell 子系统装配 —— 字节出口 + letter-shell 实例 + md/mw 地址白名单
 *
 * @par 与 core/shell 的分工
 * core/shell (`shell_api.h` / `shell_letter_port.c`) 是**门面与后端**: 不认识
 * UART, 只认注入进来的 sink 和 `shell_rx_feed()` 喂进来的字节。本文件是
 * **应用层装配**: 决定出口往哪写、何时把交互入口交给人。与日志的装配
 * (`app/init/log_init.c`) 彼此独立。
 *
 * @note 函数名为什么是 `shell_boot_init()` 而不是 `shell_init()`:
 *       `core/shell/shell_api.h` 已声明运行时的 `shell_init()`, 重名会撞。
 *
 * @par 为什么这是整个启动顺序的最后一步
 * 本工程是**纯推送模型** —— shell 自己不读设备 (`s_shell.read` 恒为 NULL,
 * 见 shell_letter_port.c), 字节的唯一喂入点是 `protocol_parser_thread`, 而那个
 * 任务比本步更早就在跑。所以把 shell 放在最前面**换不到**"边启动边敲命令"
 * 的调试窗口: 提示符会因为没人喂字节而变成聋的, 敲什么都没反应。
 *
 * 放在最后反而有三个确定收益:
 *   1. 提示符出现在启动主线**结束之后**, 不再把 `[BOOT n/7]` 日志切成两截,
 *      也不会出现 `root:/$ I/boot [BOOT 4/7] ...` 这种提示符与日志抢行首;
 *   2. 敲命令时 fs / param / 协议链都已就绪, 不会打在半初始化的子系统上
 *      (shell 的 ft 命令依赖参数, 见 init_thread.c 的装配顺序表);
 *   3. "看见提示符 = 启动完成" 这一眼可读的因果, 不用回头数日志步骤。
 */

#ifndef SHELL_INIT_H
#define SHELL_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 装配 shell 并把交互入口交出去
 *
 * 本函数返回时, letter-shell 的 banner 与提示符已经在屏幕上 —— 那才是
 * "命令可以敲了" 的唯一标志。
 *
 * @return true 成功; false letter-shell 起不来 (交互入口不可用, 但协议面不受
 *         影响: 通信串口在协议模式下不经过 shell)
 *
 * @note 前置条件: 日志子系统已装配 (本函数会通过 boot_stage 打一行),
 *       `system_mutex_init()` / `bsp_uart_start()` 已完成 (出口与锁依赖)。
 * @note md/mw 地址白名单也在这里配 —— 那个白名单只服务于 shell 的 md/mw
 *       两条命令, 归到"命令入口 + 命令权限"一起, 不另开第三个组件。
 *       失败不中断 (fail-closed: 两条命令保持拒绝)。
 */
bool shell_boot_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_INIT_H */
