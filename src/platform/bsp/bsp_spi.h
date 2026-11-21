/**
 * @file bsp_spi.h
 * @brief 板级 SPI NOR 访问层 —— 复用 CubeMX 生成的 SPI2（hspi2）
 *
 * @par 分层位置与边界
 * 本文件属于 **platform/bsp**，只依赖 STM32 HAL，**不得** include 任何
 * `core/` 或 `app/` 头。上层（FAL 的 spi_nor 驱动）只认
 * `bsp_spi_init` / `bsp_spi_cs` / `bsp_spi_xfer` 三个函数。
 *
 * @par 为什么不再自己配 SPI 外设
 * 前一版为了绕开 CubeMX 重新生成的风险，把 SPI1 的时钟/GPIO/句柄全在本层
 * 自建。但板上外部 NOR 实际接的是 **SPI2**，而 CubeMX 现在已经在生成
 * `MX_SPI2_Init()` + `HAL_SPI_MspInit`（时钟、PB13/PB14/PB15 复用配置都齐了）：
 *   PB13 = SPI2_SCK
 *   PB14 = SPI2_MISO
 *   PB15 = SPI2_MOSI
 *   hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2  → APB1 36MHz / 2 = 18MHz
 * 两套初始化并存只会互相打架（二次 HAL_SPI_Init 覆盖句柄、重复配 GPIO），
 * 所以本层改为**只借用** `hspi2`，外设初始化彻底交给 CubeMX。
 * 这样重新生成代码后也不会丢 —— 本层不再持有任何外设配置。
 *
 * @par 片选
 * SPI2 用软件 NSS，片选是普通 GPIO。**本板接在 PB12**（低有效）。
 *
 * 两种接入方式，优先用前者：
 *
 * **方式 A（推荐）**：在 CubeMX 里把 CS 脚配成 `GPIO_Output`，并给它起用户
 * 标签 `FLASH_CS`。CubeMX 会在 `main.h` 里生成 `FLASH_CS_Pin` /
 * `FLASH_CS_GPIO_Port`，并且 `MX_GPIO_Init()` 会把它初始化为高电平。
 * 本层检测到这两个宏就直接用，不再重复初始化，也不再使用下面的默认值。
 *
 * **方式 B（当前采用）**：不依赖 CubeMX，由本头文件的 `BSP_SPI_CS_PORT` /
 * `BSP_SPI_CS_PIN` 给出，`bsp_spi_init()` 自己使能端口时钟并配成推挽输出、
 * 初始高电平。
 *
 * @note 为何不把片选写进 config.h：那是“产品旋钮”（日志级别、栈尺寸、
 *       开哪个后端），而**引脚接线属于板级常量**，与 bsp_uart.c 的
 *       `BSP_COMM_UART` / `BSP_DBG_UART` 同类，所以放在 bsp 层。
 * @note CS 有效电平：多数 NOR 为低有效，见 `BSP_SPI_CS_ACTIVE_LOW`。
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  板级接线（换板子只改这一段）
 * ================================================================ */

/** @name 外部 SPI NOR 片选（软件 NSS，低有效）
 *  本板：CS = PB12。
 *  若你在 CubeMX 里把该脚配成 GPIO_Output 并命名 FLASH_CS，那么
 *  CubeMX 生成的 FLASH_CS_Pin / FLASH_CS_GPIO_Port 会自动接管，
 *  下面两个默认值随之让位（见下面的 #if 分支）。
 *  @{ */
#ifndef BSP_SPI_CS_PORT
#define BSP_SPI_CS_PORT             GPIOB
#endif
#ifndef BSP_SPI_CS_PIN
#define BSP_SPI_CS_PIN              GPIO_PIN_12
#endif
/** @} */

/* ---- 解析：CubeMX 标签优先 ---- */
#if defined(FLASH_CS_Pin) && defined(FLASH_CS_GPIO_Port)
/* 方式 A：CubeMX 已生成标签（MX_GPIO_Init 已配好输出 + 初始电平） */
#define BSP_SPI_CS_PORT_EFF         FLASH_CS_GPIO_Port
#define BSP_SPI_CS_PIN_EFF          FLASH_CS_Pin
#define BSP_SPI_CS_OWNED_BY_BSP     0
#else
/* 方式 B：用本头文件的默认值，由 bsp_spi_init() 负责初始化 */
#define BSP_SPI_CS_PORT_EFF         BSP_SPI_CS_PORT
#define BSP_SPI_CS_PIN_EFF          BSP_SPI_CS_PIN
#define BSP_SPI_CS_OWNED_BY_BSP     1
#endif

