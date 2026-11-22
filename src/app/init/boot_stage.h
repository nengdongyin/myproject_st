/**
 * @file boot_stage.h
 * @brief 启动阶段日志 —— dmesg 风格的 [BOOT n/N +xxms] 主线
 *
 * @par 为什么单独一层
 * 装配顺序表在 init_thread.c, 但每一步的**细节**只有子系统自己知道
 * (挂载了哪些卷 / 选中哪个存储后端 / 挂了哪些协议)。让每个子系统各自
 * 打印"我是第几步"会把顺序表复制 N 份 —— 改一次顺序要改 N 个文件。
 * 本模块只负责**统一前缀**: 步骤号; 内容由调用方给。
 * 时间戳**不在这里**—— elog 行首的 `[hh:mm:ss.mmm]` 已经带了 (见
 * core/log/log_elog_port.c 的 elog_port_get_time), 同一份数据不重复写。
 *
 * @par 输出形如
 * @code
 *   [00:00:01.234] I/boot   [BOOT 5/7] fs: mount /lfs (littlefs) ok
 *                           ^^^^^^^^^^ 本模块生成, 后面是调用方的正文
 *   [00:00:01.240] I/boot   fs: mount /raw (rawflash) ok
 *                           ^ step 传 0: 同一阶段内部的细节行, 不编号
 *   [00:00:01.251] I/boot   ===== boot complete in 21 ms =====
 * @endcode
 *
 * @par 用法约束
 * - `boot_stage(n, ...)`   n 取 BOOT_STEP_*, 与 init_thread.c 的顺序表一致;
 * - `boot_stage(0, ...)`   不编号 —— 同一阶段内部的细节行用;
 * - 步骤号**只在本文件定义一次**(boot_step_t), 别处不许写字面量;
 * - 本模块**不改任何装配顺序**, 只在已有调用点旁边加一行输出。
 *
 * @par 日志未就绪时怎么办
 * 装配顺序表第 1~2 步 (互斥锁 / 串口) 跑在 log_init() **之前**, 那时 elog
 * 的 output_enabled 还是 false, 直接 LOG_* 会被整条丢掉 —— "1/7、2/7"
 * 就永远不出现, 顺序表看着是断的。本模块把这几行**暂存**, 由
 * boot_stage_enable() 在 logger 起来后一次补打。见 boot_stage.c。
 */

#ifndef BOOT_STAGE_H
#define BOOT_STAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  步骤号 —— 与 init_thread.c 的装配顺序表一一对应
 *
 *  改装配顺序就改这里。init_thread.c 与各子系统都引用这些名字。
 * ================================================================ */
typedef enum
{
    BOOT_STEP_LOCKS = 1, /**< 1) 平台互斥锁 */
    BOOT_STEP_UART,      /**< 2) 串口通道 */
    BOOT_STEP_LOG,       /**< 3) 日志子系统 (log_init.c) */
    BOOT_STEP_PROTO,     /**< 4) 协议链 */
    BOOT_STEP_FS,        /**< 5) 存储挂载 */
    BOOT_STEP_PARAM,     /**< 6) 参数加载 */
    BOOT_STEP_TASK,      /**< 7) 协议解析任务 */
} boot_step_t;

/** @brief 阶段总数 (跟随 boot_step_t 最后一项, 不要手改数字)
 *
 *  @note shell **不计入**这 7 步: 它是启动完成之后的交互入口, 不是子系统。
 *        它的那一行用 `boot_stage(0, ...)` 打在不编号的"第 8 行"位置。 */
#define BOOT_STAGE_TOTAL ((unsigned)BOOT_STEP_TASK)

/**
 * @brief 输出一行启动日志: `[BOOT 5/7] <正文>` (step 传 0 则只有正文)
 * @param step BOOT_STEP_*; 传 0 表示"不编号"(同一阶段内部的细节行)
 */
void boot_stage(unsigned step, const char *fmt, ...);

/** @brief 同上, 走 warn 级别 (可接受的降级) */
void boot_stage_warn(unsigned step, const char *fmt, ...);

/** @brief 同上, 走 error 级别 (该步骤失败) */
void boot_stage_fail(unsigned step, const char *fmt, ...);

/**
 * @brief 声明"日志后端已就绪"并补打暂存的早期行
 * @note 必须在 log_init() 成功之后调用一次 (见 app/init/log_init.c); 重复调用无害。
 */
void boot_stage_enable(void);

/** @brief 收尾一行: 启动总耗时 */
void boot_stage_done(void);

/**
 * @brief 日志尚不可用时的裸写兜底 (直接写调试口, 不经 log_api / elog)
 * @param msg 完整的、已带 `\r\n` 的字符串
 * @note 用于"日志系统自己起不来"的路径 —— 那时 LOG_* 全部无效。与
 *       app/init/log_init.c 里 log init 失败的裸写是同一手法。
 */
void boot_stage_bare(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_STAGE_H */
