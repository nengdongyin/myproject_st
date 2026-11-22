/**
 * @file config.h
 * @brief 工程（产品）配置覆盖层 —— 唯一该按产品改动的配置文件
 *
 * @par 机制
 * 本文件由构建系统**强制预处理**（`-include config.h`，见 CMakeLists.txt /
 * test_build/build.rsp），因此在每个编译单元里都先于任何 `#include` 展开。
 *
 * 各模块的头文件写成这样：
 * @code
 *   #ifndef VFS_SH_CAT_MAX
 *   #define VFS_SH_CAT_MAX 4096      // 模块默认值
 *   #endif
 * @endcode
 * 于是**只要在这里定义一次，模块内的默认值就自动让位** ——
 * 模块文件一个字节都不用改，升级时可直接整体覆盖模块目录。
 *
 * @par 规则（三条）
 * 1. **只写偏离默认的项**。默认值留在模块里（那是模块的文档与自洽性）。
 * 2. **只能写 `#ifndef` 守卫的宏**。覆盖裸 `#define` 会变成宏重定义告警或静默失效；
 *    校验脚本 `tools/check_config_overlay.py` 负责事先抓出来。
 * 3. **不得覆盖"接口契约类"宏** —— 见文件末尾的禁止清单。
 *
 * @par 与 Zephyr Kconfig 的分工
 *   - Kconfig（prj.conf / *.conf）管 **Zephyr 自身**：内核、驱动、外部模块；
 *   - 本文件管 **本工程自己的模块旋钮**（core/、protocols/、platform/、app/）；
 *   - letter-shell 的旋钮走它自己的 overlay 机制（`SHELL_CFG_USER`
 *     → src/core/shell/shell_config.h），不在本文件重复。
 */

#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

/* ================================================================
 *  板级
 *
 *  波特率**不在这里** —— 它已回归 CubeMX: `.ioc` →  Connectivity → USART3 →
 *  Baud Rate = 460800 (需与上位机一致)。CubeMX 生成的
 *  `MX_USART3_UART_Init()` 是唯一来源, `bsp_uart_start()` 直接用手柄里的值。
 *  运行时若需改速, 调 bsp_uart_set_baudrate() (不必重启接收通道)。
 *
 *  背景: 这里曾有一个 TEST_BAUD_RATE, 与 app_config.h 各写一份, 导致
 *  "改一处静默失效"。按"CubeMX 能配的都归 MX"的原则删掉它。
 * ================================================================ */

/* ================================================================
 *  参数存储后端 (三选一, 由 param_manager_config.h 的 #error 互斥校验)
 *
 *  切换记录: MKV → FlashDB
 *  背景: param_manager_config.h 的模块默认值已改为
 *  PARAM_FLASHDB_ENABLE=1 / PARAM_MKV_ENABLE=0; 本文件原先仍写着
 *  PARAM_MKV_ENABLE=1, 两个宏相加 >1 就会命中 param_manager_config.h:82
 *  的互斥 #error。**后端选择只应有一个决策点** —— 就是这个文件。
 *
 *  ⚠ 两个后端的记录格式**不兼容**: 切换后旧分区里的数据读不出来,
 *    首次启动按默认值重建。若需保留旧数据, 得先在旧固件里 dump 再回灌。
 *
 *  FAL 侧要求两者相同: 分区名 param_factory / param_user0..3 与
 *  param_boot, 均由 lib/flashdb/config/fal_cfg.h 提供。
 * ================================================================ */
/* ================================================================
 *  诊断 / 日志
 *
 *  调试口开关、日志落盘、日志级别等都可在这里覆盖, 默认全部沿用模块默认:
 *    BSP_DBG_TX_ENABLE     0/1   调试口输出开关        (platform/bsp/bsp_uart.c)
 *    ELOG_OUTPUT_LVL       ELOG_LVL_* 日志级别         (lib/EasyLogger 的 elog_cfg.h)
 *    LOG_FILE_ENABLE       0/1   日志落盘 + 双文件轮换 (core/log/log_file.h)
 *    LOG_FILE_PATH         "..." 落盘路径
 *    LOG_FILE_MAX_LEN      数值  单文件上限
 * ================================================================ */

