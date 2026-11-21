/**
 * @file fal_flash_spi_nor.c
 * @brief FAL flash 设备驱动 —— 外部 SPI NOR (W25Qxx 系列指令集)
 *
 * @details
 * 这是 Zephyr 版 `platform/bsp/rp2350/fal_flash_rp2350.c` 在 STM32 上的对应物。
 * 原版把 FAL 的三个 ops 直接转发给 Zephyr 的 `flash_read/flash_write/flash_erase`；
 * 本板没有 flash 控制器抽象，只有一根 SPI 总线，因此这里直接实现 W25Qxx 的
 * 指令级时序：
 *
 * | FAL op | 指令 | 粒度 |
 * | --- | --- | --- |
 * | read  | `0x03` READ DATA | 任意偏移 |
 * | write | `0x02` PAGE PROGRAM | 页内 ≤256B，跨页自动拆分 |
 * | erase | `0x20` SECTOR ERASE / `0xD8` BLOCK ERASE 64K | 4K / 64K |
 *
 * @par 几何参数的唯一来源
 * 设备名 / 容量 / 扇区 / 页大小全部来自 `lib/flashdb/config/fal_cfg.h`，
 * 本文件不重复定义 —— 分区表与设备容量必须同源，否则 FAL 的边界校验会失效。
 *
 * @par 擦除粒度的取舍
 * 每次擦除都要等 NOR 内部完成（4K 扇区约 30~50ms，64K 块约 150~1000ms）。
 * 因此 erase() 在"偏移 64K 对齐且剩余 ≥64K"时改用 64K 块擦除：参数分区是
 * 64KB 一个，整分区擦除从 16 次扇区擦（~800ms）降到 1 次块擦（~150ms）。
 *
 * @note 本驱动不做写保护 / 状态寄存器配置（BP 位），也不做坏块管理 ——
 *       SPI NOR 无坏块概念，写保护留给硬件 WP# 引脚。
 */

/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点不再逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。
 *  @note 本文件原先用 FAL 自带的 `log_e()`, 那个宏走 FAL_PRINTF -> printf,
 *        会绕开日志门面 (无级别、无 sink 选路、带 ANSI 颜色码)。改用 LOG_E 后
 *        与全工程同一个出口; 代价是 platform/bsp 多了一个对 core/log 门面的
 *        依赖 —— 日志门面属横切基础设施, 与 port.h 同类, 故接受。 */
#define LOG_TAG "nor"
#include "log_api.h"

#include <fal.h>
#include "bsp_spi.h"

/* ================================================================
 *  W25Qxx 指令与状态位
 * ================================================================ */

#define CMD_WRITE_ENABLE      0x06u
#define CMD_READ_STATUS1      0x05u
#define CMD_READ_DATA         0x03u
#define CMD_PAGE_PROGRAM      0x02u
#define CMD_SECTOR_ERASE_4K   0x20u
#define CMD_BLOCK_ERASE_64K   0xD8u
#define CMD_JEDEC_ID          0x9Fu

#define SR1_BUSY              0x01u
#define SR1_WEL               0x02u

/** @brief 轮询 BUSY 的超时（页编程/扇区擦除都远小于此值） */
#ifndef SPI_NOR_TIMEOUT_MS
#define SPI_NOR_TIMEOUT_MS    2000u
#endif

static uint8_t s_nor_ready;   /**< init 成功标志；未就绪时所有 op 直接失败 */

/* ================================================================
 *  低层时序
 * ================================================================ */

/** @brief 读状态寄存器 1 */
static int nor_read_sr1(uint8_t *sr)
{
    uint8_t cmd = CMD_READ_STATUS1;
    int rc;

    bsp_spi_cs(1);
    rc = bsp_spi_xfer(&cmd, NULL, 1);
    if (rc == 0)
    {
        rc = bsp_spi_xfer(NULL, sr, 1);
    }
    bsp_spi_cs(0);
    return rc;
}

/** @brief 轮询 BUSY 直到空闲 */
static int nor_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t sr = 0;

    for (;;)
    {
        if (nor_read_sr1(&sr) != 0)
        {
            return -1;
        }
        if ((sr & SR1_BUSY) == 0u)
        {
            return 0;
        }
        if ((HAL_GetTick() - start) > timeout_ms)
        {
            return -1;
        }
    }
}

