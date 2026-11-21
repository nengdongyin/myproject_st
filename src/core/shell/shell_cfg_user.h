/**
 * @file shell_cfg_user.h
 * @brief 把 SHELL_CFG_USER 用「强制预处理头」注入，替代命令行 -D
 *
 * @par 为什么需要这个文件
 * letter-shell 的 `src/shell_cfg.h` 里有一行 `#include SHELL_CFG_USER` ——
 * 宏必须是**字符串字面量**才能展开成合法的 include 名字。原 Zephyr 工程靠
 * CMake 的 `target_compile_definitions(... "SHELL_CFG_USER=\"shell_config.h\"")`
 * 注入，而搬到 STM32CubeIDE 的托管构建后这条路走不通：
 *
 * CubeIDE 的 CDT 在 Windows 上会把 `-D` 选项值里的**反斜杠当作路径分隔符
 * 规范成 `/`**，于是 `SHELL_CFG_USER=\"shell_config.h\"` 在生成的 makefile 里
 * 变成 `-DSHELL_CFG_USER=/"shell_config.h/"`，宏展开后不是字符串字面量，
 * 编译报：
 * @verbatim
 *   shell_cfg.h:16:10: error: #include expects "FILENAME" or <FILENAME>
 * @endverbatim
 * 顺带把 `shell_ext.h` 裸用 `size_t` 的隐患也暴露出来（该头依赖
 * shell_config.h → port.h → stddef.h 提供 size_t）—— 同一个根因。
 *
 * 另外 `shell_api.h` 里虽然有 `#ifndef SHELL_CFG_USER` 的兜底 `#define`，
 * 但那只对**经过 shell_api.h** 的编译单元有效；letter-shell 后端自身的
 * `shell.c` / `shell_ext.c` 不经过它（它们直接 include "shell.h"），
 * 所以这两个 TU 仍然需要外部注入 —— 这正是原 CMake 注释说的
 * "这个宏必须全局注入"。
 *
 * @par 本文件的解法
 * 用编译器选项 `-include` 强制预处理本文件（见 .cproject 的 Other flags，
 * 与 `-include config.h` 同一个位置），在**任何 #include 之前**把宏定义好。
 * 好处：
 *   - 完全不经过命令行引号，CDT 怎么转义都不影响；
 *   - 与 `config.h` 的强制预处理机制同构，一处生效、全体可见；
 *   - 不违反 config.h 的"禁止覆盖 SHELL_CFG_USER"纪律
 *     （那条纪律针对的是把 *配置项* 塞进 config.h，而这里是注入机制本身）。
 *
 * @note 换配置头文件名只改下面一行。
 * @note 本文件刻意不 include 任何东西 —— 它会在每个 TU 的最前面被展开。
 */

#ifndef SHELL_CFG_USER
#define SHELL_CFG_USER "shell_config.h"
#endif
