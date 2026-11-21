/**
 * @file storage_config.h
 * @brief 存储资源定义入口 —— 设备 / 分区 / 挂载点集中于此（需求7）
 *
 * 该模块只做"资源 + 装配"，不含文件操作逻辑：定义存储器、分区几何、以及
 * 文件系统类型到挂载点的映射。换板子/改分区只动 storage_config.c。
 */

#ifndef STORAGE_CONFIG_H
#define STORAGE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief 文件挂载点（littlefs，承载 ymodem 收发文件） */
#define STORAGE_MNT_FILES "/lfs"
/** @brief 裸 flash 挂载点（静态注册伪 FS） */
#define STORAGE_MNT_RAW "/raw"
/** @brief SD 卡挂载点（FatFs） */
#define STORAGE_MNT_SD "/sd"

/* ================================================================
 *  装配结果回显（供启动日志读取）
 *
 *  @par 为什么在这里而不在 app 层
 *  “挂了哪些卷 / 用了哪个分区”的**真相**只在 s_parts / s_mounts 两张表里。
 *  app 层若自己再抄一份表就知道错了；因此本模块把**事实**以只读报告
 *  的形式吐出来，**打印与否由 app 层决定** —— storage/vfs 里一行日志都不写,
 *  保持“core 是库，不认识 log_api”的分层纪律。
 * ================================================================ */

/** @brief 报告数组容量：本表最大条目数（s_mounts 的当前规模，含可选 SD） */
#ifndef STORAGE_MOUNT_REPORT_MAX
#define STORAGE_MOUNT_REPORT_MAX 4
#endif

/** @brief 单个挂载点的装配结果 */
typedef struct
{
    const char *mnt_point; /**< "/lfs" / "/raw" / "/sd" */
    const char *bdev_name; /**< 绑定的块设备名；NULL = 无块设备的卷（rawflash 静态注册） */
    const char *fstype;    /**< "littlefs" / "rawflash" / "fatfs" / "unknown" */
    int         rc;        /**< 0 = 挂载成功；< 0 = 负 errno；skipped 时无意义 */
    bool        skipped;   /**< true = 介质缺失/MOUNT 未注册，本次启动跳过 */
} storage_mount_report_t;

/** @brief 块设备（分区切片）注册结果 */
typedef struct
{
    size_t registered; /**< 实际注册的 bdev 数量 */
    size_t configured; /**< 分区几何表条目数（含可选介质） */
    size_t skipped;    /**< 因可选介质缺失而跳过的分区数 */
} storage_bdev_report_t;

/**
 * @brief 初始化并挂载所有存储卷
 * @return 0 成功，< 0 负 errno（挂载失败）
 */
int storage_config_init(void);

/** @brief 卸载所有卷（可选） */
void storage_config_deinit(void);

/**
 * @brief 读取上次 storage_config_init() 的装配结果 —— 只读，不改 VFS 状态
 * @param[out] mounts 接收挂载点结果的数组（可为 NULL = 只要计数）
 * @param[in]  max    mounts 容量（建议 STORAGE_MOUNT_REPORT_MAX）
 * @param[out] bdevs  接收块设备注册统计（可为 NULL）
 * @return 写入 mounts 的条目数；0 = 初始化未跑或直接失败
 * @note 必须在 storage_config_init() 之后调用；本接口不触发任何重试/挂载。
 */
size_t storage_config_mount_report(storage_mount_report_t *mounts, size_t max,
                                   storage_bdev_report_t *bdevs);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_CONFIG_H */
