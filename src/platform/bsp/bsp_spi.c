/**
 * @file bsp_spi.c
 * @brief 板级 SPI NOR 访问层 —— 借用 CubeMX 的 SPI2 + 软件片选
 *
 * @details
 * 见 bsp_spi.h 的分层说明。本文件只做三件事：确认外设就绪、控片选、搬字节。
 * 所有 W25Qxx 指令级逻辑都在 fal_flash_spi_nor.c，本层不认识"Flash"。
 *
 * @note 阻塞式传输而不是 DMA：外部 NOR 的读写量以 KB 计且不频繁（参数区 /
 *       littlefs 元数据），用阻塞换掉一整条 DMA 通道与中断链更划算。
 *       真要跑大数据流再换 DMA，替换点只有 bsp_spi_xfer。
 */

#include "bsp_spi.h"

#include "port.h" /* sys_mutex_create/lock/unlock —— 锁统一从平台适配层取 */

/* 句柄由 CubeMX 在 Core/Src/main.c 里定义（SPI_HandleTypeDef hspi2;），
   此处只做外部声明，避免为了一个 extern 去改 CubeMX 管理的 main.h */
extern SPI_HandleTypeDef hspi2;

static uint8_t s_initialized;
static sys_mutex_t *s_bus_lock;   /**< 总线互斥，句柄来自 port 的互斥池（FreeRTOS 分支底层仍是
                                       优先级继承的 xSemaphoreCreateMutex） */

/* ================================================================
 *  片选初始化（仅方式 B 需要：CubeMX 不知道这个脚）
 * ================================================================ */

#if (BSP_SPI_CS_OWNED_BY_BSP == 1)
static void bsp_spi_cs_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 端口时钟：CubeMX 的 SPI2 MspInit 已使能 GPIOB，但 CS 可能在别的端口，
       这里按 CS 所属端口再使能一次（重复使能无副作用） */
#if defined(GPIOA)
    if (BSP_SPI_CS_PORT_EFF == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
#endif
#if defined(GPIOB)
    if (BSP_SPI_CS_PORT_EFF == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
#endif
#if defined(GPIOC)
    if (BSP_SPI_CS_PORT_EFF == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
#endif
#if defined(GPIOD)
    if (BSP_SPI_CS_PORT_EFF == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
#endif
#if defined(GPIOE)
    if (BSP_SPI_CS_PORT_EFF == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
#endif

    /* 先写高电平再配成输出，避免配置瞬间误选中 NOR */
    HAL_GPIO_WritePin(BSP_SPI_CS_PORT_EFF, BSP_SPI_CS_PIN_EFF,
                      BSP_SPI_CS_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET);

    gpio.Pin = BSP_SPI_CS_PIN_EFF;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_SPI_CS_PORT_EFF, &gpio);
}
#endif /* BSP_SPI_CS_OWNED_BY_BSP */

/* ================================================================
 *  初始化与片选
 * ================================================================ */

int bsp_spi_init(void)
{
    if (s_initialized)
    {
        return 0;
    }

    /* CubeMX 的 MX_SPI2_Init() 在 main() 里、调度器启动前就执行完了，
       正常情况下 Instance 一定非空。这里只是把"忘配 SPI2"变成明确报错，
       而不是让后面 HAL_SPI_Transmit 卡在超时里。 */
    if (hspi2.Instance != SPI2)
    {
        return -1;
    }

#if (BSP_SPI_CS_OWNED_BY_BSP == 1)
    bsp_spi_cs_gpio_init();
#endif

    /* 总线锁：一次性创建。用约 80B，换取“多任务同时碰 flash 不会把
       命令/数据写花”的保证，见 bsp_spi.h 的长注释。

       @note 句柄来自 port 层的 sys_mutex_create()，与 VFS / param 用的是
       同一套锁来源（SYS_MUTEX_POOL_SIZE 静态池）。不在本文件直接调
       xSemaphoreCreateMutex —— 否则 bsp 会捆绑 FreeRTOS，换 OS 时本层
       要跟着改，而 port 层已经把这层差异吃掉了。
       @note sys_mutex_create() 池满/堆不足时返回 NULL，此时直接报错 ——
       宁愿存储子系统初始化失败（会打 log_e），也不能无锁跑下去。 */
    if (s_bus_lock == NULL)
    {
        s_bus_lock = sys_mutex_create();
        if (s_bus_lock == NULL)
        {
            return -1;
        }
    }

    s_initialized = 1;
    return 0;
}

void bsp_spi_bus_lock(void)
{
    /* 宁等不丢：port 的阻塞语义是“等到拿到为止”，给超时反而会在没拿到锁
       时继续在锁外操作 flash，回到数据损坏的路径上。FreeRTOS 分支的底层是
       优先级继承互斥量，不会造成无界优先级反转。
       @note sys_mutex_lock() 对 NULL 安全（未 init 时是空操作），
       配合下面的 s_initialized 检查构成双保险。 */
    sys_mutex_lock(s_bus_lock);
}

void bsp_spi_bus_unlock(void)
{
    sys_mutex_unlock(s_bus_lock);
}

void bsp_spi_cs(int level)
{
#if BSP_SPI_CS_ACTIVE_LOW
    GPIO_PinState st = level ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
    GPIO_PinState st = level ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
    HAL_GPIO_WritePin(BSP_SPI_CS_PORT_EFF, BSP_SPI_CS_PIN_EFF, st);
}

/* ================================================================
 *  传输
 * ================================================================ */

int bsp_spi_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t done = 0;

    if (!s_initialized)
    {
        return -1;
    }
    if (len == 0u)
    {
        return 0;
    }
    if (tx == NULL && rx == NULL)
    {
        return -1;
    }

    /* HAL 的 Size 参数是 uint16_t，分块避免超过 64KB 时被截断。
       三个分支各用 HAL 对应方向的 API：
         - 双向 → TransmitReceive
         - 纯发 → Transmit   (命令字节 / 地址 / 页编程数据)
         - 纯收 → Receive    (HAL 在主机 2 线模式下会自动发哑字节产生时钟) */
    while (done < len)
    {
        uint32_t remain = len - done;
        uint16_t chunk = (remain > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)remain;
        HAL_StatusTypeDef st;

        if (tx != NULL && rx != NULL)
        {
            st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)(tx + done),
                                         (uint8_t *)(rx + done), chunk, 1000u);
        }
        else if (tx != NULL)
        {
            st = HAL_SPI_Transmit(&hspi2, (uint8_t *)(tx + done), chunk, 1000u);
        }
        else
        {
            st = HAL_SPI_Receive(&hspi2, (uint8_t *)(rx + done), chunk, 1000u);
        }

        if (st != HAL_OK)
        {
            return -1;
        }

        done += chunk;
    }

    return 0;
}
