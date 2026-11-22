/**
 * @file filesystem_init.h
 * @brief 文件系统启动装配 —— 挂载全部卷, 并把逐卷真实结果回显到启动日志
 *
 * @par 与 core/vfs/storage 的分工
 * `storage_config_init()` (core) 是**装配实现**: 按 s_parts / s_mounts 两张
 * 静态表注册 bdev、注册文件系统类型、逐个挂载点挂载。
 * 本文件是**应用层包装**: 在启动顺序表里占一步, 把结果渲染成 `[BOOT 5/7]`。
 *
 * @note 本模块不依赖 app 层的任何上下文: 它既不属于协议链, 也不持有任何
 *       应用级共享状态 —— 输入只有 core 的存储表, 输出只有成功/失败与日志。
 */
#ifndef FILESYSTEM_INIT_H
#define FILESYSTEM_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 挂载全部存储卷 (启动顺序表第 5 步)
 *  @return true 装配流程跑完 (逐卷结果见启动日志); false core 层装配失败 */
bool filesystem_init(void);

/** @brief 卸载文件系统层 (与 filesystem_init 对称) */
void filesystem_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* FILESYSTEM_INIT_H */
