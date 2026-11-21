#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "fal_def.h"

/**
 * @file fal_cfg.h
 * @brief FAL 几何配置 —— 本文件是**整片外部 SPI NOR 分区布局的唯一数据源**
 *
 * @par 与 Zephyr 版的差别
 * Zephyr 版（RP2350）描述的是 4MB 内部 flash，地址基址 0x10000000，分区里
 * 有一个 1MB 的 `app`（XIP 固件本体）。本板固件跑在 STM32 内部 flash 里，
 * SPI NOR 是**纯数据介质**，所以：
 *   - 不再有 `app` 分区（固件不在外部 NOR 上）；
 *   - 设备 `addr = 0`，分区 offset 就是设备内偏移（FAL 表配置模式下
 *     partition->offset 原样传给 ops，不做基址换算）。
 *
 * @par 布局（默认 W25Q64 / 8MB；容量由 SPI_NOR_FLASH_SIZE 决定）
 * @verbatim
 *   0x000000 .. 0x001000   param_boot     (4KB  启动分区索引, 只用首字节)
 *   0x010000 .. 0x020000   param_factory  (64KB 出厂默认参数)
 *   0x020000 .. 0x030000   param_user0
 *   0x030000 .. 0x040000   param_user1
 *   0x040000 .. 0x050000   param_user2
 *   0x050000 .. 0x060000   param_user3
 *   0x060000 .. 0x070000   raw0           (64KB rawflash 伪文件 boot_log)
 *   0x070000 .. 0x080000   raw1           (64KB rawflash 伪文件 factory)
 *   0x080000 .. 0x280000   lfs            (2MB  littlefs → /lfs)
 *   --------------------------------------------------
 *   合计 2.5MB，可放进 4MB(W25Q32) 及以上的芯片
 * @endverbatim
 *
 * @note 每个分区起点都对齐到 64KB，好让擦除走 64KB 块擦（约 150ms）而不是
 *       16 次 4KB 扇区擦（约 800ms）。`param_boot` 只有 4KB 是因为它每次
 *       切换分区都要整片擦除一次，扇区擦最省时。
 * @note 改布局只需改本文件与下面的 `_Static_assert`，`storage_config.c` /
 *       `param_storage_common.h` 按名字引用，不需要跟着改。
 */

#define FAL_PART_HAS_TABLE_CFG

/* ================================================================
 *  SPI NOR 设备几何
 * ================================================================ */

/** @brief FAL 设备名（分区表与 FAL_FLASH_DEV_TABLE 共用） */
#define SPI_NOR_FLASH_DEV_NAME  "spi_nor"

/** @brief 芯片总容量。本板：W25Q128 = 16MB (128 Mbit)
 *  W25Q32=4MB, W25Q64=8MB, W25Q128=16MB。
 *  @note 该值同时是 spi_nor_read/write/erase 的边界上限和设备报给 FAL 的 len。
 *        配小了不会报错，但芯片后半部分会变得不可访问（分区表也建不上去）。
 *  @note W25Q128 的 JEDEC ID 是 EF 40 18，容量指数 0x18=24 → 2^24 = 16MB，
 *        在 spi_nor_init() 的容量校验里会与下面的值对账。
 *  @note 16MB 正好是 24 位地址的寻址上限，**不需要** 4 字节地址模式
 *        (W25Q256 及以上才需要)，当前指令集可以直接用。 */
#ifndef SPI_NOR_FLASH_SIZE
#define SPI_NOR_FLASH_SIZE      (16u * 1024u * 1024u)
#endif

/** @brief 最小擦除粒度（4KB 扇区）—— 也是 FAL 的 blk_size */
#define SPI_NOR_SECTOR_SIZE     (4u * 1024u)

/** @brief 大块擦除粒度（64KB）—— 驱动在偏移与长度都够时自动选用 */
#define SPI_NOR_BLOCK_SIZE      (64u * 1024u)

/** @brief 页编程粒度（W25Qxx 一页 256B） */
#define SPI_NOR_PAGE_SIZE       256u

/* ================================================================
 *  分区几何
 * ================================================================ */