/* ================================================================
 *  资源裁剪（64KB SRAM 预算）—— 移植到 STM32F103ZET6 引入
 *
 *  原板 RP2350 是 520KB SRAM，本板只有 64KB。下面这些宏在模块里都有
 *  #ifndef 守卫（符合本文件的裁剪规则），改默认值不会动模块代码。
 *
 *  实测依据（首次全量链接后的 nm 排序，单位字节）:
 *    ucHeap(FreeRTOS 堆) 24576 | s_parser_stack 12288 | g_lfs_mnts 10208
 *    log_buf(EasyLogger) 10240 | s_init_stack 4096  | mkv map 4096
 *  首链 RAM 超 64KB 共 22568 字节，以下五项合计收回 ~27100 字节。
 * ================================================================ */

/** @brief 协议解析任务栈（字节）
 *  12288 → 8192。该任务最深的局部量是 ymodem 单帧 1029 字节。
 *  @note 单位仍是字节（Zephyr 语义），创建任务时经 APP_STACK_WORDS() 换成字。 */
#define STACK_SIZE                  4096

/** @brief 日志单行缓冲。1024 → 256
 *  缓冲输出模式下实际占用 = 本值 × 10（ELOG_BUF_OUTPUT_BUF_SIZE），
 *  所以这一行同时把 10240 的环形缓冲压到 2560。
 *  若后续要落盘完整日志行，调到 512 即可（再加 2.5KB）。 */
#define ELOG_LINE_BUF_SIZE          256

/** @brief VFS 最大挂载点数。8 → 2
 *  storage_config 的挂载表只挂 /lfs 与 /raw 两个点。
 *  注意：littlefs 的挂载槽池（约 1.2KB/槽）按本值静态分配，
 *  这是本次裁剪里收益最大的一项（~9.4KB）。 */
#define VFS_MAX_MOUNTS              2

/** @brief VFS 最大同时打开文件数。6 → 3
 *  ymodem 收发各占一个句柄，shell 的 cat 占一个，3 个够用。 */
#define VFS_MAX_OPEN_FILES          3

/* ================================================================
 *  其余模块旋钮 (示例, 默认不覆盖)
 *
 *  参数系统  (core/param_manager/config/param_manager_config.h)
 *    PARAM_HASH_SIZE / PARAM_MAX_ENTRIES / PARAM_SCHEMA_VERSION ...
 *  文件系统  (core/vfs/vfs_config.h)
 *    VFS_ENABLE_SHELL / VFS_SH_CAT_MAX / VFS_MAX_OPEN_FILES ...
 *  串口/协议 (platform/bsp/bsp_uart.c, core/file_transfer)
 *    BSP_UART_PIPE_SIZE / BSP_UART_RX_CHUNK ...
 *  应用      (src/app/main.c、src/app/init/init_thread.c)
 *    INIT_STACK_SIZE / INIT_TASK_PRIORITY / STACK_SIZE / PARSER_TASK_PRIORITY
 * ================================================================ */

/* ================================================================
 *  ⛔ 禁止覆盖 (接口契约 / 兼容性): 写进本文件会破坏升级兼容性或直接编译失败
 *
 *   - module_ids.h 的模块 ID 与模块初始化顺序 —— 注释已写"ID 永不漂移"；
 *   - MAKE_PARAM_ID / PARAM_MODULE_LIST 等编码宏 —— 改了等于换协议；
 *   - param_manager_config.h 的 PARAM_PARTITION_* (分区布局派生值)；
 *   - SHELL_CFG_USER —— letter-shell 自己的 overlay 入口；
 *   - 任何结构体成员顺序 / 枚举值 / 存储记录格式相关的常量。
 *
 *  校验脚本 tools/check_config_overlay.py 会拒绝这些名字。
 * ================================================================ */

#endif /* PROJECT_CONFIG_H */
