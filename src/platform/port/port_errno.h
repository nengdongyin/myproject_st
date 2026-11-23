/**
 * @file port_errno.h
 * @brief POSIX 错误码兜底 —— 给不提供完整 <errno.h> 的工具链（ARM C 库 / armcc）
 *
 * @par 问题
 * 本工程的错误返回契约是「负 errno」（见 src/core/vfs/vfs.h 的 errno_contract
 * 与 src/platform/port/port_rtos.h）。这套宏来自 **POSIX**，而 ARM C 库
 * （Keil MDK 的 AC5 armcc / AC6 armclang）只保证 ISO C 的 EDOM / ERANGE / EILSEQ
 * 一族 —— ENOENT、ENOTSUP、ENOSPC、EROFS、EBADF…… 多半不存在。
 * 一个 vfs_core.c 就有 40 处引用，缺一个宏就是一片 "undefined identifier"。
 *
 * @par 机制
 * 先 #include <errno.h>（让工具链自己的定义生效），再用 #ifndef 逐个补齐缺失项：
 *  - 工具链**已有**的定义原样保留 —— 即便数值与下面不同也无所谓（见下一节）；
 *  - 工具链**缺失**的取 newlib 的数值，与当前 GCC 构建逐位一致。
 *
 * @par 数值为什么取 newlib 的
 * 因为 GNU Tools for STM32 / newlib 是本工程的基线工具链。统一取值后，同一份
 * 源码在两条工具链下产生**完全相同**的负 errno，日志、shell 打印、上位机解析
 * 都不必分支。注意这些值只在"负数取反"的语义里被消费，从不交给 C 库。
 *
 * @par 唯一与 C 库 errno 变量交互的地方
 * shell_parse.c 与 param_storage_iniparser.c 里的 `errno == ERANGE`（strtoul 溢出
 * 检测）。ERANGE 属 ISO C，ARM C 库一定有，因此不受本兜底影响。
 *
 * @note 本文件由 config.h 引入，而 config.h 被构建系统强制预包含
 *       （GCC: `-include config.h`；armcc: `--preinclude config.h`），
 *       所以每个编译单元自动生效，业务代码不必逐个 include。
 * @note 新增 errno 用法时，若该宏不在此列表内，先在这里补一行，
 *       否则会出现在某些工具链下编不过、换回 GCC 又"好了"的漂移。
 */

#ifndef PORT_ERRNO_H
#define PORT_ERRNO_H

#include <errno.h>

/* ================================================================
 *  通用 / 文件与设备（数值与 newlib 对齐）
 * ================================================================ */
#ifndef EPERM
#define EPERM            1  /**< 不允许的操作（白名单外/只读段） */
#endif
#ifndef ENOENT
#define ENOENT           2  /**< 目标不存在（文件/分区/挂载点） */
#endif
#ifndef EIO
#define EIO              5  /**< 硬件 / 底层 I-O 错误 */
#endif
#ifndef EBADF
#define EBADF            9  /**< 非法或已关闭的句柄 */
#endif
#ifndef ENOMEM
#define ENOMEM          12  /**< 内存不足 */
#endif
#ifndef EACCES
#define EACCES          13  /**< 权限不足 */
#endif
#ifndef EBUSY
#define EBUSY           16  /**< 资源忙 / 独占冲突 */
#endif
#ifndef EEXIST
#define EEXIST          17  /**< 已存在 */
#endif
#ifndef ENODEV
#define ENODEV          19  /**< 设备或挂载上下文缺失 */
#endif
#ifndef ENOTDIR
#define ENOTDIR         20  /**< 路径不是目录 */
#endif
#ifndef EISDIR
#define EISDIR          21  /**< 目标是个目录 */
#endif
#ifndef EINVAL
#define EINVAL          22  /**< 参数非法 */
#endif
#ifndef ENFILE
#define ENFILE          23  /**< 打开句柄/描述符池耗尽 */
#endif
#ifndef EFBIG
#define EFBIG           27  /**< 文件过大 */
#endif
#ifndef ENOSPC
#define ENOSPC          28  /**< 空间 / 槽位不足 */
#endif
#ifndef EROFS
#define EROFS           30  /**< 只读挂载点 / 写保护介质 */
#endif
#ifndef ERANGE
#define ERANGE          34  /**< 结果超出范围（ISO C；仅作兜底） */
#endif
#ifndef ENOSYS
#define ENOSYS          88  /**< 功能未实现 / 已编译裁剪 */
#endif
#ifndef ENOTEMPTY
#define ENOTEMPTY       90  /**< 目录非空 */
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG    91  /**< 路径/名字过长 */
#endif
#ifndef EALREADY
#define EALREADY       120  /**< 已在处理中 / 已注册 */
#endif
#ifndef ENOTSUP
#define ENOTSUP        134  /**< 不支持该操作（后端无此能力） */
#endif

#endif /* PORT_ERRNO_H */
