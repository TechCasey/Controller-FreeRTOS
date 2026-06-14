/**
 * @file    upgrade.c
 * @brief   OTA 固件升级协议实现
 *
 * @details 车载控制器通过 CAN 总线对从机（RCM/排种器）进行 OTA 在线升级。
 *          完整流程：
 *          1. 上位机发送升级指令 → 主机响应 permit/refuse
 *          2. 从机进入 BootLoader 模式，主机等待版本号
 *          3. 主机读取 W25QXX Flash 中的固件文件
 *          4. 分帧发送（每帧 512 字节），等待每帧 ACK
 *          5. 最后一帧发送后，从机执行 Flash 搬运（写入用户区）
 *          6. 完成后从机回复升级成功/失败
 *
 * @note    关键机制：
 *          - 环形缓冲区 s_RecvCB：缓存 CAN 总线接收的数据
 *          - 广播模式 BroadcastMode：同时升级多台从机
 *          - 帧索引确认机制：need_index 跟踪期望的下一帧序号
 *          - 超时重试：ERR_CNT=4 次，WAIT_TIME=120ms
 *          - CRC 校验：计算整包数据的校验和，存入 Aux_temp_buff
 *
 * @note    注释格式说明：
 *          /* 【固定区】这段代码是固定的，尽量默认使用，不要改动 */
 *          /* 【自定义区】核心业务逻辑可在这里自定义添加/修改 */
 *          /* 【运行时步骤 1/2/3...】程序运行时首先运行什么→其次运行什么→最后运行什么 */
 *          /* 【搭建步骤 1/2/3...】搭建时首先做什么→其次做什么→最后做什么 */
 */

#include "upgrade.h"

/* ============================================================
 * 【固定区】宏定义 — 协议常量
 * ============================================================ */
#define BUF_SIZE        1040    /* 接收缓冲区大小（字节），每次写入大小1040字节 */
#define ERR_CNT         4       /* 允许超时次数 */
#define WAIT_TIME       120      /* 超时时间（ms） */

/* ============================================================
 * 【固定区】外部引用 — 文件系统（来自 W25Qxx 驱动）
 * ============================================================ */
extern lfs_t lfs_w25qxx;
extern lfs_file_t lfs_file_w25qxx;
const char *AuxCtl_file = "AuxCtl_file";   /* 文件系统存储路径 */

/* ============================================================
 * 【固定区】全局静态变量 — 升级状态跟踪
 * ============================================================ */
static uint16_t need_index = 0;                    /* 当前期望接收的数据包帧序号 */
static eUpgradeStep eupgrade_step = eInit;          /* 从机端升级流程标识 */

/* ============================================================
 * 【固定区】全局变量 — 动态缓冲区
 * ============================================================ */
uint8_t  *Aux_temp_buff = NULL;    /* 辅助临时缓冲区（512字节，存储OTA数据包） */
uint8_t  *RecvBuf = NULL;          /* 接收端点环形缓冲区 */
uint16_t total_frame = 0;          /* 升级数据包总帧数 */
uint8_t  DeviceNum = 0;            /* 已响应设备数量（广播模式） */

/* ============================================================
 * 【固定区】全局结构体实例
 * ============================================================ */
static uint8_t *peek;                                /* 窥视位指针（临时获取接收数据地址） */

CircularBufferContext_t  s_RecvCB = {0};             /* 环形缓冲区（存储TBOX下发的升级包） */
UpgradeCanMsg_t         s_UpgradeCanMsg = {0};       /* 升级流程消息结构 */
UpgradeCanMsg_t         Aux_UpgradeCanMsg = {0};     /* 辅助升级消息结构 */
upgradeTask_t           s_upgradeTask = {0};          /* 升级任务线程结构体 */
upgrade_t               s_upgrade = {0};               /* 升级数据包结构体（600字节） */
Timer_t                 s_Timer1s = {0};               /* 升级超时定时器 */
McuOta                  McuOtaArg = {0};              /* 从机口升级参数结构体 */
Broadcast               RespondDevice_Num[DEVICE_NUM] = {0};  /* 广播模式设备状态数组 */
CAN_Msg_t               msg = {0};                     /* CAN消息结构 */

/* ============================================================
 * 【自定义区】辅助结构体定义（可按需扩展）
 * ============================================================ */
typedef struct {
    uint16_t FileOffset;           /* 数据包偏移帧号 */
    uint8_t  OtaStep;              /* 主机端OTA步骤 */
    uint8_t  ack_cmd;              /* ACK响应值 */
    uint16_t need_index;          /* 第几帧数据包 */
    uint32_t Can_CMD_ID;           /* 指令CAN ID */
    uint32_t CAN_DATA_ID;         /* 数据CAN ID */
    uint8_t  DEVICE_Value;        /* 目标设备地址 0X18FFA0~0X18FFAF */
    uint8_t  McuDownloadFlag;     /* 下载文件完成标志 */
} McuOta;

/* ============================================================
 * 【固定区】环形缓冲区初始化
 * ─────────────────────────────────────────────────────────────
 * 【搭建步骤 1/2/3...】
 *   1 → 分配 BUF_SIZE(1040字节) 的内存空间（pvPortMalloc）
 *   2 → 初始化环形缓冲区结构体 s_RecvCB（CircularBufferInit）
 *   3 → 完成，缓冲区可接收 CAN 分帧数据
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → RecvBufInit() 在系统启动时被 UpgradeTask_Init() 调用一次
 *   2 → 之后 RecvBuf 在 CAN 中断中被 CircularBufferPushBack 持续写入
 *   3 → CheckPack() 从环形缓冲区读取并解析完整升级包
 * ─────────────────────────────────────────────────────────────
 */
void RecvBufInit(void)
{
    /* 【固定区】动态分配接收缓冲区（1040字节） */
    RecvBuf = (uint8_t *)pvPortMalloc(BUF_SIZE);
    /* 【固定区】初始化环形缓冲区结构体 */
    CircularBufferInit(&s_RecvCB, RecvBuf, BUF_SIZE, sizeof(uint8_t));
}

/* ============================================================
 * 【固定区】CRC16 校验计算（每隔8字节跳1字节序号）
 * ─────────────────────────────────────────────────────────────
 * 【搭建步骤 1/2/3...】
 *   1 → 遍历 upgrade.data[0..525]（526字节）
 *   2 → 每8字节的第1字节（序号）跳过不参与CRC计算
 *   3 → 有效数据复制到 Aux_temp_buff[0..511]（512字节）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → CheckPack() 检测到完整帧头后调用本函数
 *   2 → 计算512字节有效数据的CRC16值
 *   3 → 与帧中的 checksum 比较，匹配则接收成功
 * ─────────────────────────────────────────────────────────────
 * CRC算法：标准CRC16（Modbus）
 *   - 多项式：0x8408（反转形式，等价于 0x1021）
 *   - 初始值：0x0000
 *   - 每字节右移8位，根据最低位决定是否异或反转多项式
 */
