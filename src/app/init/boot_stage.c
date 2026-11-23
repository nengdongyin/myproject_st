/**
 * @file boot_stage.c
 * @brief 启动阶段日志实现 —— 统一前缀 + 日志就绪前的暂存与补打
 *
 * @details
 * 两件事, 都很短:
 *   1. 前缀: `[BOOT %u/%u] ` —— 只放步骤号 (取 BOOT_STEP_*), 内容交 vsnprintf,
 *      长度受 BOOT_LINE_MAX 截断。**时间不在这里**: elog 行首已经有
 *      `[hh:mm:ss.mmm]` (见 core/log/log_elog_port.c 的 elog_port_get_time),
 *      同一份数据在主线上再追一遍 "+12ms" 只会把行拉长。
 *   2. 就绪前的暂存: log_init() 之前 elog 的 output_enabled = false,
 *      LOG_* 会被静默丢掉 (elog_output 开头就 return)。第 1~2 步的行先存进
 *      s_prelog, boot_stage_enable() 之后按原级别补打。
 *
 * @note 为什么用静态字节数组而不是堆: boot_stage 会在**堆管理器可用之前**
 *       (第 1 步只初始化互斥锁) 被调用, 那时不能指望 system_malloc。静态区
 *       总共 BOOT_PRELOG_LINES × BOOT_PRELOG_LINE_MAX = 2 × 64 = 128 字节。
 *
 * @note 本文件只管"打日志", 不判断任何装配结果 —— 成功/失败由调用方传级别。
 */

/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点无需逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。
 *  @note 所有启动阶段行共用 "boot" 标签: 它们是**同一份顺序表**的产物,
 *        不归属任何单个子系统 (子系统自己的细节行仍留在子系统标签下)。 */
#define LOG_TAG "boot"

#include "boot_stage.h"

#include "log_api.h"  /* 门面: LOG_I/W/E */
#include "port.h"     /* system_get_time_ms */
#include "port_rtos.h" /* port_heap_info: 收尾那行堆预算 */
#include "bsp_uart.h" /* boot_stage_bare 的裸写出口 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 *  行缓冲与"日志就绪前"的暂存区
 * ================================================================ */

/** @brief 暂存行数上限 —— 只需覆盖第 1~2 步(互斥锁 / 串口)的两行 */
#ifndef BOOT_PRELOG_LINES
#define BOOT_PRELOG_LINES 2
#endif

/**
 * @brief 暂存区单行上限
 * @note 与下面的 BOOT_LINE_MAX **分开定义**: 两块的需求量完全不同 ——
 *       第 1~2 步的行最长约 40 字符 ("[BOOT 1/7] sys: 3 system mutexes
 *       ready"), 64 有富余; 而 BOOT_LINE_MAX 是栈上的格式化缓冲, 要容纳
 *       后面各阶段几十到上百字符的正文。合成一个宏就只能取大的那个,
 *       静态区会跟着白白胀到 4 倍。
 */
#ifndef BOOT_PRELOG_LINE_MAX
#define BOOT_PRELOG_LINE_MAX 64
#endif

/** @brief 单行上限。config.h 把 ELOG_LINE_BUF_SIZE 压到 256, 这里取一半留余量 */
#ifndef BOOT_LINE_MAX
#define BOOT_LINE_MAX 128
#endif

/** @brief 输出级别 (本文件内部序号, 与 elog 的 ELOG_LVL_* 解耦) */
enum
{
    BOOT_LVL_INFO = 0,
    BOOT_LVL_WARN,
    BOOT_LVL_ERROR,
};

static char    s_prelog[BOOT_PRELOG_LINES][BOOT_PRELOG_LINE_MAX];
static uint8_t s_prelog_lvl[BOOT_PRELOG_LINES];
static uint8_t s_prelog_cnt; /**< 已暂存行数 */
static bool    s_ready;      /**< true = logger 已就绪, 之后的调用直接输出 */

/**
 * @brief 输出 (或在未就绪时暂存) 一整行
 *
 * @note 溢出策略: 直接丢弃, **不做环形覆盖** —— 第 1~2 步不该产生这么多行,
 *       真溢出说明有人在这里塞了循环日志, 覆盖只会把真正重要的第一行挤掉。
 */