#define PART_PARAM_BOOT_SIZE    (4u * 1024u)
#define PART_PARAM_BANK_SIZE    (64u * 1024u)
#define PART_RAW_SIZE           (64u * 1024u)
#define PART_LFS_SIZE           (2u * 1024u * 1024u)

/** @name 分区起始偏移 */
/** @{ */
#define PART_PARAM_BOOT_OFFSET  0x000000u
#define PART_PARAM_FACTORY_OFFSET 0x010000u
#define PART_PARAM_USER0_OFFSET 0x020000u
#define PART_PARAM_USER1_OFFSET 0x030000u
#define PART_PARAM_USER2_OFFSET 0x040000u
#define PART_PARAM_USER3_OFFSET 0x050000u
#define PART_RAW0_OFFSET        0x060000u
#define PART_RAW1_OFFSET        0x070000u
#define PART_LFS_OFFSET         0x080000u
/** @} */

/* VFS 分区名（FAL_PART_TABLE 与 storage_config.c 的 s_parts 共用，避免双写漂移） */
#define FAL_PART_RAW0_NAME      "raw0"
#define FAL_PART_RAW1_NAME      "raw1"
#define FAL_PART_LFS_NAME       "lfs"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* 布局不变量：参数区 / rawflash / littlefs 不得重叠，且不得越界 */
_Static_assert(PART_RAW0_OFFSET == PART_PARAM_USER3_OFFSET + PART_PARAM_BANK_SIZE,
               "raw0 必须紧接 param_user3");
_Static_assert(PART_LFS_OFFSET == PART_RAW1_OFFSET + PART_RAW_SIZE,
               "lfs 必须紧接 raw1");
_Static_assert(PART_LFS_OFFSET + PART_LFS_SIZE <= SPI_NOR_FLASH_SIZE,
               "littlefs 超出 SPI NOR 容量");
_Static_assert((PART_PARAM_BOOT_SIZE % SPI_NOR_SECTOR_SIZE) == 0u,
               "param_boot 必须扇区对齐");
_Static_assert((PART_PARAM_BANK_SIZE % SPI_NOR_BLOCK_SIZE) == 0u,
               "param 分区必须 64KB 对齐，才能走块擦除");
#endif

extern const struct fal_flash_dev spi_nor_flash;

#define FAL_FLASH_DEV_TABLE   \
    {                         \
        &spi_nor_flash,       \
    }

#define FAL_PART_TABLE                                                                                     \
    {                                                                                                      \
        {FAL_PART_MAGIC_WORD, "param_boot",    SPI_NOR_FLASH_DEV_NAME, PART_PARAM_BOOT_OFFSET,    PART_PARAM_BOOT_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, "param_factory", SPI_NOR_FLASH_DEV_NAME, PART_PARAM_FACTORY_OFFSET, PART_PARAM_BANK_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, "param_user0",   SPI_NOR_FLASH_DEV_NAME, PART_PARAM_USER0_OFFSET,   PART_PARAM_BANK_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, "param_user1",   SPI_NOR_FLASH_DEV_NAME, PART_PARAM_USER1_OFFSET,   PART_PARAM_BANK_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, "param_user2",   SPI_NOR_FLASH_DEV_NAME, PART_PARAM_USER2_OFFSET,   PART_PARAM_BANK_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, "param_user3",   SPI_NOR_FLASH_DEV_NAME, PART_PARAM_USER3_OFFSET,   PART_PARAM_BANK_SIZE, 0}, \
        {FAL_PART_MAGIC_WORD, FAL_PART_RAW0_NAME, SPI_NOR_FLASH_DEV_NAME, PART_RAW0_OFFSET,      PART_RAW_SIZE, 0},        \
        {FAL_PART_MAGIC_WORD, FAL_PART_RAW1_NAME, SPI_NOR_FLASH_DEV_NAME, PART_RAW1_OFFSET,      PART_RAW_SIZE, 0},        \
        {FAL_PART_MAGIC_WORD, FAL_PART_LFS_NAME,  SPI_NOR_FLASH_DEV_NAME, PART_LFS_OFFSET,       PART_LFS_SIZE, 0},        \
    }

#endif /* _FAL_CFG_H_ */