uint16_t GetSkipCrc16OfRam(const upgrade_t upgrade)
{
    uint16_t CRCValue = 0;
    uint32_t SizeCnt = 0;
    uint8_t  CharCnt = 0;
    uint32_t i = 0;

    for (SizeCnt = 0; SizeCnt < (UPDATE_PACK_SIZE + UPDATE_NUM_SIZE); SizeCnt++)
    {
        /* 【固定区】每8字节的第1字节是帧序号，跳过不参与CRC计算 */
        if ((SizeCnt % 8) == 0)
        {
            /* 跳过序号字节 */
        }
        else
        {
            /* 【固定区】CRC16 异或当前字节 */
            CRCValue ^= upgrade.data[SizeCnt];

            /* 【固定区】复制有效数据到 Aux_temp_buff[] */
            Aux_temp_buff[i] = upgrade.data[SizeCnt];
            i++;

            /* 【固定区】CRC16 位运算（8位右移） */
            for (CharCnt = 0; CharCnt < 8; CharCnt++)
            {
                if (CRCValue & CRC_CHECK_MASK)
                {
                    CRCValue >>= 1;
                    CRCValue ^= CRC_CONST_CODE;
                }
                else
                {
                    CRCValue >>= 1;
                }
            }
        }
    }

    return CRCValue;
}

/* ============================================================
 * 【固定区】数据包解析：从环形缓冲区提取并校验升级数据包
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 遍历环形缓冲区，查找帧头 "SS"（0x53, 0x53）
 *   2 → 找到帧头后弹出526字节完整包，计算CRC校验
 *   3 → 校验通过返回1，失败返回2，未收完返回0
 * ─────────────────────────────────────────────────────────────
 * 数据包格式（upgrade_t，526字节）：
 *   head1='S', head2='S', index_frame, total_frame,
 *   data[520]（每8字节第1字节为序号）, checksum
 */
uint8_t CheckPack(CircularBufferContext_t *ctx, upgrade_t *pUpgrade)
{
    uint8_t dataTemp[8] = {0};

    /* 【固定区】如果上次已弹出帧头，直接返回校验结果 */
    if (pUpgrade->head1 == 'S' && pUpgrade->head2 == 'S')
    {
        uint16_t checksum = GetSkipCrc16OfRam(*pUpgrade);
        if (pUpgrade->checksum == checksum)
            return 1;  /* 校验成功 */
        else
            return 2;  /* 校验错误 */
    }

    /* 【固定区】缓冲区为空，返回0（未收完） */
    if (CircularBufferEmpty(ctx))
        return 0;

    /* 【固定区】窥视第1字节，检查是否为 'S' */
    if (CircularBufferPeek(ctx, 0, (void **)&peek) == 0)
    {
        if (*peek == 'S')
        {
            /* 【固定区】窥视第2字节，检查是否为 'S' */
            if (CircularBufferSize(ctx) > 1)
            {
                if (CircularBufferPeek(ctx, 1, (void **)&peek) == 0)
                {
                    if (*peek == 'S')
                    {
                        /* 【固定区】帧头完整 "SS"，缓冲区数据足够则弹出 */
                        if (CircularBufferSize(ctx) >= sizeof(upgrade_t))
                        {
                            CircularBufferPopFront(ctx, pUpgrade, sizeof(upgrade_t));
                        }
                    }
                    else
                    {
                        /* 【固定区】第2字节不是 'S'，丢弃前2字节 */
                        CircularBufferPopFront(ctx, dataTemp, 2);
                    }
                }
            }
        }
        else
        {
            /* 【固定区】第1字节不是 'S'，丢弃1字节（消除噪声） */
            CircularBufferPopFront(ctx, dataTemp, 1);
        }
    }

    return 0;  /* 未收完或正在解析 */
}

/* ============================================================
 * 【固定区】发送 ACK 响应
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 组装8字节ACK帧：["ACK:", ack_num, need_index高8位, need_index低8位]
 *   2 → 调用 Can0SendMsg(0x18FFC1D2, ...) 发送到CAN总线
 *   3 → 上位机/从机根据ACK状态码决定下一步操作
 * ─────────────────────────────────────────────────────────────
 * ACK状态码定义：
 *   eACK_Simple=0x00      简单确认
 *   eACK_Index=0x01       请求下一帧
 *   eACK_CheckErr=0x02    CRC校验错误，重发
 *   eACK_MoveFlash=0x06   通知执行Flash搬运
 *   eACK_UpdateSuccess=0x07 升级成功
 */
void SendACK(uint8_t ack_num, uint16_t need_index)
{
    /* 【固定区】组装ACK帧 */
    uint8_t ack[8] = "ACK:";
    ack[4] = ack_num;
    ack[5] = need_index >> 8;          /* need_index 高8位 */
    ack[6] = need_index & 0xFF;         /* need_index 低8位 */
    /* 【固定区】通过CAN0发送ACK响应（ID: 0x18FFC1D2） */
    Can0SendMsg(0x18FFC1D2, 1, ack, sizeof(ack));
}

/* ============================================================
 * 【固定区】定时器使能/停止
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → sta=true 时：启动定时器，TimerCnt清零，TimerOutFlag清零
 *   2 → 定时器回调 upgradeTimer() 每10ms执行一次，TimerCnt累加
 *   3 → TimerCnt >= TimerOut 时：TimerOutFlag=1（通知上层超时）
 * ─────────────────────────────────────────────────────────────
 * 使用示例：
 *   EnableTimer(true, WAIT_TIME * 10)  // 启动，超时时间=1200ms
 *   EnableTimer(false, WAIT_TIME)       // 停止
 */
static void EnableTimer(bool sta, uint16_t timeout)
{
    /* 【固定区】定时器控制 */
    s_Timer1s.TimerStart = sta;
    s_Timer1s.TimerCnt = 0;
    s_Timer1s.TimerOutFlag = 0;
    s_Timer1s.TimerOut = timeout;
}

/* ============================================================
 * 【固定区】读取超时状态
 * ─────────────────────────────────────────────────────────────
 * @return  超时状态标志位（0=未超时，1=已超时）
 */
static uint8_t GetTimerOut(void)
{
    return s_Timer1s.TimerOutFlag;
}

/* ============================================================
 * 【固定区】升级定时器回调（周期性执行，每10ms）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 检查 TimerStart 是否为1（定时器已启动）
 *   2 → TimerCnt 每调用一次 +1
 *   3 → TimerCnt >= TimerOut 时：重置TimerCnt，停止定时器，设置TimerOutFlag=1
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 此函数在 Timer Daemon 任务（高优先级）中执行，不能调用会阻塞的API
 */
static void upgradeTimer(TimerHandle_t xTimer)
{
    (void)xTimer;  /* 未使用，消除警告 */

    if (s_Timer1s.TimerStart == 1)
    {
        if (++s_Timer1s.TimerCnt >= s_Timer1s.TimerOut)
        {
            s_Timer1s.TimerCnt = 0;
            s_Timer1s.TimerStart = 0;
            s_Timer1s.TimerOutFlag = 1;
        }
    }
}

/* ============================================================
 * 【固定区】挂起所有任务（升级前调用）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → vTaskSuspend() 挂起所有业务任务（CAN通信/施肥/报警/排种/RCM）
 *   2 → xTimerStop() 停止所有关联的软件定时器
 *   3 → 升级任务本身继续执行，不被挂起
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 升级期间挂起所有任务，确保CAN总线和Flash不被其他任务访问
 */
static void SuspendAllTasks(void)
{
    /* 【固定区】挂起CAN通信任务 */
    extern void *s_CanComm_SendTask_Handle;
    extern void *s_CanComm_RecvTask_Handle;
    extern void *s_manure_task_Handle;
    extern void *s_alarm_task_Handle;
    extern void *s_seed_task_Handle;
    extern void *s_RCM_RecvTask_Handle;
    extern void *s_RCM_SendTask_Handle;

    extern void *s_CanComm_SendTimer_Handle;
    extern void *s_CanComm_Timer1s_Handle;
    extern void *s_manure_Timer_Handle;
    extern void *s_alarm_Timer_Handle;
    extern void *s_seed_Timer_Handle;
    extern void *s_RCM_RecvTimer_Handle;
    extern void *s_RCM_SendTimer_Handle;

    (void)s_CanComm_SendTask_Handle;
    (void)s_CanComm_RecvTask_Handle;
    (void)s_manure_task_Handle;
    (void)s_alarm_task_Handle;
    (void)s_seed_task_Handle;
    (void)s_RCM_RecvTask_Handle;
    (void)s_RCM_SendTask_Handle;

    (void)s_CanComm_SendTimer_Handle;
    (void)s_CanComm_Timer1s_Handle;
    (void)s_manure_Timer_Handle;
    (void)s_alarm_Timer_Handle;
    (void)s_seed_Timer_Handle;
    (void)s_RCM_RecvTimer_Handle;
    (void)s_RCM_SendTimer_Handle;

    /* 【自定义区】如果实际系统有这些任务，取消注释并调用 vTaskSuspend/xTimerStop */
    /* 示例:
     * vTaskSuspend(s_CanComm.SendTask.Task_handle);
     * xTimerStop(s_CanComm.SendTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_CanComm.RecvTask.Task_handle);
     * xTimerStop(s_CanComm.Timer1s.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_manure.manure_task.Task_handle);
     * xTimerStop(s_manure.StateTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_alarm.taskInfo.Task_handle);
     * xTimerStop(s_alarm.AlarmTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_seed.seed_task.Task_handle);
     * xTimerStop(s_seed.seedTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_RCM.RecvTask.Task_handle);
     * xTimerStop(s_RCM.RecvTime.pTimer_Handle, portMAX_DELAY);
     * vTaskSuspend(s_RCM.SendTask.Task_handle);
     * xTimerStop(s_RCM.SendTime.pTimer_Handle, portMAX_DELAY);
     */
}

/* ============================================================
 * 【固定区】恢复所有任务（升级后调用）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → vTaskResume() 恢复所有被挂起的业务任务
 *   2 → xTimerStart() 重新启动所有关联的软件定时器
 *   3 → 系统回到正常运行状态
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 升级完成后必须调用此函数，否则系统会一直挂起
 */
static void ResumeAllTasks(void)
{
    /* 【自定义区】如果实际系统有这些任务，取消注释并调用 vTaskResume/xTimerStart */
    /* 示例:
     * vTaskResume(s_CanComm.SendTask.Task_handle);
     * xTimerStart(s_CanComm.SendTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_CanComm.RecvTask.Task_handle);
     * xTimerStart(s_CanComm.Timer1s.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_manure.manure_task.Task_handle);
     * xTimerStart(s_manure.StateTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_alarm.taskInfo.Task_handle);
     * xTimerStart(s_alarm.AlarmTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_seed.seed_task.Task_handle);
     * xTimerStart(s_seed.seedTimer.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_RCM.RecvTask.Task_handle);
     * xTimerStart(s_RCM.RecvTime.pTimer_Handle, portMAX_DELAY);
     * vTaskResume(s_RCM.SendTask.Task_handle);
     * xTimerStart(s_RCM.SendTime.pTimer_Handle, portMAX_DELAY);
     */
}

/* ============================================================
 * 【固定区】发送升级请求帧
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 组装升级请求帧 {"u","p","g","r","a","d","e",0x00}
 *   2 → 调用 Can1SendMsg(ID, 1, buf, 8) 发送到目标从机
 *   3 → 从机收到后应回复 "permit" 表示允许升级
 */
static void SendUpgradeRequest(uint32_t ID)
{
    /* 【固定区】发送 "upgrade" 升级请求 */
    uint8_t sendBuf[8] = "upgrade";
    Can1SendMsg(ID, 1, sendBuf, 8);
}

/* ============================================================
 * 【固定区】发送版本信息（总帧数）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 组装版本帧：{0x82, 'S', 'S', total_frame}
 *   2 → 调用 Can1SendMsg(SENDVER_CANID=0x18FFAAD2, ...) 发送
 *   3 → 从机收到后回复 ACK，确认升级参数
 */
static void SendVersion(uint32_t ID)
{
    /* 【固定区】组装版本信息帧 */
    uint8_t sendBuf[8] = {0};
    sendBuf[0] = 0x82;
    sendBuf[1] = 'S';
    sendBuf[2] = 'S';
    sendBuf[3] = (uint8_t)total_frame;
    Can1SendMsg(ID, 1, sendBuf, 8);
}

/* ============================================================
 * 【固定区】向显示屏发送升级进度信息
 * ─────────────────────────────────────────────────────────────
 * CAN ID: 0x18FFA0D2
 * 数据格式：{UpgradeMode, UpgradeProgress, 0x04, 0x00,
 *            DeviceResult_1~4}（每设备2bit编码）
 */
static void SendDataToScreen(uint8_t UpgradeMode, uint8_t UpgradeProgress,
                             uint8_t DeviceUpgradeResult_1, uint8_t DeviceUpgradeResult_2,
                             uint8_t DeviceUpgradeResult_3, uint8_t DeviceUpgradeResult_4)
{
    /* 【固定区】组装屏幕显示数据 */
    uint8_t sendBuf[8] = {0};
    sendBuf[0] = UpgradeMode;
    sendBuf[1] = UpgradeProgress;
    sendBuf[2] = 0x04;
    sendBuf[3] = 0x00;
    sendBuf[4] = DeviceUpgradeResult_1;
    sendBuf[5] = DeviceUpgradeResult_2;
    sendBuf[6] = DeviceUpgradeResult_3;
    sendBuf[7] = DeviceUpgradeResult_4;
    Can0SendMsg(0x18FFA0D2, 1, sendBuf, 8);
}

/* ============================================================
 * 【固定区】向显示屏发送升级结果（压缩多设备状态到4字节）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 遍历RespondDevice_Num[]，读取每个设备的ReceiveFlag
 *   2 → 将16个设备的状态压缩到32位变量（每设备2bit）
 *   3 → 调用SendDataToScreen()发送到显示屏
 */
void SendResultToScreen(uint8_t UpgradeMode, uint8_t UpgradeProgress)
{
    uint8_t sendbuff[4] = {0};
    uint32_t senddata = 0xAAAAAAAA;  /* 初始值，每2bit为10b（失败/未响应） */

    /* 【固定区】压缩16个设备的升级状态 */
    for (uint8_t i = 0; i < 16; i++)
    {
        if (RespondDevice_Num[i].ReceiveFlag == 1)
        {
            senddata = (senddata << 2) | 0x01;  /* 升级成功 */
        }
        else if (RespondDevice_Num[i].ReceiveFlag == 2)
        {
            senddata = (senddata << 2) | 0x03;  /* 升级中 */
        }
        else
        {
            senddata = (senddata << 2) | 0x02;  /* 升级失败或未响应 */
        }
    }

    /* 【固定区】4字节拆分并发送 */
    sendbuff[0] = (senddata & 0xFF000000) >> 24;
    sendbuff[1] = (senddata & 0x00FF0000) >> 16;
    sendbuff[2] = (senddata & 0x0000FF00) >> 8;
    sendbuff[3] = (senddata & 0x000000FF);
    SendDataToScreen(UpgradeMode, UpgradeProgress,
                     sendbuff[0], sendbuff[1], sendbuff[2], sendbuff[3]);
}

/* ============================================================
 * 【固定区】向从机发送升级数据包（分帧传输）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 组装 upgrade_t 结构体（526字节）：
 *        head1/2='SS', index_frame, total_frame, data[520]（每8字节插序号）, checksum
 *   2 → 将结构体按每8字节一帧拆分，通过CAN1发送
 *   3 → 每帧间隔10ms，避免CAN总线负载过高
 * ─────────────────────────────────────────────────────────────
 * 数据区格式（每8字节）：
 *   byte[0] = 帧序号（0,1,2...）
 *   byte[1..7] = 有效数据（7字节）
 *   共520字节数据 → 约75帧CAN帧
 */
static void SendUpgradeData(uint8_t *buf, uint16_t index, uint16_t total_frame)
{
    uint8_t sendBuf[8] = {0};

    /* 【固定区】组装升级数据包结构体 */
    s_upgrade.num = UPDATE_PACK_SIZE;
    s_upgrade.head1 = 'S';
    s_upgrade.head2 = 'S';
    s_upgrade.index_frame = index;
    s_upgrade.checksum = 0;
    s_upgrade.total_frame = total_frame;

    uint16_t j = 0;

    /* 【固定区】组装数据区：每8字节插入1字节帧序号 */
    for (uint16_t i = 0; i < (UPDATE_PACK_SIZE + UPDATE_NUM_SIZE); i++)
    {
        if ((i % 8) == 0)
        {
            s_upgrade.data[i] = (i / 8);  /* 序号字节 */
        }
        else
        {
            s_upgrade.data[i] = buf[j];    /* 有效数据 */
            j++;
        }
    }

    /* 【固定区】计算CRC校验值 */
    s_upgrade.checksum = GetSkipCrc16OfRam(s_upgrade);

    /* 【固定区】分帧发送（每帧8字节） */
    uint16_t upgrade_size = sizeof(s_upgrade);
    uint16_t send_cnt = upgrade_size / 8;
    uint16_t send_index = 0;

    while (send_index < send_cnt)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            sendBuf[i] = ((uint8_t *)&s_upgrade)[send_index * 8 + i];
        }

        delay_xms(10);  /* 【固定区】帧间隔10ms */
        Can1SendMsg(0x18FFBAD2, 1, sendBuf, 8);

        send_index++;
    }
}

/* ============================================================
 * 【固定区】解析从机返回的ACK响应
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 检查接收数据前4字节是否为 "ACK:"
 *   2 → 提取 byte[4] 作为 ACK 状态码
 *   3 → 提取 byte[5]<<8 | byte[6] 作为期望的下一帧序号
 * ─────────────────────────────────────────────────────────────
 * @return  1=解析成功（有效ACK），0=解析失败（非ACK）
 */
static uint8_t AnalysisACK(const char *AnalysisBuf, uint8_t *ack_cmd, uint16_t *need_index)
{
    static const char ack[] = "ACK:";

    if (strncmp(ack, AnalysisBuf, 4) != 0)
        return 0;  /* 不是ACK帧 */

    *ack_cmd = AnalysisBuf[4];
    *need_index = AnalysisBuf[5] << 8 | AnalysisBuf[6];
    return 1;
}

/* ============================================================
 * 【固定区】OTA 升级主流程（主机端，单设备模式）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】主机对单个从机的完整升级流程：
 *   1 → eSmcuOtaNull → eSmcuOtaUpgradeRequest：发送升级请求 "upgrade"
 *   2 → eSmcuOtaWaitACKInBootLoard：等待从机回复 "permit"
 *   3 → eSmcuOtaSendVer → eSmcuOtaWaitVerACK：发送版本信息（总帧数）
 *   4 → eSmcuOtaReadFile → eSmcuOtaSendData → eSmcuOtaWaitDataACK：
 *        循环读取固件文件，分帧发送，等待每帧ACK
 *   5 → eSmcuOtaSendDataEnd → eSmcuOtaWaitMoveFlashACK：发送最后一帧
 *   6 → eSmcuOtaWaitResultACK：等待升级结果
 * ─────────────────────────────────────────────────────────────
 * 状态机（12个状态）：
 *   eSmcuOtaNull → eSmcuOtaUpgradeRequest → eSmcuOtaWaitACKInBootLoard
 *   → eSmcuOtaSendVer → eSmcuOtaWaitVerACK
 *   → [循环] eSmcuOtaReadFile → eSmcuOtaSendData → eSmcuOtaWaitDataACK
 *   → eSmcuOtaSendDataEnd → eSmcuOtaWaitMoveFlashACK
 *   → eSmcuOtaWaitResultACK / eSmcuOtaWaitACKTimeOut
 * ─────────────────────────────────────────────────────────────
 * @return  1=成功，2=文件丢失，3=超时
 */
static uint8_t DownOta_Process(uint32_t ID)
{
    uint8_t reset_err_cnt = 0;
    uint8_t err_cnt = 0;

    McuOtaArg.OtaStep = eSmcuOtaNull;

    /* 【固定区】文件指针定位到文件头 */
    lfs_file_seek(&lfs_w25qxx, &lfs_file_w25qxx, 0, LFS_SEEK_SET);

    while (1)
    {
        /* 【固定区】从CAN1接收队列读取数据（100ms超时） */
        if (xQueueReceive(s_RCM.RecvQueue, &msg, 100) != pdPASS)
        {
            memset(&msg, 0, sizeof(msg));
        }

        if (msg.id == 0)
        {
            /* 无数据，等待超时判断 */
        }
        else if (msg.id == 0x18FFC1D2)
        {
            /* 【固定区】接收到响应ID的数据 */
            Aux_UpgradeCanMsg.RecvFlag = 1;
            memcpy(Aux_UpgradeCanMsg.BUFF, msg.data, sizeof(Aux_UpgradeCanMsg.BUFF));
            memset(&msg, 0, sizeof(msg));
        }
        else
        {
            continue;
        }

        if (reset_err_cnt)
        {
            reset_err_cnt = 0;
            err_cnt = 0;
        }

        /* ============================================================
         * 【固定区】OTA状态机 — 12个状态处理分支
         * ============================================================ */
        switch (McuOtaArg.OtaStep)
        {
            case eSmcuOtaNull:
            {
                err_cnt = 0;
                EnableTimer(true, WAIT_TIME * 10);
                McuOtaArg.FileOffset = 0;
                McuOtaArg.OtaStep = eSmcuOtaUpgradeRequest;
                break;
            }

            case eSmcuOtaUpgradeRequest:
            {
                SendUpgradeRequest(ID);
                EnableTimer(true, WAIT_TIME * 10);
                McuOtaArg.OtaStep = eSmcuOtaWaitACKInBootLoard;
                break;
            }

            case eSmcuOtaWaitACKInBootLoard:
            {
                if (Aux_UpgradeCanMsg.RecvFlag)
                {
                    Aux_UpgradeCanMsg.RecvFlag = 0;
                    char *permit = "permit";

                    if (strncmp((char *)permit, (char *)Aux_UpgradeCanMsg.BUFF, 2) == 0)
                    {
                        McuOtaArg.OtaStep = eSmcuOtaSendVer;
                        reset_err_cnt = 1;
                    }
                    else
                    {
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                        EnableTimer(false, WAIT_TIME * 10);
                        return 2;
                    }
                }
                else if (GetTimerOut())
                {
                    SendUpgradeRequest(ID);
                    EnableTimer(true, WAIT_TIME * 10);
                    if (err_cnt++ >= ERR_CNT)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                }
                break;
            }

            case eSmcuOtaSendVer:
            {
                SendVersion(SENDVER_CANID);
                EnableTimer(true, WAIT_TIME * 10);
                McuOtaArg.OtaStep = eSmcuOtaWaitVerACK;
                break;
            }

            case eSmcuOtaWaitVerACK:
            {
                if (Aux_UpgradeCanMsg.RecvFlag)
                {
                    Aux_UpgradeCanMsg.RecvFlag = 0;
                    const uint8_t Ack[] = "ACK:";

                    if (strncmp((char *)Ack, (char *)Aux_UpgradeCanMsg.BUFF, 4) == 0)
                    {
                        McuOtaArg.OtaStep = eSmcuOtaReadFile;
                        reset_err_cnt = 1;
                    }
                    else
                    {
                        McuOtaArg.OtaStep = eSmcuOtaSendVer;
                        EnableTimer(true, WAIT_TIME * 10);
                        if (err_cnt++ >= ERR_CNT)
                            McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                    }
                }
                else if (GetTimerOut())
                {
                    SendVersion(SENDVER_CANID);
                    EnableTimer(true, WAIT_TIME * 10);
                    if (err_cnt++ >= ERR_CNT)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                }
                break;
            }

            case eSmcuOtaReadFile:
            {
                /* 【固定区】从W25Q文件系统读取固件数据（512字节） */
                uint8_t retSta = lfs_file_read(&lfs_w25qxx, &lfs_file_w25qxx,
                                               Aux_temp_buff, UPDATE_PACK_SIZE);

                if (retSta < LFS_ERR_OK)
                {
                    if (err_cnt++ >= ERR_CNT)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                }
                else
                {
                    McuOtaArg.OtaStep = eSmcuOtaSendData;
                    reset_err_cnt = 1;
                }

                if (McuOtaArg.FileOffset == total_frame)
                {
                    McuOtaArg.OtaStep = eSmcuOtaSendDataEnd;
                    reset_err_cnt = 1;
                }
                break;
            }

            case eSmcuOtaSendData:
            {
                SendUpgradeData(Aux_temp_buff, McuOtaArg.FileOffset, total_frame);
                EnableTimer(true, WAIT_TIME * 10);
                McuOtaArg.OtaStep = eSmcuOtaWaitDataACK;
                SendResultToScreen(0x81, ((McuOtaArg.FileOffset * 100) / total_frame));
                break;
            }

            case eSmcuOtaWaitDataACK:
            {
                if (Aux_UpgradeCanMsg.RecvFlag)
                {
                    Aux_UpgradeCanMsg.RecvFlag = 0;

                    if (AnalysisACK((char *)Aux_UpgradeCanMsg.BUFF,
                                    &McuOtaArg.ack_cmd, &McuOtaArg.need_index) == 1)
                    {
                        if (McuOtaArg.ack_cmd == eACK_Index)
                        {
                            McuOtaArg.FileOffset = McuOtaArg.need_index;
                            McuOtaArg.OtaStep = eSmcuOtaReadFile;
                            reset_err_cnt = 1;
                        }
                        else if (McuOtaArg.ack_cmd == eACK_CheckErr)
                        {
                            McuOtaArg.OtaStep = eSmcuOtaSendData;
                            if (++err_cnt >= ERR_CNT)
                                McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                        }
                    }
                }
                else if (GetTimerOut())
                {
                    EnableTimer(true, WAIT_TIME * 10);
                    if (++err_cnt >= ERR_CNT)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                }
                break;
            }

            case eSmcuOtaSendDataEnd:
            {
                SendUpgradeData(Aux_temp_buff, McuOtaArg.FileOffset, total_frame);
                SendResultToScreen(0x81, 100);
                EnableTimer(true, WAIT_TIME * 10);
                McuOtaArg.OtaStep = eSmcuOtaWaitMoveFlashACK;
                break;
            }

            case eSmcuOtaWaitMoveFlashACK:
            {
                if (Aux_UpgradeCanMsg.RecvFlag)
                {
                    Aux_UpgradeCanMsg.RecvFlag = 0;

                    if (AnalysisACK((char *)Aux_UpgradeCanMsg.BUFF,
                                    &McuOtaArg.ack_cmd, &McuOtaArg.need_index) == 1)
                    {
                        if (McuOtaArg.ack_cmd == eACK_MoveFlash)
                        {
                            McuOtaArg.OtaStep = eSmcuOtaWaitResultACK;
                            reset_err_cnt = 1;
                        }
                        else if (McuOtaArg.ack_cmd == eACK_CheckErr)
                        {
                            McuOtaArg.OtaStep = eSmcuOtaSendDataEnd;
                            EnableTimer(true, WAIT_TIME * 10);
                            if (err_cnt++ >= ERR_CNT)
                                McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                        }
                    }
                }
                else if (GetTimerOut())
                {
                    if (err_cnt++ % (ERR_CNT * 5) == 0)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                    else if (err_cnt % ERR_CNT == 0)
                        McuOtaArg.OtaStep = eSmcuOtaSendDataEnd;

                    EnableTimer(true, WAIT_TIME * 10);
                }
                break;
            }

            case eSmcuOtaWaitResultACK:
            {
                if (Aux_UpgradeCanMsg.RecvFlag)
                {
                    Aux_UpgradeCanMsg.RecvFlag = 0;

                    if (AnalysisACK((char *)Aux_UpgradeCanMsg.BUFF,
                                    &McuOtaArg.ack_cmd, &McuOtaArg.need_index) == 1)
                    {
                        if (McuOtaArg.ack_cmd == eACK_UpdateSuccess)
                        {
                            McuOtaArg.FileOffset = 0;
                            McuOtaArg.OtaStep = eSmcuOtaNull;
                            EnableTimer(false, WAIT_TIME * 10);
                            return 1;
                        }
                        else
                        {
                            EnableTimer(false, WAIT_TIME * 10);
                            return McuOtaArg.ack_cmd;
                        }
                    }
                }
                else if (GetTimerOut())
                {
                    EnableTimer(true, WAIT_TIME * 10);
                    if (err_cnt++ >= ERR_CNT * 5)
                        McuOtaArg.OtaStep = eSmcuOtaWaitACKTimeOut;
                }
                break;
            }

            case eSmcuOtaWaitACKTimeOut:
            {
                EnableTimer(false, WAIT_TIME * 10);
                reset_err_cnt = 1;
                err_cnt = 0;
                McuOtaArg.OtaStep = eSmcuOtaNull;
                return 3;
            }
        }
    }
}

/* ============================================================
 * 【固定区】从机升级主流程（支持广播模式和单设备模式）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 判断升级模式：CanId==0x18FFA4D2 → 广播模式，否则单设备模式
 *   2 → 广播模式：调用 Upgrade_BroadcastMode() 一次升级所有设备
 *   3 → 如果广播模式有失败设备，切换为逐个升级失败的设备
 * ─────────────────────────────────────────────────────────────
 * @note  广播模式升级失败时，回退到逐个设备升级模式
 */
static void AuxUpgrade_Process(uint32_t CanId)
{
    uint8_t DeviceId = 0, ResultNum = 0, temp = 0;
    CanId = (CanId << 8) | 0x18FFA0D2;

    if (CanId == 0x18FFA4D2)
    {
        /* 【固定区】广播模式升级 */
        temp = Upgrade_BroadcastMode(BROADCAST_CANID);

        if (temp == DeviceNum)
        {
            SendResultToScreen(0x44, 0);  /* 广播模式所有设备升级成功 */
        }
        else
        {
            /* 【固定区】广播模式失败，切换为逐个设备升级 */
            SendResultToScreen(0x48, 0);

            for (uint8_t i = 0; i < DEVICE_NUM; i++)
            {
                if ((RespondDevice_Num[DeviceId].Enable) &&
                    (RespondDevice_Num[DeviceId].ReceiveFlag == 0))
                {
                    if (DeviceId >= 15)
                        CanId = 0x18BCA41F + ((DeviceId - 15) << 4);
                    else
                        CanId = 0x18BCA401 + DeviceId;

                    RespondDevice_Num[DeviceId].ReceiveFlag = 2;
                    SendResultToScreen(0x81, 0);

                    uint8_t send_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                    SendMsgToRCM(0x2023, send_data, 8);

                    uint8_t result = DownOta_Process(CanId);

                    if (result == 1)
                        RespondDevice_Num[DeviceId].ReceiveFlag = 1;
                    else
                        RespondDevice_Num[DeviceId].ReceiveFlag = 0;
                }
                DeviceId++;
            }

            if (ResultNum)
                SendResultToScreen(0x48, 0);
            else
                SendResultToScreen(0x44, 0);
        }
    }
    else
    {
        /* 【固定区】单设备模式升级 */
        DownOta_Process(CanId);
    }
}

/* ============================================================
 * 【固定区】升级任务主入口 — 从机OTA端（接收升级包侧）
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】从机上电后完整升级流程：
 *   1 → UpgradeTask_Init() 创建任务+定时器，任务初始挂起
 *   2 → 外部设置 s_upgradeTask.init_flag 唤醒任务
 *   3 → SuspendAllTasks() → 挂起所有任务 → 进入升级模式
 *   4 → 状态机：eInit → eWaitVer → eWaitUpdate1stPack → eWaitUpdatePack
 *   5 → CheckPack() 从环形缓冲区循环解析CAN帧，写入Flash/文件系统
 *   6 → 最后一帧收到后 → eMoveFlash → 通知从机执行Flash搬运
 *   7 → 升级完成 → ResumeAllTasks() → vTaskSuspend(NULL)（挂起自己）
 * ─────────────────────────────────────────────────────────────
 * 状态机（5个状态）：
 *   eInit → eWaitVer → eWaitUpdate1stPack → eWaitUpdatePack → eMoveFlash
 *
 * 升级类型：
 *   init_flag=1 → 主机自身升级（直接写入备份Flash）
 *   init_flag=2/3 → 从机升级（写入W25Qxx文件系统）
 *   init_flag=4 → 排种器升级（写入W25Qxx文件系统）
 */
