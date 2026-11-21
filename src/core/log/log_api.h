/**
 * @file log_api.h
 * @brief 日志抽象门面 —— 全工程唯一的日志 API 表面
 *
 * @par 为什么需要这一层
 * 日志是全工程最横切的 API。若业务代码直接调用某个日志库
 * (EasyLogger 的 elog_i / rxi 的 log_info), 换后端时所有调用点都要改,
 * 且两库的宏名、参数个数、初始化方式都不一致。
 *
 * 本门面与 port.h 同构: core/ 只认 log_api.h, 换后端只改本文件的
 * 后端选择宏, 调用点零修改。与 param 存储后端同一套"编译期互斥"
 * 思路 (param_manager_config.h 的 PARAM_*_ENABLE), 而非 VFS 的
 * "运行期注册" —— 因为同一时刻只会有一个 logger 生效, 未选中的
 * 必须从 ROM 里整体消失。
 *
 * @par 后端选择 (编译期三选一, 可用 -D 覆盖)
 *   - LOG_BACKEND_EASYLOGGER (默认): tag 过滤 + hexdump + 缓冲输出
 *   - LOG_BACKEND_RXI:            仅级别 + 线程安全, tag 被丢弃
 *   - LOG_BACKEND_NONE:           全部编译为空 (关日志)
 *
 * @par 用法 (标签属于模块, 不属于每条日志)
 * @code
 *   #define LOG_TAG "param"        // 必须在 #include "log_api.h" 之前
 *   #include "log_api.h"
 *
 *   LOG_I("flush ok, module=%d", id);
 *   LOG_HEXDUMP(16, buf, len);
 * @endcode
 *
 * @par 装配顺序 (app 层 init)
 * @code
 *   system_mutex_init();          // port.h, 先于日志 (输出锁依赖)
 *   log_sink_set(uart_tx_write);  // 注入字节出口 (可为 NULL)
 *   log_init();                   // 初始化并开启输出
 * @endcode
 */

#ifndef CORE_LOG_API_H
#define CORE_LOG_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  输出 sink —— 与 file_transfer 的 ft_tx_fn 同构的字节出口
 *
 *  后端格式化好一行后把字节交给 sink, 落到 UART/文件/Flash 由实现方
 *  决定。core 不持有任何物理通道句柄 (和 file_transfer "不认识 UART"
 *  是同一纪律)。
 * ================================================================ */

/** @brief 字节出口回调 (与 file_transfer 的 ft_tx_fn 同签名, 可互传) */
typedef void (*log_sink_fn)(const uint8_t *data, uint32_t len);

/* ================================================================
 *  后端选择
 * ================================================================ */

#if !defined(LOG_BACKEND_EASYLOGGER) && !defined(LOG_BACKEND_RXI) && \
    !defined(LOG_BACKEND_NONE)
    #define LOG_BACKEND_EASYLOGGER 1
#endif

/* ================================================================
 *  后端生命周期 API
 *
 *  EASYLOGGER 后端在 log_elog_port.c 提供实现; 其余后端退化为空宏,
 *  应用层无需 #if 保护即可无条件调用 log_init() / log_sink_set()。
 * ================================================================ */

#if defined(LOG_BACKEND_EASYLOGGER)
    void log_sink_set(log_sink_fn fn);
    int  log_init(void);
#else
    #define log_sink_set(fn)   ((void)(fn))
    #define log_init()         (0)
#endif

/* ================================================================
 *  LOG_* 宏映射 —— 调用面唯一入口
 * ================================================================ */

/* ================================================================
 *  模块标签 LOG_TAG —— 每条日志不再重复写标签
 *
 *  每个 .c 在自己顶部 (**在 `#include "log_api.h" 之前**) 写一行:
 *      #define LOG_TAG "vfs"
 *  之后所有 LOG_* 调用自动带上这个标签, 调用点只留格式串与参数。
 *
 *  三条设计约束:
 *   1. 标签是**模块属性**, 不是"每条日志的选择"。同一文件的日志天然同源;
 *      逐条写标签只会让大部分调用点重复同一个字符串, 改标签要改几十处。
 *   2. 换后端不迁移调用点: 有标签的后端 (EasyLogger) 用 LOG_TAG;
 *      没有标签的后端 (rxi / 裸 printf 重定向) 在映射处直接**丢弃**它 ——
 *      这正是把标签从调用面移到模块面的目的。
 *   3. 未定义 LOG_TAG 也不报错, 落到默认值 "app"; 但新文件请显式写
 *      LOG_TAG, 否则串口日志看不出是哪个模块在说话。
 * ================================================================ */
#ifndef LOG_TAG
#define LOG_TAG "app"
#endif

#if defined(LOG_BACKEND_EASYLOGGER)
    /* ---- EasyLogger (带 tag 过滤) ---- */
    #include <elog.h>
    #define LOG_A(...)      elog_a(LOG_TAG, __VA_ARGS__)
    #define LOG_E(...)      elog_e(LOG_TAG, __VA_ARGS__)
    #define LOG_W(...)      elog_w(LOG_TAG, __VA_ARGS__)
    #define LOG_I(...)      elog_i(LOG_TAG, __VA_ARGS__)
    #define LOG_D(...)      elog_d(LOG_TAG, __VA_ARGS__)
    #define LOG_V(...)      elog_v(LOG_TAG, __VA_ARGS__)
    #define LOG_RAW(...)    elog_raw(__VA_ARGS__)   /* elog_raw 本身无 tag */
    #define LOG_HEXDUMP(width, buf, size) \
            elog_hexdump(LOG_TAG, (uint8_t)(width), (uint8_t *)(buf), (uint16_t)(size))

#elif defined(LOG_BACKEND_RXI)
    /* ---- rxi/log.c (无 tag) ----
     * 尚未接线: rxi 无 tag/hexdump/异步, 接入时把其 log.h 改名后在此 include。
     * 这里就是 LOG_TAG 被丢掉的落点 —— 也是本改造要保下的扩展点。 */
    #error "LOG_BACKEND_RXI not wired yet: map rxi log_* here (LOG_TAG ignored)"

#else /* LOG_BACKEND_NONE */
    /* ---- 关日志: 全部编译为空 (含 LOG_TAG, 所以它变成了未使用的宏, 无害) ---- */
    #define LOG_A(...)      ((void)0)
    #define LOG_E(...)      ((void)0)
    #define LOG_W(...)      ((void)0)
    #define LOG_I(...)      ((void)0)
    #define LOG_D(...)      ((void)0)
    #define LOG_V(...)      ((void)0)
    #define LOG_RAW(...)    ((void)0)
    #define LOG_HEXDUMP(width, buf, size) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* CORE_LOG_API_H */
