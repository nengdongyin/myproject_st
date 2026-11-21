/** @brief 本模块日志标签 (LOG_* 宏自动带上, 调用点不再逐条写)
 *  @note 必须定在 `#include "log_api.h"` 之前。 */
#define LOG_TAG "fs"

#include "file_storage_init.h"
#include "storage_config.h"
#include "boot_stage.h" /* 启动阶段日志: [BOOT 5/7] 前缀 */
#include "log_api.h" /* 日志门面: LOG_I/W/E + tag */

/**
 * @brief 初始化并挂载所有存储卷
 *
 * 只做"存储资源就绪"。ymodem 收发的文件句柄由 file_transfer 组件在
 * 每次传输时按需 open/close, 这里不再预置任何 fd 或默认文件名 ——
 * 传输目标由参数 PID_FT_RECV_PATH / PID_FT_SEND_PATH 决定。
 */
bool file_storage_init(app_ctx_t *ctx)
{
    (void)ctx;

    int rc = storage_config_init();
    if (rc != 0)
    {
        boot_stage_fail(BOOT_STEP_FS, "fs: storage_config_init failed: %d", rc);
        return false;
    }

    /* 回显**真实**的装配结果，而不是“应该有 /lfs”这种硬编码文案。
       数据来自 storage_config 的 s_parts / s_mounts 两张静态表经实际
       挂载后的逐项结果 (storage_config_mount_report) —— 表是单一数据源,
       这里只负责排版。 */
    storage_bdev_report_t bdevs;
    storage_mount_report_t mounts[STORAGE_MOUNT_REPORT_MAX];
    size_t n = storage_config_mount_report(mounts, STORAGE_MOUNT_REPORT_MAX, &bdevs);

    boot_stage(0, "fs: bdev %u/%u registered%s",
               (unsigned)bdevs.registered, (unsigned)bdevs.configured,
               (bdevs.skipped != 0u) ? " (optional media absent)" : "");

    unsigned ok = 0;
    for (size_t i = 0; i < n; i++)
    {
        const storage_mount_report_t *m = &mounts[i];

        if (m->skipped)
        {
            /* 可选介质缺失（如未插 SD）—— 不是错误，但要能看见被跳过 */
            boot_stage_warn(0, "fs: mount %s skipped (no block device)",
                            m->mnt_point);
        }
        else if (m->rc == 0)
        {
            ok++;
            boot_stage(0, "fs: mount %s (%s) ok", m->mnt_point, m->fstype);
        }
        else
        {
            boot_stage_fail(0, "fs: mount %s (%s) failed: %d",
                            m->mnt_point, m->fstype, m->rc);
        }
    }

    boot_stage(BOOT_STEP_FS, "fs: %u/%u volumes mounted", ok, (unsigned)n);
    return true;
}

void file_storage_deinit(app_ctx_t *ctx)
{
    (void)ctx;
    storage_config_deinit();
}
