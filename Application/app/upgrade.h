/**
 * @file    upgrade.h
 * @brief   OTA 固件升级协议头文件
 *
 * @details 本模块实现车载控制器通过 CAN 总线对所有从机（RCM 电机控制器、排种器等）
 *          进行 OTA 在线固件升级的完整协议。
 *
 * @note    协议架构：
 *          升级请求 → 等待 BootLoader 就绪 → 发送固件版本 → 读取文件 → 分帧发送数据
 *          每帧 512 字节有效数据 + 74 字节帧头/校验（共 600 字节）
 *          BootLoader 收到数据后写入 Flash，完成后执行程序跳转
 *
 * @note    关键流程：
 *          eInit → eWaitVer → eWaitUpdate1stPack → eWaitUpdatePack → eMoveFlash
 */

#ifndef _UPGRADE_H_
#define _UPGRADE_H_

#include "include.h"

/* ============================================================
 * 升级包参数定义
 * ============================================================ */
#define UPDATE_PACK_SIZE        512     /* 单帧升级包有效数据大小（字节） */
#define UPDATE_NUM_SIZE         74      /* 升级包帧头/校验区大小（字节） */
#define BUF_SIZE                1040    /* 接收缓冲区大小（字节） */
#define ERR_CNT                 4       /* 等待ACK超时最大重试次数 */
#define WAIT_TIME               120     /* 超时时间阈值（ms） */
#define DEVICE_NUM              18       /* 车载控制器从机最大设备数量 */
#define OVERTIME                (WAIT_TIME / 3 * 100)  /* 广播模式超时时间（约4s） */
#define SENDVER_CANID           0x18FFAAD2   /* 发送版本信息的CAN ID */
#define BROADCAST_CANID         0x18FFA5D2   /* 广播模式CAN ID */

/* ============================================================
 * CRC校验参数定义
 * ============================================================ */
#define CRC_CONST_CODE          0x8408   /* CRC16反转多项式（与0x1021等价） */
#define CRC_CHECK_MASK         0x0001   /* CRC最低位掩码 */

/* ============================================================
 * 【固定区】升级步骤枚举 — OTA从机端主状态机
 * ============================================================ */
typedef enum {
    eInit = 0,                 /* 初始化（擦除备份区/打开文件系统） */
    eWaitVer,                  /* 等待上位机版本确认 */
    eWaitUpdate1stPack,        /* 等待第一帧升级数据包 */
    eWaitUpdatePack,           /* 等待后续升级数据包（循环） */
    eMoveFlash,                /* 通知从机将备份区Flash搬运到用户区 */
} eUpgradeStep;

/* ============================================================
 * 【固定区】ACK响应类型枚举
 * ============================================================ */
typedef enum {
    eACK_Simple = 0,           /* 简单确认（仅回复） */
    eACK_Index,                /* 请求下一帧数据 */
    eACK_UpdateSuccess,        /* 升级成功 */
    eACK_CheckErr,             /* CRC校验错误，需重发 */
    eACK_WaitTimeOut,           /* 等待超时 */
    eACK_WriteFlashErr,         /* 写Flash错误 */
    eACK_MoveFlash,            /* Flash搬运指令（通知从机执行搬运） */
    eACK_VerConsistency,       /* 版本一致，不需要升级 */
    eACK_UpdateFail,           /* 升级失败 */
} eACK_TYPE;

/* ============================================================
 * 【固定区】OTA从机端流程状态枚举
 * ============================================================ */
typedef enum {
    eSmcuOtaNull = 0,                  /* 空状态 */
    eSmcuOtaInit,                      /* 初始化 */
    eSmcuOtaUpgradeRequest,             /* 发送升级请求 */
    eSmcuOtaWaitACKInBootLoard,        /* 等待BootLoader响应permit */
    eSmcuOtaSendVer,                   /* 发送版本信息（总帧数） */
    eSmcuOtaWaitVerACK,                /* 等待版本信息ACK */
    eSmcuOtaReadFile,                  /* 从文件系统读取固件数据 */
    eSmcuOtaSendData,                  /* 发送升级数据包 */
    eSmcuOtaWaitDataACK,               /* 等待数据包ACK */
    eSmcuOtaSendDataEnd,               /* 发送最后一帧数据 */
    eSmcuOtaWaitMoveFlashACK,           /* 等待Flash搬运ACK */
    eSmcuOtaWaitResultACK,             /* 等待升级结果ACK */
    eSmcuOtaWaitACKTimeOut,            /* 等待ACK超时 */
} eSmcuOtaSta;