/** @brief 写使能并确认 WEL 已置位（不确认就编程会静默失败） */
static int nor_write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    uint8_t sr = 0;
    int rc;

    bsp_spi_cs(1);
    rc = bsp_spi_xfer(&cmd, NULL, 1);
    bsp_spi_cs(0);
    if (rc != 0)
    {
        return -1;
    }

    if (nor_read_sr1(&sr) != 0)
    {
        return -1;
    }
    return ((sr & SR1_WEL) != 0u) ? 0 : -1;
}

/** @brief 组 4 字节指令头（指令 + 24 位地址） */
static void nor_build_addr_cmd(uint8_t *hdr, uint8_t cmd, uint32_t addr)
{
    hdr[0] = cmd;
    hdr[1] = (uint8_t)((addr >> 16) & 0xFFu);
    hdr[2] = (uint8_t)((addr >> 8) & 0xFFu);
    hdr[3] = (uint8_t)(addr & 0xFFu);
}

/* ================================================================
 *  FAL ops
 * ================================================================ */

static int spi_nor_init(void)
{
    uint8_t cmd = CMD_JEDEC_ID;
    uint8_t id[3] = {0};
    int rc;

    if (bsp_spi_init() != 0)
    {
        LOG_E("bus init failed");
        return -1;
    }

    /* 整个探测流程持总线锁。init 期通常单线程，但保持一致：以后把 FAL
       初始化挪到并发环境时不会漏锁。 */
    bsp_spi_bus_lock();

    /* 上电/复位后可能仍在内部操作中，先等空闲再读 ID */
    (void)nor_wait_ready(SPI_NOR_TIMEOUT_MS);

    bsp_spi_cs(1);
    rc = bsp_spi_xfer(&cmd, NULL, 1);
    if (rc == 0)
    {
        rc = bsp_spi_xfer(NULL, id, sizeof(id));
    }
    bsp_spi_cs(0);

    if (rc != 0 || (id[0] == 0x00u) || (id[0] == 0xFFu))
    {
        LOG_E("JEDEC ID read failed (cs=%02x %02x %02x)",
              id[0], id[1], id[2]);
        bsp_spi_bus_unlock();
        return -1;
    }

    /* id[2] = 容量指数：容量 = 2^id[2] 字节 */
    log_i("spi nor: JEDEC %02x %02x %02x, capacity = %lu bytes",
          id[0], id[1], id[2],
          (unsigned long)((id[2] < 32u) ? (1UL << id[2]) : 0UL));

    if (((size_t)1 << id[2]) < SPI_NOR_FLASH_SIZE)
    {
        LOG_E("chip smaller than configured partition area");
        bsp_spi_bus_unlock();
        return -1;
    }

    s_nor_ready = 1;
    bsp_spi_bus_unlock();
    return 0;
}

static int spi_nor_read(long offset, uint8_t *buf, size_t size)
{
    uint8_t hdr[4];
    int rc;

    if (!s_nor_ready)
    {
        return -1;
    }
    if (buf == NULL || size == 0)
    {
        return 0;
    }
    if (offset < 0 || ((size_t)offset + size) > SPI_NOR_FLASH_SIZE)
    {
        return -1;
    }

    /* 读也持锁：单次读不会损坏数据，但若不持锁，一次“读状态寄存器”
       就可能插到别人擦除的中间，把 BUSY 轮询结果读错（把忙读成空闲，
       于是提前进入下一次页编程，而该页编程会被器件静默忽略）。 */
    bsp_spi_bus_lock();

    nor_build_addr_cmd(hdr, CMD_READ_DATA, (uint32_t)offset);

    bsp_spi_cs(1);
    rc = bsp_spi_xfer(hdr, NULL, sizeof(hdr));
    if (rc == 0)
    {
        rc = bsp_spi_xfer(NULL, buf, (uint32_t)size);
    }
    bsp_spi_cs(0);

    bsp_spi_bus_unlock();
    return (rc == 0) ? (int)size : -1;
}