/** @brief 片选有效电平（多数 NOR 为低有效） */
#ifndef BSP_SPI_CS_ACTIVE_LOW
#define BSP_SPI_CS_ACTIVE_LOW       1
#endif

/* ================================================================
 *  接口
 * ================================================================ */

/**
 * @brief 确认 SPI2 已就绪并（必要时）初始化片选
 *
 * @return 0 成功; -1 hspi2 未被 CubeMX 初始化（MX_SPI2_Init 没调用）
 *
 * @note 本函数**不**调用 HAL_SPI_Init —— 外设归 CubeMX 管，这里只做检查。
 */
int bsp_spi_init(void);

/**
 * @brief 片选控制
 * @param level 0 = 选中(低电平有效时拉低), 非 0 = 释放(拉高)
 *
 * @note 假定总线独占（外部 NOR 是唯一从设备），未做多设备仲裁。
 */
void bsp_spi_cs(int level);

/**
 * @brief 取得/释放 SPI 总线锁
 *
 * @par 为什么需要它
 * SPI2 上只有外部 NOR 一个从设备，但**调用者可能来自多个任务**：
 *   - 参数子系统（FlashDB → FAL → 本层），由 SYS_LOCK_PARAM 保护；
 *   - 文件子系统（littlefs → VFS → FAL），由 SYS_LOCK_FILE 保护；
 *   - 日志落盘（log_file.c）走 VFS，因此能发生在**任意调用者任务**上。
 * 那两把锁是**子系统级**的，互不覆盖 SPI 总线本身 —— 两个任务同时进 flash
 * 时，CS 拉低 … 拉高 会交错，命令与数据错位。
 *
 * @par 锁的粒度：整个 FAL op，而不是单次 CS
 * 一度考虑过"每次 CS 拉低取锁"（实现最简单），但那样锁会在
 * `nor_wait_ready()` 轮询的间隙被释放，另一个任务就能插进来写。而
 * **W25Qxx 在擦除/编程 BUSY 期间会静默忽略页编程指令**（WEL 照样被清、
 * 状态看着像成功，但数据没落地）—— 这是比字节交错更难查的损坏。
 * 所以由调用方（fal_flash_spi_nor.c）在**整个 op 外围**持锁，
 * 包括跨页写入的多次页编程与擦除后的 BUSY 轮询。
 *
 * @par 代价
 * 一次 64KB 块擦约 150ms（最长 1s）、一次页编程约 1ms，这期间其他任务
 * 碰 flash 会阻塞。考虑到 flash 操作本来就不频繁（参数存盘 / 文件元数据），
 * 这个代价远比数据损坏低。
 *
 * @note 锁从 **port 适配层**取（`sys_mutex_create` / `sys_mutex_lock`），与 VFS、
 *       param 是同一套锁来源。FreeRTOS 分支下底层是优先级继承互斥量，等待语义
 *       是"等到拿到为止"：宁等不丢，不设超时（超时会让 op 在锁外继续跑，
 *       回到损坏风险）。本层**不**直接调 `xSemaphoreCreateMutex` —— 否则换 OS
 *       时这一层要跟着改，而 port 已经把这层差异吃掉了。
 * @note 本层只做总线串行化，**不**做“同一时刻只能一个 op”以外的语义承诺；
 *       跨子系统的数据一致性仍由各自的 SYS_LOCK_* 负责。
 * @note 不可重入：ops 之间不嵌套，不要在已持锁的区间里再调一个 op。
 * @note 不能在 ISR 里调用（flash 操作本来也不应出现在 ISR 中）。
 */
void bsp_spi_bus_lock(void);

/** @brief 释放 SPI 总线锁（与 bsp_spi_bus_lock 成对使用） */
void bsp_spi_bus_unlock(void);

/**
 * @brief SPI 全双工传输（阻塞）
 * @param tx  发送缓冲; NULL 表示只收（HAL 会自动发哑字节产生时钟）
 * @param rx  接收缓冲; NULL 表示只发（丢弃接收）
 * @param len 字节数
 * @return 0 成功; -1 参数非法 / 未初始化 / HAL 传输失败
 */
int bsp_spi_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */
