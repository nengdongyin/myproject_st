#include "imperx_cmd_map.h"
#include "sensor_module.h"
#include "module_ids.h"
#include "param_service.h"     /* PID_SCHEMA_VER / PID_SYS_USERSET_* */
#include "file_transfer.h"
#include "console_mode.h"

/* ================================================================
 *  地址空间划分（新增映射请遵守, 避免与既有地址撞车）
 *
 *   0x0000          框架元数据     MODULE_META  存储格式版本号（只读）
 *   0x0010-0x00FF   IP 寄存器直映  MODULE_ID 0x80..0xFF，地址 = 寄存器地址
 *   0x0100-0x01FF   系统 / 用户集  MODULE_SYS，  地址 = 0x0100 + local_id
 *   0xF000-0xF0FF   命令类         FT 收发 / 串口模式切换（EXEC）
 * ================================================================ */

static const cmd_map_entry_t g_cmd_map[] = {
    { 0x0010, PID_IP_SENSOR_EXPOSURE },
    { 0x0012, PID_IP_SENSOR_GAIN },
    { 0x0014, PID_IP_SENSOR_FPS },
    { 0x0016, PID_IP_SENSOR_RESOLUTION },
    { 0x0018, PID_IP_SENSOR_ROI_X },

    /* ---- 框架元数据 (MODULE_META) ----
     * 只读: 读回存储格式版本号。flash 里的参数库版本不匹配时启动会触发
     * 迁移/erase，现场排查"参数怎么全没了"时这条最省事。 */
    { 0x0000, PID_SCHEMA_VER },

    /* ---- 用户集控制 (MODULE_SYS, GenICam SFNC §16 UserSetControl) ----
     * 地址 = 0x0100 + local_id，与 param_service.h 的 local_id 一一对应；
     * 只读/只写/拒出厂分区这些语义细节在 param_service.h，这里只做地址映射。
     *
     *   WRITE 0x0100 = 选中 set (0=出厂/Default, 1..3=UserSetN)
     *                  —— 只切落盘目标，不读内容；随后的 SAVE 会写进新 set
     *   WRITE 0x0101 = Load  : 选中 set -> 活动值（也可当"丢弃未保存修改"用）
     *   WRITE 0x0102 = Save  : 活动值 -> 选中 set（出厂分区拒写 -> READONLY）
     *   WRITE 0x0103 = 上电加载哪个 set（写存储元数据，下次启动生效）
     *   WRITE 0x0104 = Reset : 清空选中 set 的存储（不动活动值）
     *   WRITE 0x0105 = DefaultSave  : 活动值 -> 出厂分区（厂商扩展，产线用）
     *   WRITE 0x0106 = DefaultDelete: 清空出厂分区（厂商扩展，售后用）
     *
     * EXEC 参数 (0x0101/0x0102/0x0104/0x0105/0x0106) 的 4 字节负载
     * 会被忽略 —— 帧长固定 7 字节，填 00 00 00 00 即可。
     *
     * @note Save/Load/Reset 会读写/擦除外部 NOR（百 ms 量级），回调在协议
     *       接收线程里同步执行 —— 这期间该线程不收新字节，主机要等 ACK。
     * @note SELECTOR 只切驱动不造数据：选中一个从未存过的 set 后直接
     *       Save 才会创建记录，Load 一个空 set 得到的是各参数默认值。 */
    { 0x0100, PID_SYS_USERSET_SELECTOR },
    { 0x0101, PID_SYS_USERSET_LOAD },
    { 0x0102, PID_SYS_USERSET_SAVE },
    { 0x0103, PID_SYS_USERSET_DEFAULT },
    { 0x0104, PID_SYS_USERSET_RESET },
    { 0x0105, PID_SYS_USERSET_DEFAULT_SAVE },
    { 0x0106, PID_SYS_USERSET_DEFAULT_DELETE },

    /* 文件传输命令 —— 写这两个地址即触发对应 EXEC 参数:
     *   WRITE 0xF000 → PID_FT_RECV_START (启动 ymodem 接收)
     *   WRITE 0xF001 → PID_FT_SEND_START (启动 ymodem 发送)
     *
     * param_write_raw 对 EXEC 参数自动路由到模块的 exec 回调,
     * 因此协议层不需要任何专用命令通道 —— "旋钮即参数"。 */
    { 0xF000, PID_FT_RECV_START },
    { 0xF001, PID_FT_SEND_START },

    /* 通信串口复用 —— 写这个地址即把 uart1 交给 shell:
     *   WRITE 0xF003 → shell 模式 (交互式命令行, 应急调试)
     * 与上面两条一样走 param_write_raw → EXEC 回调, 不需要专用命令通道。
     * 回程只有一条路: shell 内敲 `uart_mode proto` (切到 shell 后协议帧
     * 已不被解析, 再开一个"切回协议"的地址也接不到)。
     * 详见 core/console/console_mode.h。 */
    { 0xF003, PID_CONSOLE_ENTER_SHELL },
};

static const size_t g_cmd_map_count =
    sizeof(g_cmd_map) / sizeof(g_cmd_map[0]);

const cmd_map_entry_t *cmd_map_lookup(uint16_t addr)
{
    for (size_t i = 0; i < g_cmd_map_count; i++) {
        if (g_cmd_map[i].addr == addr)
            return &g_cmd_map[i];
    }
    return NULL;
}
