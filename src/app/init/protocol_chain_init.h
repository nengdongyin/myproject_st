/**
 * @file protocol_chain_init.h
 * @brief 协议链的启动装配 + 它对外暴露的运行期访问点
 *
 * @par 为什么不暴露上下文结构体
 * 链对象、imperx / camyu 的解析器实例及其收发缓存都是本模块的内部状态
 * (protocol_chain_init.c 里的 s_ctx 与静态数组)。外部只有两个诉求:
 * 把字节喂进链 (接收任务)、问链是否在跑 (shell 的 status)。
 * 所以这里只给函数 —— 谁也无法在别处改写内部字段。
 */
#pragma once

#include <stdbool.h>
#include "protocol_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建链并挂载 ymodem / imperx / camyu (启动顺序表第 4 步)
 *  @return true 成功; false 任一步失败 (内部已回滚到装配前状态) */
bool protocol_chain_init(void);

/** @brief 逆序拆卸: detach 组件 → destroy parser → destroy chain */
void protocol_chain_cleanup(void);

/** @brief 取链对象 —— 唯一的用途是交给接收任务当参数 */
protocol_chain *protocol_chain_handle(void);

/** @brief 链是否已建好且未被拆卸 (供 shell 的 status 等只读展示) */
bool protocol_chain_is_up(void);

#ifdef __cplusplus
}
#endif
