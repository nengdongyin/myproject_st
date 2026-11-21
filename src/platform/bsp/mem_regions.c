/**
 * @file mem_regions.c
 * @brief 板级内存白名单表 (STM32F103ZET6 / LQFP144)
 *
 * 地址取自 STM32F103xE 参考手册的存储器映像:
 *   Flash: 0x08000000 .. 0x08080000   (512 KiB)
 *   SRAM : 0x20000000 .. 0x20010000   (64 KiB)
 *
 * 换板子时改下面这张表即可; 本表与 RTOS 无关。
 *
 * 只读段 (writable = 0) 上 `mw` 会被拒绝; `md` 不受 writable 影响。
 *
 * @note 与 RP2350 版的差异: 原板是 4MB XIP flash + 520KB SRAM，本板只有
 *       512KB flash + 64KB SRAM —— 调试时 `md` 越界会直接读到 0xFFFFFFFF
 *       或触发总线异常，白名单把范围卡死能避免误踩。
 */

#include "mem_regions.h"

/** @brief 本板可访问地址段 (顺序无关, 判定用"整段落在同一段内") */
static const mem_region_t s_regions[] = {
    /* 内部 flash: 只读。写它要走 FLASH 编程流程(解锁/页擦/写入), 不是 memcpy 能做的事 */
    { "flash", 0x08000000u, 0x00080000u, 0 },

    /* SRAM: 可读写 (调试时看栈/全局变量、改临时变量都在这) */
    { "sram",  0x20000000u, 0x00010000u, 1 },

    /* ---- 外设段: 默认不开放 ----
     * 打开前请确认: (1) 该地址在本板确实存在; (2) 读它没有副作用;
     * (3) 写它不会把总线/时钟锁死。示例:
     *   { "apb1", 0x40000000u, 0x00010000u, 1 },
     *   { "apb2", 0x40010000u, 0x00010000u, 1 },
     */
};

const mem_region_t *mem_regions_get(size_t *count)
{
    if (count != NULL)
        *count = sizeof(s_regions) / sizeof(s_regions[0]);
    return s_regions;
}

int mem_regions_configure(void)
{
    size_t n = 0;
    const mem_region_t *tbl = mem_regions_get(&n);

    return mem_access_configure(tbl, n);
}