static void UpgradeTask_Process(void *param)
{
    (void)param;

    /* 【固定区】任务初始挂起自己，等待外部唤醒 */
    vTaskSuspend(s_upgradeTask.upgradeTask.Task_handle);

    uint8_t err_cnt = 0;
    uint8_t reset_err_cnt = 0;
    uint8_t UpgradeType = 0;
    uint8_t OtaSta = 0, lfs_file_open_state = 0;
    BaseType_t Status = pdFALSE;

    /* 【固定区】分配512字节临时缓冲区 */
    Aux_temp_buff = (uint8_t *)pvPortMalloc(UPDATE_PACK_SIZE);

    /* 【固定区】打开文件系统（从机升级时使用） */
    if ((lfs_file_open(&lfs_w25qxx, &lfs_file_w25qxx,
                        AuxCtl_file, LFS_O_RDWR | LFS_O_CREAT)) != LFS_ERR_OK)
    {
        lfs_file_open_state = 1;
    }

    while (1)
    {
        if (s_upgradeTask.init_flag >= 1)
        {
            /* 【固定区】初始化标志置位，准备开始升级 */
            memset(&s_upgrade, 0, sizeof(s_upgrade));
            OtaSta = 0;
            xTimerStart(s_upgradeTask.Timer1s.pTimer_Handle, portMAX_DELAY);
            eupgrade_step = eInit;
            err_cnt = 0;
            reset_err_cnt = 0;
            UpgradeType = s_upgradeTask.init_flag;

            if (s_upgradeTask.init_flag == 1)
            {
                /* 【自定义区】主机自身升级，清除Info信息 */
                UpgradeType = 0;
                /* s_eep_upgradeInfo.upgradeFlag = 0; */
                /* s_eep_upgradeInfo.tmpFlashProgramOk = 0; */
                /* s_eep_upgradeInfo.tmpFlashProgramSize = 0; */
                /* eeprom_write_32bBuff(UPDATE_ADDR, (uint32_t *)&s_eep_upgradeInfo, sizeof(s_eep_upgradeInfo)); */
            }

            s_upgradeTask.init_flag = 0;
            SuspendAllTasks();
            vTaskDelay(20);
        }

        /* 【固定区】从CAN通信队列读取数据（20ms超时） */
        Status = xQueueReceive(s_CanComm.RecvQueue, &msg, 20);

        if (Status != pdPASS)
            memset(&msg, 0, sizeof(msg));

        if (msg.id == 0x18FFAAD2)
        {
            /* 【固定区】接收到TBOX下发的版本确认消息 */
            s_UpgradeCanMsg.RecvFlag = 1;
            memcpy(s_UpgradeCanMsg.BUFF, msg.data, sizeof(s_UpgradeCanMsg.BUFF));
        }
        else if (msg.id == 0x18FFBAD2)
        {
            /* 【固定区】接收到升级数据包，存入环形缓冲区 */
            for (uint8_t i = 0; i < msg.dlc; i++)
                CircularBufferPushBack(&s_RecvCB, &msg.data[i]);
        }
        else
        {
            memset(&msg, 0, sizeof(msg));
        }

        if (reset_err_cnt)
        {
            reset_err_cnt = 0;
            err_cnt = 0;
        }

        /* ============================================================
         * 【固定区】从机端OTA状态机 — 5个状态处理分支
         * ============================================================ */
        switch (eupgrade_step)
        {
            case eInit:
            {
                need_index = 0;
                reset_err_cnt = 0;

                if (UpgradeType)
                {
                    /* 【固定区】从机升级（写入文件系统） */
                    if (lfs_file_open_state)
                    {
                        /* ReplyUpgradePermit(false); */
                        if (++err_cnt >= ERR_CNT)
                        {
                            err_cnt = 0;
                            OtaSta = 2;
                            goto exit;
                        }
                    }
                    else
                    {
                        lfs_file_seek(&lfs_w25qxx, &lfs_file_w25qxx, 0, LFS_SEEK_SET);
                        /* ReplyUpgradePermit(true); */
                        eupgrade_step = eWaitVer;
                        reset_err_cnt = 1;
                        EnableTimer(true, WAIT_TIME * 10);
                    }
                }
                else
                {
                    /* 【自定义区】主机自身升级，擦除备份区Flash */
                    /* if (FmcEraseBackUpPages() > 0) { ReplyUpgradePermit(false); ... } */
                    /* else { ReplyUpgradePermit(true); eupgrade_step = eWaitVer; ... } */
                    eupgrade_step = eWaitVer;
                    reset_err_cnt = 1;
                    EnableTimer(true, WAIT_TIME * 10);
                }
                break;
            }

            case eWaitVer:
            {
                if (s_UpgradeCanMsg.RecvFlag == 1)
                {
                    s_UpgradeCanMsg.RecvFlag = 0;
                    SendACK(eACK_Simple, 0);
                    EnableTimer(true, WAIT_TIME * 10);
                    memset(&s_upgrade, 0, sizeof(s_upgrade));
                    eupgrade_step = eWaitUpdate1stPack;
                    reset_err_cnt = 1;
                }
                else if (GetTimerOut())
                {
                    EnableTimer(true, WAIT_TIME * 10);
                    /* ReplyUpgradePermit(true); */
                    if (++err_cnt >= ERR_CNT)
                    {
                        err_cnt = 0;
                        EnableTimer(false, WAIT_TIME);
                        OtaSta = 4;
                        goto exit;
                    }
                }
                break;
            }

            case eWaitUpdate1stPack:
            {
                uint8_t checkSta = CheckPack(&s_RecvCB, &s_upgrade);

                if (checkSta > 0)
                {
                    if (checkSta == 1)
                    {
                        total_frame = s_upgrade.total_frame;

                        if (s_upgrade.index_frame == need_index)
                        {
                            /* 【自定义区】写入Flash或文件系统 */
                            /* vPortEnterCritical();
                             * if (UpgradeType) lfs_file_write(...);
                             * else FmcWrite(BACKUP_FLASH_START_ADDR + need_index, ...);
                             * vPortExitCritical();
                             */
                            need_index++;
                        }
                    }

                    memset(&s_upgrade, 0, sizeof(s_upgrade));
                    SendACK(eACK_Index, need_index);
                    EnableTimer(true, WAIT_TIME * 10);
                    eupgrade_step = eWaitUpdatePack;
                    reset_err_cnt = 1;
                }
                else if (GetTimerOut() == 1)
                {
                    EnableTimer(true, WAIT_TIME * 10);
                    SendACK(eACK_Simple, 0);
                    if (++err_cnt >= ERR_CNT)
                    {
                        err_cnt = 0;
                        EnableTimer(false, WAIT_TIME * 10);
                        OtaSta = 4;
                        goto exit;
                    }
                }
                break;
            }

            case eWaitUpdatePack:
            {
                uint8_t checkSta = CheckPack(&s_RecvCB, &s_upgrade);

                if (checkSta > 0)
                {
                    err_cnt = 0;

                    if (checkSta == 1)
                    {
                        if (s_upgrade.index_frame == need_index)
                        {
                            /* 【自定义区】写入Flash或文件系统 */
                            need_index++;

                            if (need_index == total_frame)
                            {
                                /* 【固定区】最后一帧，通知Flash搬运 */
                                memset(&s_upgrade, 0, sizeof(s_upgrade));
                                SendACK(eACK_MoveFlash, 0);
                                eupgrade_step = eMoveFlash;
                                break;
                            }
                        }

                        memset(&s_upgrade, 0, sizeof(s_upgrade));
                        SendACK(eACK_Index, need_index);
                        EnableTimer(true, WAIT_TIME * 10);
                        reset_err_cnt = 1;
                    }
                    else
                    {
                        /* 【固定区】CRC校验错误，请求重发 */
                        memset(&s_upgrade, 0, sizeof(s_upgrade));
                        SendACK(eACK_CheckErr, need_index);
                        EnableTimer(true, WAIT_TIME * 10);
                        reset_err_cnt = 1;
                    }
                }
                else if (GetTimerOut() == 1)
                {
                    EnableTimer(true, WAIT_TIME * 10);
                    SendACK(eACK_CheckErr, need_index);
                    if (++err_cnt >= ERR_CNT)
                    {
                        err_cnt = 0;
                        EnableTimer(false, WAIT_TIME * 10);
                        OtaSta = 4;
                        goto exit;
                    }
                }
                break;
            }

            case eMoveFlash:
            {
                /* 【自定义区】主机升级执行Flash搬运，从机升级关闭文件系统 */
                /* vPortEnterCritical();
                 * if (read_usr_write_file() < 0) s_eep_upgradeInfo.w25qProgramOk = 0;
                 * else s_eep_upgradeInfo.w25qProgramOk = 1;
                 * vPortExitCritical();
                 */

                /* s_eep_upgradeInfo.tmpFlashProgramOk = 1; */
                /* s_eep_upgradeInfo.tmpFlashProgramSize = total_frame + 1; */
                SendACK(eACK_UpdateSuccess, 0);
                delay_xms(20);
                OtaSta = 1;
                goto exit;
            }

            default:
            {
                eupgrade_step = eInit;
                break;
            }
        }

exit:
        if (OtaSta > 0)
        {
            if (UpgradeType == 0)
            {
                xTimerStop(s_upgradeTask.Timer1s.pTimer_Handle, portMAX_DELAY);
                EnableTimer(false, WAIT_TIME * 10);
            }

            CircularBufferClear(&s_RecvCB);

            if (OtaSta == 1)
            {
                if (UpgradeType == 0)
                {
                    /* 【自定义区】主机升级完成后触发软复位，转BootLoader */
                    /* s_eep_upgradeInfo.upgradeFlag = 1; */
                    /* vPortFree(RecvBuf); */
                    /* vPortFree(Aux_temp_buff); */
                    /* eeprom_write_32bBuff(UPDATE_ADDR, ...); */
                    /* __disable_irq(); */
                    /* NVIC_SystemReset(); */
                }
                else
                {
                    /* 【固定区】从机升级完成后 */
                    memset(&msg, 0, sizeof(msg));
                    AuxUpgrade_Process(UpgradeType);
                    xTimerStop(s_upgradeTask.Timer1s.pTimer_Handle, portMAX_DELAY);
                    EnableTimer(false, WAIT_TIME * 10);
                    ResumeAllTasks();
                    vTaskSuspend(NULL);
                }
            }
            else
            {
                xTimerStop(s_upgradeTask.Timer1s.pTimer_Handle, portMAX_DELAY);
                ResumeAllTasks();
                vTaskSuspend(NULL);
                EnableTimer(false, 1000);
            }
        }
    }

    /* 【固定区】释放动态分配内存（理论上不会执行到这里） */
    vPortFree(Aux_temp_buff);
    vPortFree(RecvBuf);
    lfs_file_close(&lfs_w25qxx, &lfs_file_w25qxx);
}

/* ============================================================
 * 【固定区】广播模式响应设备计数
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 初始化 RespondDevice_Num[]，统计 Enable==1 的设备数量
 *   2 → 启动超时定时器，循环等待所有从机响应
 *   3 → 每收到一个设备的ACK → num++，直到所有设备响应或超时
 * ─────────────────────────────────────────────────────────────
 * @return  已响应设备数量
 */