/* ============================================================
 * 【固定区】CAN消息接收结构体
 * ============================================================ */
typedef struct {
    uint8_t RecvFlag;                  /* 接收标志位（1=已接收到数据） */
    uint8_t BUFF[8];                  /* 接收数据缓冲区（8字节） */
} UpgradeCanMsg_t;

/* ============================================================
 * 【固定区】广播模式设备状态结构体
 * ============================================================ */
typedef struct {
    uint8_t Enable;                    /* 设备使能标志（1=参与升级） */
    uint8_t ReceiveFlag;               /* 接收响应标志（0=未响应, 1=成功, 2=升级中） */
} Broadcast;

/* ============================================================
 * 【固定区】软件定时器结构体
 * ============================================================ */
typedef struct {
    uint8_t  TimerOutFlag;             /* 超时标志位（1=已超时） */
    uint8_t  TimerStart;                /* 定时器启动标志（1=运行中） */
    uint16_t TimerCnt;                  /* 当前计时计数（每次+1） */
    uint16_t TimerOut;                  /* 超时阈值（TimerCnt>=TimerOut时超时） */
} Timer_t;

/* ============================================================
 * 【固定区】升级数据包结构体（共526字节）
 * ─────────────────────────────────────────────────────────────
 * 数据格式：每8字节插入1字节帧序号
 * 526字节 = 2(head1+head2) + 2(num) + 2(index_frame) + 2(total_frame)
 *          + 520(data[520]) + 2(checksum) + 2(null)
 *          = 526 字节 → 约 66 帧 CAN 帧
 * ─────────────────────────────────────────────────────────────
 */
typedef struct {
    uint8_t  head1;                    /* 帧头标识1（'S'） */
    uint8_t  head2;                    /* 帧头标识2（'S'） */
    uint16_t num;                      /* 有效数据大小（UPDATE_PACK_SIZE） */
    uint16_t index_frame;              /* 当前帧序号 */
    uint16_t total_frame;              /* 升级包总帧数 */
    /* data[520] = 每8字节插入1字节帧序号，共512字节有效数据 */
    uint8_t  data[UPDATE_PACK_SIZE + UPDATE_NUM_SIZE];
    uint16_t checksum;                 /* CRC16校验值 */
    uint16_t null[2];                   /* 保留字段 */
} upgrade_t;

/* ============================================================
 * 【固定区】OTA升级任务结构体
 * ============================================================ */
typedef struct {
    uint8_t  init_flag;                /* 初始化标志（1=主机升级, 2/3=从机升级, 4=排种器） */
    task_t   upgradeTask;             /* 升级任务参数 */
    Times_t  Timer1s;                  /* 升级定时器（1ms周期） */
    Times_t  SendTimer;                /* 发送定时器（预留） */
} upgradeTask_t;

/* ============================================================
 * 外部变量声明
 * ============================================================ */
extern upgradeTask_t s_upgradeTask;

/* ============================================================
 * 函数声明
 * ============================================================ */
/**
 * OTA 升级任务初始化
 * 功能：分配接收缓冲区 + 创建升级任务 + 创建软件定时器
 */
void UpgradeTask_Init(void);

/**
 * 广播模式升级主函数
 * @param ID  广播CAN ID（0x18FFA5D2）
 * @return    已响应设备数量
 */
uint8_t Upgrade_BroadcastMode(uint32_t ID);

/**
 * 设置指定设备的连接状态（是否参与OTA升级）
 * @param DeviceId  设备ID（行号，0~17）
 * @param state     使能状态（true=参与升级）
 */
void SetRowConnectState(uint8_t DeviceId, bool state);

#endif /* _UPGRADE_H_ */