static int spi_nor_write(long offset, const uint8_t *buf, size_t size)
{
    size_t written = 0;
    int result = 0;

    if (!s_nor_ready)
    {
        return -1;
    }
    if (buf == NULL || size == 0)
    {
        return 0;
    }
    if (offset < 0 || ((size_t)offset + size) > SPI_NOR_FLASH_SIZE)
    {
        return -1;
    }

    /* 整个写操作（跨页的多次页编程 + 每次的 BUSY 轮询）持锁。
       不能只锁单次 CS —— 轮询间隙一旦放锁，别人插进来的页编程会在
       BUSY 期间被器件静默忽略，数据不落地却不报错。 */
    bsp_spi_bus_lock();

    while (written < size)
    {
        uint8_t hdr[4];
        size_t page_left = SPI_NOR_PAGE_SIZE - (((size_t)offset + written) % SPI_NOR_PAGE_SIZE);
        size_t chunk = (size - written < page_left) ? (size - written) : page_left;
        int rc;

        if (nor_write_enable() != 0)
        {
            result = -1;
            break;
        }

        nor_build_addr_cmd(hdr, CMD_PAGE_PROGRAM, (uint32_t)(offset + written));

        bsp_spi_cs(1);
        rc = bsp_spi_xfer(hdr, NULL, sizeof(hdr));
        if (rc == 0)
        {
            rc = bsp_spi_xfer(buf + written, NULL, (uint32_t)chunk);
        }
        bsp_spi_cs(0);
        if (rc != 0)
        {
            result = -1;
            break;
        }

        if (nor_wait_ready(SPI_NOR_TIMEOUT_MS) != 0)
        {
            result = -1;
            break;
        }

        written += chunk;
    }

    bsp_spi_bus_unlock();
    return (result == 0) ? (int)written : -1;
}

static int spi_nor_erase(long offset, size_t size)
{
    int result = 0;

    if (!s_nor_ready)
    {
        return -1;
    }
    if (size == 0)
    {
        return 0;
    }
    if (offset < 0 || ((size_t)offset + size) > SPI_NOR_FLASH_SIZE)
    {
        return -1;
    }
    if (((uint32_t)offset % SPI_NOR_SECTOR_SIZE) != 0u)
    {
        return -1; /* 擦除必须扇区对齐 */
    }

    /* 整个擦除（可能跨多个扇区/块 + 每次的 BUSY 轮询）持锁。
       一次 64KB 块擦约 150ms，这期间其他任务碰 flash 会阻塞 —— 这是
       有意的，因为放锁让别人在擦除中途写会丢数据。 */
    bsp_spi_bus_lock();

    while (size > 0u)
    {
        uint8_t hdr[4];
        size_t gran;
        uint8_t cmd;
        int rc;

        if (size >= SPI_NOR_BLOCK_SIZE &&
            (((uint32_t)offset % SPI_NOR_BLOCK_SIZE) == 0u))
        {
            gran = SPI_NOR_BLOCK_SIZE;
            cmd = CMD_BLOCK_ERASE_64K;
        }
        else
        {
            gran = SPI_NOR_SECTOR_SIZE;
            cmd = CMD_SECTOR_ERASE_4K;
        }

        if (nor_write_enable() != 0)
        {
            result = -1;
            break;
        }

        nor_build_addr_cmd(hdr, cmd, (uint32_t)offset);

        bsp_spi_cs(1);
        rc = bsp_spi_xfer(hdr, NULL, sizeof(hdr));
        bsp_spi_cs(0);
        if (rc != 0)
        {
            result = -1;
            break;
        }

        if (nor_wait_ready(SPI_NOR_TIMEOUT_MS) != 0)
        {
            result = -1;
            break;
        }

        offset += (long)gran;
        size = (size > gran) ? (size - gran) : 0u;
    }

    bsp_spi_bus_unlock();
    return result;
}

/* ================================================================
 *  FAL 设备注册
 * ================================================================ */

const struct fal_flash_dev spi_nor_flash = {
    .name = SPI_NOR_FLASH_DEV_NAME,
    .addr = 0,                          /* FAL 的分区 offset 直接就是本设备内的偏移 */
    .len = SPI_NOR_FLASH_SIZE,
    .blk_size = SPI_NOR_SECTOR_SIZE,
    .ops = {spi_nor_init, spi_nor_read, spi_nor_write, spi_nor_erase},
    .write_gran = 1,                    /* NOR 可按字节写（驱动内部按页拆分） */
};
