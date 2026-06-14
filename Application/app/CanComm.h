/**
 * @file    CanComm.h
 * @brief   CAN0 通信模块 — 头文件（FreeRTOS 聚焦版）
 *
 * =============================================================================
 * 架构说明（简化版，删除了业务逻辑）
 * =============================================================================
 *
 * CAN0 通信模块演示了 FreeRTOS 中最典型的四大 RTOS 模式：
 *
 * 【模式一：ISR → 任务（队列）】
 *   CAN0_Message_IRQHandler（中断）
 *       ↓ xQueueSendToBackFromISR
 *   CanRecvTask_Process（任务）→ 解析消息
 *
 * 【模式二：定时器 → 任务（信号量）】
 *   CanSend_Timer（定时器回调）
 *       ↓ xSemaphoreGive
 *   CanSendTask_Process（任务）→ 发送消息
 *
 * 【模式三：定时器（看门狗）】
 *   Can_Timer1s（定时器回调）→ 检测通信超时
 *
 * 【模式四：互斥量（硬件保护）】
 *   Can0SendMsg() 中使用 xSemaphoreTake/Give 保护 CAN 硬件
 *
 * 硬件拓扑：
 *   上位机/显示终端 ──CAN0──► 主控 ──CAN1──► 下层设备（电机驱动单元）
 * =============================================================================
 */

#ifndef __CAN_COMM_H__
#define __CAN_COMM_H__

/* ============================================================
 * CAN 消息 ID（保持原始编号，业务相关词已删除）
 * ============================================================ */
#define ID_eSetParam1          0x01F5   /* 基本参数设置1 */
#define ID_eSetParam2          0x02F5   /* 播种行使能设置 */
#define ID_eSetParam3          0x03F5   /* 标定/目标/开关参数 */
#define ID_eSetParam4          0x04F5   /* 排种设定 */
#define ID_eSetParam5          0x05F5   /* 控制指令 */
#define ID_eSetParam6          0x06F5   /* 高级参数 */
#define ID_eSetParam7          0x07F5   /* 亩施肥量 */
#define ID_eSetParam8          0x08F5   /* 各行株距 */
#define ID_eFillSeed           0x09F5   /* 补种控制 */
#define ID_eGPSSpeed           0x0AF5   /* GPS 速度 */
#define ID_eTestSpeed          0x0BF5   /* 模拟速度 */
#define ID_eGPSArea            0x0CF5   /* GPS 面积 */
#define ID_eShiFeiMotor        0x0DF5   /* 施肥电机使能/方向 */
#define ID_eTotalFV            0x10F5   /* 固件版本 */
#define ID_eQueryFV            0x11F5   /* 固件版本查询 */
#define ID_eFirmwareUpgrade    0x1FF5    /* 固件升级触发 */

/* 上位机下行消息 ID（发送方向：上位机 → 主控） */
#define ID_CtrlType      0x18FF01D2   /* 控制类型标识 */
#define ID_WorkState     0x18FF02D2   /* 作业状态 */
#define ID_Area          0x18FF03D2   /* 作业面积 */
#define ID_Info          0x18FF04D2   /* 工作信息 */
#define ID_SeedNumber    0x18FF05D2   /* 排种数量 */
#define ID_Fertilizing    0x18FF06D2   /* 施肥量 */

/* ============================================================
 * CAN0 通信模块结构体
 * 封装了 CAN0 通信所需的所有 FreeRTOS 资源
 * ============================================================ */
typedef struct {
    task_t          RecvTask;       /* 接收任务参数 */
    task_t          SendTask;       /* 发送任务参数 */
    Times_t         SendTimer;      /* 发送节拍定时器 */
    Times_t         Timer1s;        /* 1s 超时看门狗定时器 */
    QueueHandle_t   RecvQueue;      /* 接收消息队列 */
    SemaphoreHandle_t sembHandle;   /* 发送信号量（二值） */
} CanComm_t;

/* ============================================================
 * 外部声明
 * ============================================================ */
extern CanComm_t s_CanComm;

/* ============================================================
 * 函数声明
 * ============================================================ */
/**
 * CAN0 通信模块初始化
 * 在 main.c 的 Step 12（核心自定义区）中调用
 *
 * 创建内容：
 *   1. 接收任务（CanRecvTask_Process）
 *   2. 发送任务（CanSendTask_Process）
 *   3. 接收消息队列（CAN0 → 任务）
 *   4. 发送节拍定时器（500ms 周期）
 *   5. 1s 超时定时器（通信看门狗）
 *   6. 发送信号量（定时器 → 任务）
 */
void CanTaskInit(void);

#endif /* __CAN_COMM_H__ */