static void boot_emit(uint8_t lvl, const char *line)
{
    if (!s_ready)
    {
        if (s_prelog_cnt < BOOT_PRELOG_LINES)
        {
            strncpy(s_prelog[s_prelog_cnt], line, BOOT_PRELOG_LINE_MAX - 1u);
            s_prelog[s_prelog_cnt][BOOT_PRELOG_LINE_MAX - 1u] = '\0';
            s_prelog_lvl[s_prelog_cnt] = lvl;
            s_prelog_cnt++;
        }
        return;
    }

    switch (lvl)
    {
    case BOOT_LVL_WARN:
        LOG_W("%s", line);
        break;
    case BOOT_LVL_ERROR:
        LOG_E("%s", line);
        break;
    default:
        LOG_I("%s", line);
        break;
    }
}

/**
 * @brief 把 `[BOOT n/N] <正文>` 格式化进 buf；step == 0 时只放正文
 * @return true = buf 可用; false = 前缀都放不下 (理论不可达, 但不越界写)
 *
 * @note 时间不在本函数里出现 —— 它已经是 elog 行首的 `[hh:mm:ss.mmm]`。
 *       同一份数据在主线上再追一遍 "+12ms" 只会让行变长、变难看。
 */
static bool boot_format(char *buf, size_t sz, unsigned step,
                        const char *fmt, va_list ap)
{
    int n = 0;

    if (step > BOOT_STAGE_TOTAL)
    {
        step = BOOT_STAGE_TOTAL; /* 步骤号写错时不至于打出 9/7 */
    }

    if (step != 0u)
    {
        n = snprintf(buf, sz, "[BOOT %u/%u] ", step, BOOT_STAGE_TOTAL);
        if (n < 0 || (size_t)n >= sz)
        {
            return false;
        }
    }

    vsnprintf(buf + n, sz - (size_t)n, fmt, ap);
    return true;
}

/** @brief 三个级别共用的格式化 + 输出路径 */
static void boot_stage_v(uint8_t lvl, unsigned step, const char *fmt, va_list ap)
{
    char buf[BOOT_LINE_MAX];

    if (fmt == NULL)
    {
        return;
    }
    if (boot_format(buf, sizeof(buf), step, fmt, ap))
    {
        boot_emit(lvl, buf);
    }
}

/* ================================================================
 *  公开接口
 * ================================================================ */

void boot_stage(unsigned step, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    boot_stage_v(BOOT_LVL_INFO, step, fmt, ap);
    va_end(ap);
}

void boot_stage_warn(unsigned step, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    boot_stage_v(BOOT_LVL_WARN, step, fmt, ap);
    va_end(ap);
}

void boot_stage_fail(unsigned step, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    boot_stage_v(BOOT_LVL_ERROR, step, fmt, ap);
    va_end(ap);
}

void boot_stage_enable(void)
{
    s_ready = true;

    /* 补打暂存行: 此时 elog 已 elog_start(), output_enabled = true。
       逐行按原级别重放, 顺序与产生顺序一致。 */
    for (uint8_t i = 0; i < s_prelog_cnt; i++)
    {
        boot_emit(s_prelog_lvl[i], s_prelog[i]);
    }
    s_prelog_cnt = 0;
}

void boot_stage_done(void)
{
    /* 收尾标记: 与 Linux 启动日志最后一行同性质 —— 读者一眼看出"到这里就绪了"。
       总耗时直接取 uptime: init 任务是调度器起来后第一个跑的, 所以它就是启动耗时。 */
    boot_stage(0, "===== boot complete in %lu ms =====",
               (unsigned long)system_get_time_ms());

    /* 堆预算: 此刻 init 任务(8KB 栈)仍存活, 所以 min_free 反映的是**含启动峰值**的
       最低水位 —— 判断 configTOTAL_HEAP_SIZE 能否收紧就看这个数。
       free 命令能随时查到同样的值, 但产线固件关掉 shell 后只剩这一行。 */
    port_heap_info_t hp;

    if (port_heap_info(&hp) == 0 && hp.total != 0u)
    {
        boot_stage(0, "heap: total %u B, min-free %u B, peak used %u B",
                   (unsigned)hp.total, (unsigned)hp.min_free,
                   (unsigned)(hp.total - hp.min_free));
    }
}

void boot_stage_bare(const char *msg)
{
    if (msg == NULL)
    {
        return;
    }

    /* 此刻日志系统不可用(甚至串口通道都没建), 只能裸写调试口。
       安全性: bsp_dbg_write 用的是 CubeMX 在 main() 里就初始化好的调试口句柄,
       不依赖 bsp_uart_start() —— main.c 的启动 banner 走的也是这条路。 */
    bsp_dbg_write((const uint8_t *)msg, (uint32_t)strlen(msg));
}