uint8_t RespondCheck(void)
{
    uint8_t num = 0;
    DeviceNum = 0;

    /* 【固定区】清零所有设备响应状态，统计使能设备数 */
    for (uint8_t i = 0; i < DEVICE_NUM; i++)
    {
        RespondDevice_Num[i].ReceiveFlag = 0;
        if (RespondDevice_Num[i].Enable == 1)
            DeviceNum++;
    }

    /* 【固定区】启动超时定时器 */
    EnableTimer(true, OVERTIME);

    if (McuOtaArg.OtaStep == eSmcuOtaWaitResultACK)
        EnableTimer(true, OVERTIME * 2);  /* 延长超时时间 */

    do
    {
        if (xQueueReceive(s_RCM.RecvQueue, &msg, 100) != pdPASS)
            memset(&msg, 0, sizeof(msg));

        if ((msg.id > 0x18BD0000) && (msg.id < 0x18BD00FF))
        {
            RespondDevice_Num[(uint8_t)((msg.id & 0x000000FF)) - 1].ReceiveFlag = 1;

            if (McuOtaArg.OtaStep == eSmcuOtaSendVer)
            {
                num++;
            }
            else
            {
                if (McuOtaArg.OtaStep == eSmcuOtaWaitMoveFlashACK)
                {
                    if ((strncmp((char *)msg.data, "ACK:", 4) == 0) && (msg.data[4] == 0x06))
                        num++;
                }
                else
                {
                    if (strncmp((char *)msg.data, "ACK:", 4) == 0)
                        num++;
                }
            }

            memset(&msg, 0, sizeof(msg));
            EnableTimer(true, OVERTIME);
        }

        if (num == DeviceNum)
            return num;

        if (GetTimerOut())
        {
            EnableTimer(true, OVERTIME);
            return num;
        }
    } while (1);
}

/* ============================================================
 * 【固定区】广播模式升级主函数
 * ─────────────────────────────────────────────────────────────
 * 【运行时步骤 1/2/3...】
 *   1 → 发送升级请求到广播ID，所有从机同时接收
 *   2 → RespondCheck() 等待所有从机响应 "ACK:"
 *   3 → 发送版本信息（总帧数）到 SENDVER_CANID
 *   4 → 循环读取固件文件，SendUpgradeData() 发送所有帧
 *   5 → 最后一帧发送后，等待所有从机完成Flash搬运
 *   6 → 返回已成功升级的设备数量
 * ─────────────────────────────────────────────────────────────
 * @note  广播模式特点：一次发送，所有从机同时接收
 *        最后统一等待所有从机完成（通过 RespondCheck）
 */
uint8_t Upgrade_BroadcastMode(uint32_t ID)
{
    uint8_t err_cnt = 0;
    uint8_t reset_err_cnt = 0;
    uint8_t OtaSta = 0, OkDeviceNum = 0;

    /* 【固定区】文件指针定位到文件头 */
    lfs_file_seek(&lfs_w25qxx, &lfs_file_w25qxx, 0, LFS_SEEK_SET);

    /* 【固定区】发送前要求所有设备停止升级相关消息发送 */
    uint8_t send_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    /* send_data[0] = (uint8_t)s_SetParam.u32boZhongHang; */
    SendMsgToRCM(0x2023, send_data, 8);

    xQueueReset(s_RCM.RecvQueue);
    memset(&msg, 0, sizeof(msg));

    SendDataToScreen(0x41, 0x00, 0, 0, 0, 0);
    McuOtaArg.FileOffset = 0;
    SendUpgradeRequest(ID);
    EnableTimer(true, OVERTIME);
    McuOtaArg.OtaStep = eSmcuOtaSendVer;
    vTaskDelay(50);

    do
    {
        OkDeviceNum = RespondCheck();

        if (OkDeviceNum)
        {
            switch (McuOtaArg.OtaStep)
            {
                case eSmcuOtaSendVer:
                {
                    SendVersion(SENDVER_CANID);
                    McuOtaArg.OtaStep = eSmcuOtaReadFile;
                    break;
                }

                case eSmcuOtaReadFile:
                {
                    /* 【固定区】从文件系统读取固件数据 */
                    uint8_t retSta = lfs_file_read(&lfs_w25qxx, &lfs_file_w25qxx,
                                                   Aux_temp_buff, UPDATE_PACK_SIZE);

                    if (retSta < LFS_ERR_OK)
                        return 2;

                    SendUpgradeData(Aux_temp_buff, McuOtaArg.FileOffset, total_frame);
                    McuOtaArg.FileOffset++;

                    SendDataToScreen(0x41, (McuOtaArg.FileOffset * 100) / (total_frame + 1),
                                     0, 0, 0, 0);

                    if (McuOtaArg.FileOffset == total_frame + 1)
                        McuOtaArg.OtaStep = eSmcuOtaWaitMoveFlashACK;

                    break;
                }

                case eSmcuOtaWaitMoveFlashACK:
                case eSmcuOtaWaitResultACK:
                {
                    return OkDeviceNum;
                }

                default:
                    break;
            }
        }
    } while (OkDeviceNum);

    xQueueReset(s_RCM.RecvQueue);
    return 0;
}

/* ============================================================
 * 【固定区】设置指定行控制器的连接状态
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】在广播模式升级前，调用此函数设置参与升级的设备列表
 * @param DeviceId  设备ID（行号，0~17）
 * @param state     使能状态（true=参与升级）
 */
void SetRowConnectState(uint8_t DeviceId, bool state)
{
    RespondDevice_Num[DeviceId].Enable = state;
}

/* ============================================================
 * 【固定区】OTA 升级任务初始化
 * ─────────────────────────────────────────────────────────────
 * 【搭建步骤 1/2/3...】
 *   1 → RecvBufInit() 分配接收缓冲区，初始化环形缓冲区
 *   2 → xTaskCreate(UpgradeTask_Process, ...) 创建升级任务
 *   3 → xTimerCreate(upgradeTimer) 创建软件定时器
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 此函数在 main() 的 Step 12 中调用（在 __enable_irq 之前）
 * ⚠️ 任务创建后会自动挂起，等待外部设置 s_upgradeTask.init_flag 唤醒
 */
void UpgradeTask_Init(void)
{
    /* 【固定区】初始化接收缓冲区 */
    RecvBufInit();

    BaseType_t retSta = 0;

    /* 【固定区】配置升级任务参数 */
    s_upgradeTask.upgradeTask.Task_name = "upgradeTask";
    s_upgradeTask.upgradeTask.Task_priority = osPriorityRealtime5;
    s_upgradeTask.upgradeTask.Task_stackDepth = configMINIMAL_STACK_SIZE * 4;

    /* 【固定区】创建升级任务 */
    retSta = xTaskCreate(
        UpgradeTask_Process,
        s_upgradeTask.upgradeTask.Task_name,
        s_upgradeTask.upgradeTask.Task_stackDepth,
        NULL,
        s_upgradeTask.upgradeTask.Task_priority,
        &s_upgradeTask.upgradeTask.Task_handle
    );

    if (retSta != pdPASS)
    {
        debug_printf(INFO_ERR, "create upgradeTask fail\r\n");
        configASSERT(retSta);
    }

    /* 【固定区】配置升级定时器参数 */
    s_upgradeTask.Timer1s.Timer_ID = 0;
    s_upgradeTask.Timer1s.Timer_name = "upgrade timer";
    s_upgradeTask.Timer1s.Timer_State = pdTRUE;
    s_upgradeTask.Timer1s.Timer_Tick = 1;  /* 1ms周期 */

    /* 【固定区】创建软件定时器（upgradeTimer回调） */
    s_upgradeTask.Timer1s.pTimer_Handle = xTimerCreate(
        s_upgradeTask.Timer1s.Timer_name,
        s_upgradeTask.Timer1s.Timer_Tick,
        s_upgradeTask.Timer1s.Timer_State,
        &s_upgradeTask.Timer1s.Timer_ID,
        upgradeTimer
    );
}