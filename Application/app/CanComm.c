/**
 * @file    CanComm.c
 * @brief   CAN0 通信模块 — 实现文件（FreeRTOS 聚焦版）
 *
 * =============================================================================
 * ⚠️【重要】此文件仅保留 FreeRTOS 编程范式，删除了所有业务逻辑代码。
 * 所有消息解析分支（case ...）中的具体业务处理已替换为占位注释，
 * 指示"在哪里添加自定义解析逻辑"。
 *
 * 业务相关代码已全部删除。
 * CAN ID 和帧格式保持不变，以展示真实车载通信场景。
 * =============================================================================
 *
 * =============================================================================
 * 四大 FreeRTOS 模式在这个文件中的体现
 * =============================================================================
 *
 * 【模式 A：ISR → 任务（队列）— CAN 数据接收】
 *
 *   硬件 CAN0 中断（CAN0_Message_IRQHandler）
 *       │
 *       ├── taskENTER_CRITICAL_FROM_ISR()         ← 进入临界段
 *       ├── can_rx_fifo_read(CAN0, &fifo)          ← 读取硬件 FIFO
 *       ├── xQueueSendToBackFromISR(queue, &msg)   ← ISR 安全发送
 *       └── portYIELD_FROM_ISR(woken)              ← 触发 PendSV 切换
 *       │
 *       ▼
 *   CanRecvTask_Process（任务）
 *       ├── xQueueReceive(queue, &msg)             ← 阻塞等待数据
 *       ├── switch(msg.id)                         ← 按 ID 分发
 *       └── 解析 + 业务处理
 *
 * 【模式 B：定时器 → 任务（信号量）— 周期发送】
 *
 *   CanSend_Timer（定时器回调，500ms 周期）
 *       ├── xSemaphoreGive(sembHandle)            ← 释放信号量
 *       └── 定时器自动重载
 *       │
 *       ▼
 *   CanSendTask_Process（任务）
 *       ├── xSemaphoreTake(sembHandle)             ← 阻塞等待信号量
 *       ├── SendMsg()                              ← 发送 CAN 帧
 *       └── 延时（5ms 间隔）+ 循环
 *
 * 【模式 C：定时器（看门狗）— 超时检测】
 *
 *   Can_Timer1s（定时器回调，1s 周期）
 *       ├── 累加超时计数器
 *       └── 超时时触发报警/复位
 *
 * 【模式 D：互斥量 — 硬件发送保护】
 *
 *   Can0SendMsg() / Can1SendMsg()
 *       ├── xSemaphoreTake(mutex)                  ← 获取互斥量
 *       ├── 发送 CAN 帧
 *       └── xSemaphoreGive(mutex)                  ← 释放互斥量
 *
 * =============================================================================
 * 程序运行流程（按步骤执行，顺序固定）
 * =============================================================================
 *
 * 【阶段一：初始化】（CanTaskInit，在 main.c Step 12 中调用）
 *
 * Step 1  ──► 配置接收任务参数（名字/栈/优先级）
 * Step 2  ──► 配置发送任务参数（名字/栈/优先级）
 * Step 3  ──► xTaskCreate(CanRecvTask_Process) ──► 创建接收任务
 * Step 4  ──► xTaskCreate(CanSendTask_Process) ──► 创建发送任务
 * Step 5  ──► xQueueCreate ──► 创建接收消息队列（CAN0 → 任务）
 * Step 6  ──► xTimerCreate(CanSend_Timer) ──► 创建发送节拍定时器
 * Step 7  ──► xTimerCreate(Can_Timer1s) ──► 创建 1s 超时定时器
 * Step 8  ──► xSemaphoreCreateBinary ──► 创建发送信号量
 * Step 9  ──► 返回 main()，之后 main() 调用 __enable_irq + vTaskStartScheduler
 *
 * 【阶段二：调度器启动后，任务开始运行】
 *
 * 【接收任务运行流程】（CanRecvTask_Process）
 *
 * Step 10 ──► xTimerStart(Timer1s) ──► 启动 1s 看门狗定时器
 * Step 11 ──► while(1) 主循环开始
 * Step 12 ──► xQueueReceive(RecvQueue, &msg, portMAX_DELAY) ──► 阻塞等待 CAN 数据
 * Step 13 ──► 收到数据 → 重置超时计数器（清除报警标志）
 * Step 14 ──► switch(msg.id) ──► 按 CAN ID 分发
 * Step 15 ──► case 0x01F5~0x11F5：各参数设置消息解析（【自定义解析逻辑】）
 * Step 16 ──► 其他处理分支（【自定义解析逻辑】）
 * Step 17 ──► 返回 Step 11，继续等待下一条消息
 *
 * 【发送任务运行流程】（CanSendTask_Process）
 *
 * Step 18 ──► xSemaphoreTake(sembHandle, portMAX_DELAY) ──► 等待定时器信号
 * Step 19 ──► while(sendMsgType <= eMsgEnd) ──► 循环发送所有类型消息
 * Step 20 ──►   SendMsg(ID_CtrlType, data) ──► 发送一帧 CAN 数据
 * Step 21 ──►   vTaskDelay(CAN_SEND_INTERVAL) ──► 延时 5ms（避免总线拥塞）
 * Step 22 ──►   sMsgType++ ──► 切换到下一个消息类型
 * Step 23 ──► sMsgType = eMsgInit ──► 一轮发完，重置类型计数器
 * Step 24 ──► vTaskDelay(发送周期调整) ──► 等待下一个发送周期
 * Step 25 ──► 返回 Step 18，等待下一个定时器触发
 *
 * 【发送定时器回调】（CanSend_Timer，每 500ms 执行一次）
 *
 * Step 26 ──► xSemaphoreGive(sembHandle) ──► 释放信号量，唤醒发送任务
 * Step 27 ──► 定时器自动重载，下一个 500ms 再次触发
 *
 * 【1s 超时定时器回调】（Can_Timer1s）
 *
 * Step 28 ──► 累加超时计数器
 * Step 29 ──► 超过阈值 → 触发通信超时报警
 * Step 30 ──► 超时 → CAN0 软件复位
 * Step 31 ──► 上电 30s 延迟保护（启动阶段不报警）
 *
 * 【CAN 发送硬件保护】（Can0SendMsg / Can1SendMsg）
 *
 * Step 32 ──► xSemaphoreTake(CAN0_Mutex, portMAX_DELAY) ──► 获取互斥量
 * Step 33 ──► can_filter_config() ──► 配置 CAN 发送过滤器
 * Step 34 ──► can_message_transmit() ──► 发送 CAN 帧
 * Step 35 ──► xSemaphoreGive(CAN0_Mutex) ──► 释放互斥量
 *
 * =============================================================================
 * 【核心自定义区】如何修改这个文件
 * =============================================================================
 *   Step 15 — 在各 case 分支中添加自定义消息解析逻辑
 *   Step 20 — 在 SendMsg() 中填充自定义发送数据
 *   Step 31 — 调整超时阈值或报警方式
 *   修改 SEND_PERIOD / CAN_SEND_INTERVAL 调整发送频率
 *   修改 Timer1s 周期调整超时检测灵敏度
 * =============================================================================
 */

#include "CanComm.h"
#include "debug.h"

/* ============================================================
 * 常量定义（调整参数）
 * ============================================================ */
/**
 * 发送大周期：500ms
 * 定时器每 500ms 触发一次，发送任务循环发送所有消息类型
 */
#define SEND_PERIOD                500

/**
 * 发送消息队列最大长度
 * 接收任务从 CAN0 中断接收数据，最大缓冲 8 条
 */
#define CAN_MSG_QUEUE_LEN          8

/**
 * 1s 定时器周期（固定 1 秒，用于通信超时看门狗）
 */
#define CAN_TIME_1S_TICK           (1000 / portTICK_PERIOD_MS)

/**
 * 发送节拍定时器周期（动态调整，用于控制发送频率）
 */
#define CAN_SEND_TIMER_TICK        (can_send_period)

/**
 * CAN 消息间隔：5ms
 * 每发送一条消息后延时 5ms 再发下一条，避免总线拥塞
 */
#define CAN_SEND_INTERVAL          (5 * portTICK_PERIOD_MS / portTICK_PERIOD_MS)

/**
 * 发送定时器 ID（用于在回调中区分多个定时器）
 */
#define CAN_SEND_TIMER_ID         1

/**
 * 1s 超时定时器 ID
 */
#define CAN_TIME_1S_ID             2

/* ============================================================
 * 静态变量
 * ============================================================ */
/**
 * 发送大周期（tick 数，初始 500ms）
 * 可以在运行时动态调整
 */
static uint16_t can_send_period = SEND_PERIOD / portTICK_PERIOD_MS;

/**
 * CAN0 通信模块全局实例
 * 在 main.c 中声明为 extern，在本文件中定义
 */
CanComm_t s_CanComm = {0};

/**
 * 发送消息类型枚举
 * 定义发送任务每一轮循环中依次发送的消息类型
 *
 * ⚠️ 框架固定区：不要改动枚举的顺序！
 *   发送任务依赖这些枚举值按顺序循环发送。
 *   添加新消息类型时，在 eMsgEnd 之前插入。
 */
typedef enum {
    eMsgInit = 0,         /* 初始/空闲状态 */
    eCtrlType = 1,        /* 控制类型标识 */
    eWorkState,           /* 作业状态 */
    eArea,                /* 作业面积 */
    eInfo,                /* 工作信息 */
    eSeedNumber,          /* 排种数量 */
    eFertilizing,         /* 施肥量 */

    eMsgEnd = eFertilizing, /* 枚举结束标记（当前最后一种消息） */
} eSendMsg;

/* ============================================================
 * Step 32~35 — CAN 发送函数（互斥量保护）
 * ============================================================ */
/**
 * 通过 CAN0 发送消息（互斥量保护）
 *
 * Step 32 ──► xSemaphoreTake(CAN0_Mutex) ──► 获取互斥量
 *   - 等待 CAN0 总线空闲（如果其他任务正在使用）
 *   - portMAX_DELAY = 无限等待
 *
 * Step 33 ──► can_filter_config() ──► 配置发送过滤器
 *
 * Step 34 ──► can_message_transmit() ──► 发送 CAN 帧
 *
 * Step 35 ──► xSemaphoreGive(CAN0_Mutex) ──► 释放互斥量
 *
 * ⚠️ 互斥量确保同一时刻只有一个任务使用 CAN0 总线
 *   防止多任务同时发送导致 CAN 总线冲突
 */
extern SemaphoreHandle_t CAN0_MutexHandle;  /* 在 bsp_can.c 中定义 */
extern SemaphoreHandle_t CAN1_MutexHandle;  /* 在 bsp_can.c 中定义 */

static void SendMsg(uint32_t ID, uint8_t *pdata, uint8_t dataLen)
{
    if (CAN0_MutexHandle != NULL) {
        /* Step 32 ──► 获取互斥量 */
        xSemaphoreTake(CAN0_MutexHandle, portMAX_DELAY);
    }

    /* Step 33 ──► 发送 CAN 帧 */
    Can0SendMsg(ID, 1, pdata, dataLen);

    if (CAN0_MutexHandle != NULL) {
        /* Step 35 ──► 释放互斥量 */
        xSemaphoreGive(CAN0_MutexHandle);
    }
}

/* ============================================================
 * Step 10~17 — 接收任务主循环
 * ============================================================ */
/**
 * CAN0 接收任务
 *
 * 功能：从消息队列中读取 CAN0 接收到的数据帧，按 CAN ID 解析。
 * 删除了原始业务逻辑，仅保留解析框架。
 *
 * 消息 ID 与处理说明：
 *   0x01F5 — 基本参数（行距/行数/音量/静音）
 *   0x02F5 — 播种行使能掩码
 *   0x03F5 — 标定/目标/开关参数
 *   0x04F5 — 排种设定
 *   0x05F5 — 控制指令（标定按钮/开始作业/速度来源/报警）
 *   0x06F5 — 高级参数（地轮直径/株距比例）
 *   0x07F5 — 亩施肥量
 *   0x08F5 — 各行株距
 *   0x09F5 — 补种控制
 *   0x0AF5 — GPS 速度
 *   0x0BF5 — 模拟速度
 *   0x0CF5 — GPS 面积
 *   0x0DF5 — 施肥电机使能/方向
 *   0x10F5 / 0x11F5 — 固件版本查询
 *   0x1FF5 — 固件升级触发
 *
 * ⚠️ 框架固定区：xQueueReceive 和超时计数器逻辑不要改动！
 *   在 Step 15 的各 case 分支中添加自定义解析逻辑。
 */
static void CanRecvTask_Process(void *param)
{
    (void)param;

    /* Step 10 ──► 启动 1s 超时看门狗定时器 */
    xTimerStart(s_CanComm.Timer1s.pTimer_Handle, portMAX_DELAY);

    BaseType_t Status = pdFALSE;
    CAN_Msg_t msg = {0};

    while (1) {
        /* Step 12 ──► 从队列中读取 CAN 消息（阻塞等待） */
        Status = xQueueReceive(s_CanComm.RecvQueue, &msg, portMAX_DELAY);

        if (Status != pdPASS) {
            continue;
        }

        /* Step 13 ──► 收到数据 → 重置超时计数器（清除通信超时报警） */
        extern uint8_t s_LowVoltageClr_CanErrTime;  /* 来自系统参数 */
        extern uint8_t s_LowVoltageClr_CanErrTimeCnt;
        extern uint8_t Timer_1s_Delay;

        s_LowVoltageClr_CanErrTimeCnt = 0;  /* 重置超时计数器 */

        if (msg.ide == 1) {  /* 扩展帧 */
            switch (msg.id) {

                /* ────────────────────────────────────────────
                 * Step 15 — 【核心自定义区】参数解析分支
                 * ────────────────────────────────────────────
                 * 原始代码中这里有大量业务逻辑（解析参数、更新全局结构体）。
                 * 现已删除，仅保留框架注释，指示如何添加自定义解析。
                 *
                 * 格式：case ID:
                 *         // 解析 msg.data[0]~msg.data[7]
                 *         // 更新全局参数或触发业务处理
                 *         break;
                 * ──────────────────────────────────────────── */

                case ID_eSetParam1: {
                    /* 基本参数设置1（0x01F5）
                     * data[2]<<8 | data[3] — 行距
                     * data[4] — 行数
                     * data[5] — 清零标志
                     * data[6] — 音量
                     * data[7] — 静音标志
                     *
                     * 【自定义解析逻辑示例】
                     * s_SetParam.hangJu = msg.data[2] << 8 | msg.data[3];
                     * s_SetParam.hangShu = msg.data[4];
                     * if (msg.data[5] == 1) { ClearStatistics(); }
                     * s_SetParam.Vol = msg.data[6];
                     * s_SetParam.jingYin = msg.data[7];
                     */
                    break;
                }

                case ID_eSetParam2: {
                    /* 播种行使能掩码设置（0x02F5）
                     * data[0]~data[3] — 32 位使能掩码（播种行 bit-map）
                     *
                     * 【自定义解析逻辑示例】
                     * uint32_t mask = msg.data[3] << 24 | msg.data[2] << 16 |
                     *                  msg.data[1] << 8  | msg.data[0];
                     * if (mask != s_SetParam.u32boZhongHang) {
                     *     UpdateRowEnableMask(mask);
                     * }
                     */
                    break;
                }

                case ID_eSetParam3: {
                    /* 标定/目标/开关参数（0x03F5）
                     * data[0]<<8|data[1] — 标定施肥量（克/亩）
                     * data[2]<<8|data[3] — 目标施肥量（亩/播）
                     * data[4]<<8|data[5] — 目标播种量（粒/亩）
                     * data[6] — 种盘盘孔数
                     * data[7] — 位域（施肥开关/排种异常检测/种子类型/灵敏度）
                     *
                     * 【自定义解析逻辑示例】
                     * s_SetParam.u16biaoDingFeiLiang = msg.data[0] << 8 | msg.data[1];
                     * Var_Param.target_BaseFat_mu_p = msg.data[2] << 8 | msg.data[3];
                     * Var_Param.target_Seed_mu_p = msg.data[4] << 8 | msg.data[5];
                     * s_SetParam.seed_plate_num = msg.data[6];
                     * s_SetParam.BaseFatSwitch = msg.data[7] & 0x01;
                     * s_SetParam.Sensitivity = msg.data[7] >> 4;
                     */
                    break;
                }

                case ID_eSetParam4: {
                    /* 排种设定（0x04F5）
                     * data[0]<<8|data[1] — 排种设定值
                     * data[2]<<8|data[3] — 目标播种量
                     */
                    break;
                }

                case ID_eSetParam5: {
                    /* 控制指令（0x05F5）
                     * data[0] — 标定按钮（0=停止, 1=开始, 2=停止标定）
                     * data[1] — 开始/停止作业
                     * data[2] — 清零 EEPROM
                     * data[3] — 控制器类型
                     * data[4] — 施肥图例选择
                     * data[5] — 复位报警
                     * data[6] — 速度来源（0=雷达, 1=GNSS, 2=地轮, 3=模拟）
                     *
                     * 【自定义解析逻辑示例】
                     * Var_Param.Cal_curr_flag = msg.data[0];
                     * Var_Param.WS_flag = msg.data[1];
                     * if (msg.data[2] == 1) { ClearEepromData(); }
                     * s_SetParam.ControllerType = msg.data[3];
                     * s_SetParam.ResetAlarm = msg.data[5];
                     * sSpeedAndAreaCtrl.setSpeedFrom = msg.data[6];
                     */
                    break;
                }

                case ID_eSetParam6: {
                    /* 高级参数（0x06F5）
                     * data[0] — 侧速迟数
                     * data[1] — 地轮直径
                     * data[2] — 株距比例
                     * data[3] — 施肥比例
                     */
                    break;
                }

                case ID_eGPSSpeed: {
                    /* GPS 速度（0x0AF5）
                     * data[0]~data[1] — 速度值（km/h，放大10倍）
                     *
                     * 【自定义解析逻辑示例】
                     * uint16_t speed_x10 = msg.data[0] << 8 | msg.data[1];
                     * float speed = speed_x10 / 10.0f;
                     * UpdateSpeed(speed);
                     */
                    break;
                }

                case ID_eTestSpeed: {
                    /* 模拟速度（0x0BF5）
                     * data[0]~data[1] — 模拟速度值（km/h，放大10倍）
                     */
                    break;
                }

                case ID_eGPSArea: {
                    /* GPS 面积（0x0CF5）
                     * data[0]~data[3] — 累计面积（平方米）
                     */
                    break;
                }

                case ID_eShiFeiMotor: {
                    /* 施肥电机使能/方向（0x0DF5）
                     * data[0] bit0 — 使能位
                     * data[0] bit1 — 方向位
                     */
                    break;
                }

                case ID_eFillSeed: {
                    /* 补种控制（0x09F5）
                     * data[0] — 补种使能
                     * data[1] — 补种行号
                     */
                    break;
                }

                case ID_eTotalFV:
                case ID_eQueryFV: {
                    /* 固件版本查询（0x10F5 / 0x11F5）
                     * 返回固件版本信息
                     *
                     * 【自定义解析逻辑示例】
                     * SendFirmwareVersion();
                     */
                    break;
                }

                /* 固件升级触发（0x18FFA1D2~0x18FFA4D2）
                 * 进入固件升级模式
                 */

                default:
                    break;
            }
        }
        /* 循环回到 Step 11，等待下一条 CAN 消息 */
    }
}

/* ============================================================
 * Step 18~25 — 发送任务主循环
 * ============================================================ */
/**
 * CAN0 发送任务
 *
 * 功能：在定时器触发时，循环发送多类型状态帧到上位机。
 *
 * Step 18 ──► xSemaphoreTake(sembHandle) ──► 等待定时器信号（阻塞）
 * Step 19 ──► while(sMsgType <= eMsgEnd) ──► 循环发送所有类型消息
 * Step 20 ──►   SendMsg(ID_XXX, data) ──► 发送 CAN 帧
 * Step 21 ──►   vTaskDelay(CAN_SEND_INTERVAL) ──► 延时 5ms（避免总线拥塞）
 * Step 22 ──►   sMsgType++ ──► 切换下一个消息类型
 * Step 23 ──► sMsgType = eMsgInit ──► 一轮发完，重置类型
 * Step 24 ──► 返回 Step 18，等待下一个定时器触发
 *
 * ⚠️ 框架固定区：定时器信号量等待和循环发送逻辑不要改动！
 *   在 Step 20 的 SendMsg() 调用中填充自定义发送数据。
 */
static void CanSendTask_Process(void *param)
{
    (void)param;

    eSendMsg sMsgType = eMsgInit;

    while (1) {
        /* Step 18 ──► 等待定时器信号量（阻塞等待，直到 CanSend_Timer 触发） */
        xSemaphoreTake(s_CanComm.sembHandle, portMAX_DELAY);

        /* Step 19 ──► 循环发送所有类型的消息 */
        while (sMsgType <= eMsgEnd) {
            uint8_t data[8] = {0};

            /* Step 20 ──► 根据消息类型填充数据并发送
             * ────────────────────────────────────────────
             * 原始代码中这里填充具体的业务状态数据。
             * 现已删除，仅保留框架注释。
             *
             * 【自定义发送数据示例】
             * switch (sMsgType) {
             *     case eCtrlType:
             *         data[0] = g_sysState.ctrlType;
             *         data[1] = g_sysState.workMode;
             *         SendMsg(ID_CtrlType, data, 8);
             *         break;
             *     case eWorkState:
             *         // 填充作业状态数据
             *         SendMsg(ID_WorkState, data, 8);
             *         break;
             *     // ... 其他消息类型
             * }
             * ──────────────────────────────────────────── */

            /* 占位：当前仅演示发送空帧结构 */
            SendMsg(0, data, 8);  /* 占位符，需替换为实际 ID 和数据 */

            /* Step 21 ──► 延时 5ms 再发下一条（防止总线拥塞） */
            vTaskDelay(CAN_SEND_INTERVAL);

            /* Step 22 ──► 切换到下一个消息类型 */
            sMsgType = (eSendMsg)(sMsgType + 1);
        }

        /* Step 23 ──► 一轮发送完成，重置类型计数器 */
        sMsgType = eMsgInit;

        /* Step 24 ──► 等待下一个 500ms 周期 */
        vTaskDelay(can_send_period);
    }
}

/* ============================================================
 * Step 26~27 — 发送节拍定时器回调
 * ============================================================ */
/**
 * CAN0 发送节拍定时器回调（500ms 周期，自动重载）
 *
 * 功能：每 500ms 触发一次，释放发送信号量，唤醒发送任务。
 *
 * Step 26 ──► xSemaphoreGive(sembHandle) ──► 释放信号量
 * Step 27 ──► 定时器自动重载，下一个 500ms 再次触发
 *
 * ⚠️ 框架固定区：信号量释放逻辑不要改动！
 *   可以修改 can_send_period 动态调整发送周期。
 */
static void CanSend_Timer(TimerHandle_t xTimer)
{
    (void)xTimer;

    /* Step 26 ──► 释放信号量，唤醒发送任务 */
    xSemaphoreGive(s_CanComm.sembHandle);
}

/* ============================================================
 * Step 28~31 — 1s 超时看门狗定时器回调
 * ============================================================ */
/**
 * CAN0 1s 超时看门狗定时器
 *
 * 功能：每 1 秒执行一次，检测 CAN0 通信是否超时。
 * 如果超过设定时间未收到数据，触发通信超时报警和 CAN0 复位。
 *
 * Step 28 ──► 累加超时计数器
 * Step 29 ──► 超过阈值 → 触发通信超时报警
 * Step 30 ──► 超时 → CAN0 软件复位 + 重新初始化
 * Step 31 ──► 上电 30s 延迟保护（启动阶段不报通信故障）
 *
 * ⚠️ 框架固定区：计数器累加和超时检测逻辑不要改动！
 *   可以修改超时阈值（s_LowVoltageClr.CanErrTime）调整灵敏度。
 */
static void Can_Timer1s(TimerHandle_t xTimer)
{
    (void)xTimer;

    extern uint8_t s_LowVoltageClr_CanErrTime;
    extern uint8_t s_LowVoltageClr_CanErrTimeCnt;
    extern uint8_t Timer_1s_Delay;

    /* Step 28 ──► 累加超时计数器 */
    if (++s_LowVoltageClr_CanErrTimeCnt > s_LowVoltageClr_CanErrTime) {
        s_LowVoltageClr_CanErrTimeCnt = 0;

        /* Step 31 ──► 上电 30s 延迟保护 */
        if (Timer_1s_Delay > 30) {
            /* Step 29 ──► 超过阈值 → 触发通信超时报警
             * SetCanCommErr(1, ...) 应由调用者定义
             */
            /* SetCanCommErr(1, AUDIO_LEVEL_NORMAL, PLAY_CNT_MAX, SET_ALARM_TIMER_NOMAL); */
        }

        /* Step 30 ──► CAN0 软件复位
         * 非调试模式下自动复位并重新初始化 CAN0 总线
         */
#if (DEBUG_MODE == 0)
        if (can_software_reset(CAN0)) {
            CAN0_Init(CAN_250K_BPS);
        }
#endif
    }

    /* 上电延迟计数器递增 */
    if (Timer_1s_Delay++ >= 255) {
        Timer_1s_Delay = 255;
    }

    /* 上电前 30 秒内自动复位报警 */
    if (Timer_1s_Delay < 30) {
        extern uint8_t s_SetParam_ResetAlarm;
        s_SetParam_ResetAlarm = 1;
    }
}

/* ============================================================
 * Step 1~9 — CAN 通信模块初始化（在 main.c Step 12 中调用）
 * ============================================================ */
/**
 * CAN0 通信模块初始化
 *
 * 功能：创建 CAN0 通信所需的全部 FreeRTOS 资源。
 * 调用位置：main.c 的 Step 12（在 __enable_irq 和 vTaskStartScheduler 之前）
 *
 * 创建顺序（固定，不要打乱）：
 *   Step 1 → Step 2 → Step 3 → Step 4 → Step 5 → Step 6 → Step 7 → Step 8 → Step 9
 *
 * ⚠️ 重要：所有 RTOS 资源必须在 vTaskStartScheduler() 之前创建！
 *
 * 创建内容：
 *   接收任务（CanRecvTask_Process）
 *   发送任务（CanSendTask_Process）
 *   接收消息队列（CAN0 中断 → 任务）
 *   发送节拍定时器（500ms 周期）
 *   1s 超时定时器（通信看门狗）
 *   发送信号量（二值，Timer → Task 同步）
 */
void CanTaskInit(void)
{
    BaseType_t ret;

    /* ──────────────────────────────────────────────────────
     * Step 1 — 配置接收任务参数
     * ──────────────────────────────────────────────────────
     * 接收任务优先级：osPriorityRealtime = 25（实时优先级）
     * 栈深度：configMINIMAL_STACK_SIZE × 2（256 word = 1KB）
     *
     * 【核心自定义区】可调整优先级和栈大小
     */
    s_CanComm.RecvTask.Task_name = "CanRecvTask";
    s_CanComm.RecvTask.Task_priority = osPriorityRealtime;
    s_CanComm.RecvTask.Task_stackDepth = configMINIMAL_STACK_SIZE * 2;

    /* ──────────────────────────────────────────────────────
     * Step 2 — 配置发送任务参数
     * ──────────────────────────────────────────────────────
     * 发送任务优先级：osPriorityRealtime1 = 26（比接收任务稍低）
     * 栈深度：configMINIMAL_STACK_SIZE × 3（384 word = 1.5KB）
     *
     * 【核心自定义区】可调整优先级和栈大小
     */
    s_CanComm.SendTask.Task_name = "CanSendTask";
    s_CanComm.SendTask.Task_priority = osPriorityRealtime1;
    s_CanComm.SendTask.Task_stackDepth = configMINIMAL_STACK_SIZE * 3;

    /* ──────────────────────────────────────────────────────
     * Step 3 — 创建接收任务
     * ──────────────────────────────────────────────────────
     * xTaskCreate(TaskFunction, pcName, usStackDepth,
     *             pvParameters, uxPriority, pxCreatedTask)
     *   - TaskFunction = CanRecvTask_Process
     *   - pcName       = "CanRecvTask"
     *   - usStackDepth = 256 word
     *   - uxPriority   = osPriorityRealtime = 25
     *
     * ⚠️ 框架固定区：不要改动 xTaskCreate 的调用位置！
     *   必须在所有 RTOS 资源创建完成后才能启动调度器。
     */
    ret = xTaskCreate(
        CanRecvTask_Process,
        s_CanComm.RecvTask.Task_name,
        s_CanComm.RecvTask.Task_stackDepth,
        NULL,
        s_CanComm.RecvTask.Task_priority,
        &s_CanComm.RecvTask.Task_handle
    );

    if (ret != pdPASS) {
        debug_printf(INFO_ERR, "CanRecvTask create fail\r\n");
        configASSERT(ret);
    }

    /* ──────────────────────────────────────────────────────
     * Step 4 — 创建发送任务
     * ──────────────────────────────────────────────────────
     * ⚠️ 框架固定区：同上
     */
    ret = xTaskCreate(
        CanSendTask_Process,
        s_CanComm.SendTask.Task_name,
        s_CanComm.SendTask.Task_stackDepth,
        NULL,
        s_CanComm.SendTask.Task_priority,
        &s_CanComm.SendTask.Task_handle
    );

    if (ret != pdPASS) {
        debug_printf(INFO_ERR, "CanSendTask create fail\r\n");
        configASSERT(ret);
    }

    /* ──────────────────────────────────────────────────────
     * Step 5 — 创建接收消息队列
     * ──────────────────────────────────────────────────────
     * xQueueCreate(uxQueueLength, uxItemSize)
     *   - uxQueueLength = CAN_MSG_QUEUE_LEN = 8（最大缓冲 8 条 CAN 帧）
     *   - uxItemSize    = sizeof(CAN_Msg_t)（每条消息的大小）
     *
     * ⚠️ 框架固定区：
     *   - 队列在任务创建之后创建（无顺序要求）
     *   - 队列大小根据 CAN 数据量和处理速度调整
     *   - 如果队列经常满，说明接收任务处理不及时，可以增大队列或优化任务处理速度
     *
     * 【核心自定义区】可调整队列长度和消息大小
     */
    s_CanComm.RecvQueue = xQueueCreate(CAN_MSG_QUEUE_LEN, sizeof(CAN_Msg_t));

    if (s_CanComm.RecvQueue == NULL) {
        debug_printf(INFO_ERR, "CanRecv Queue create fail\r\n");
        configASSERT(s_CanComm.RecvQueue);
    }

    /* ──────────────────────────────────────────────────────
     * Step 6 — 创建发送节拍定时器
     * ──────────────────────────────────────────────────────
     * xTimerCreate(pcTimerName, xTimerPeriodInTicks, uxAutoReload,
     *              pvTimerID, pxCallbackFunction)
     *   - pcTimerName         = "CanSendTimer"
     *   - xTimerPeriodInTicks = CAN_SEND_TIMER_TICK = 500（500ms）
     *   - uxAutoReload        = pdTRUE（自动重载）
     *   - pvTimerID           = &s_CanComm.SendTimer.Timer_ID
     *   - pxCallbackFunction  = CanSend_Timer
     *
     * ⚠️ 框架固定区：定时器创建后不会自动启动！
     *   定时器必须在调度器启动后由任务或主函数调用 xTimerStart()。
     *   这里先创建，稍后在接收任务中调用 xTimerStart(Timer1s)。
     *
     * 【核心自定义区】可调整定时器周期（修改 CAN_SEND_TIMER_TICK）
     */
    s_CanComm.SendTimer.Timer_name = "CanSendTimer";
    s_CanComm.SendTimer.Timer_Tick = CAN_SEND_TIMER_TICK;
    s_CanComm.SendTimer.Timer_State = pdTRUE;   /* 自动重载 */
    s_CanComm.SendTimer.Timer_ID = CAN_SEND_TIMER_ID;

    s_CanComm.SendTimer.pTimer_Handle = xTimerCreate(
        s_CanComm.SendTimer.Timer_name,
        s_CanComm.SendTimer.Timer_Tick,
        s_CanComm.SendTimer.Timer_State,
        &s_CanComm.SendTimer.Timer_ID,
        CanSend_Timer
    );

    if (s_CanComm.SendTimer.pTimer_Handle == NULL) {
        debug_printf(INFO_ERR, "CanSendTimer create fail\r\n");
        configASSERT(s_CanComm.SendTimer.pTimer_Handle);
    }

    /* ──────────────────────────────────────────────────────
     * Step 7 — 创建 1s 超时定时器
     * ──────────────────────────────────────────────────────
     * xTimerCreate(...)
     *   - pcTimerName         = "canTimer1s"
     *   - xTimerPeriodInTicks = CAN_TIME_1S_TICK = 1000（1000ms = 1s）
     *   - uxAutoReload        = pdTRUE（自动重载）
     *   - pvTimerID           = &s_CanComm.Timer1s.Timer_ID
     *   - pxCallbackFunction  = Can_Timer1s
     *
     * 【核心自定义区】可调整超时阈值（修改 s_LowVoltageClr.CanErrTime）
     */
    s_CanComm.Timer1s.Timer_name = "canTimer1s";
    s_CanComm.Timer1s.Timer_ID = CAN_TIME_1S_ID;
    s_CanComm.Timer1s.Timer_State = pdTRUE;
    s_CanComm.Timer1s.Timer_Tick = CAN_TIME_1S_TICK;

    s_CanComm.Timer1s.pTimer_Handle = xTimerCreate(
        s_CanComm.Timer1s.Timer_name,
        s_CanComm.Timer1s.Timer_Tick,
        s_CanComm.Timer1s.Timer_State,
        &s_CanComm.Timer1s.Timer_ID,
        Can_Timer1s
    );

    if (s_CanComm.Timer1s.pTimer_Handle == NULL) {
        debug_printf(INFO_ERR, "canTimer1s create fail\r\n");
        configASSERT(s_CanComm.Timer1s.pTimer_Handle);
    }

    /* ──────────────────────────────────────────────────────
     * Step 8 — 创建发送信号量（二值）
     * ──────────────────────────────────────────────────────
     * xSemaphoreCreateBinary()
     *   - 初始状态：不可用（信号量 = 0）
     *   - 定时器回调 Give 后：可用（信号量 = 1）
     *   - 任务 Take 后：恢复不可用（信号量 = 0）
     *
     * ⚠️ 框架固定区：二值信号量是 Timer → Task 同步的核心机制，不要改动！
     *
     * 【核心自定义区】如需 ISR → Task 同步，改用 xSemaphoreCreateBinary()
     *   并在 ISR 中调用 xSemaphoreGiveFromISR()
     */
    s_CanComm.sembHandle = xSemaphoreCreateBinary();

    if (s_CanComm.sembHandle == NULL) {
        debug_printf(INFO_ERR, "CanSemaphore create fail\r\n");
        configASSERT(s_CanComm.sembHandle);
    }

    /* ──────────────────────────────────────────────────────
     * Step 9 — 初始化完成，返回 main.c
     * ──────────────────────────────────────────────────────
     * 发送节拍定时器在发送任务运行后由任务自己启动（参考 CanSendTask_Process）
     * 1s 超时定时器在接收任务启动后立即启动（参考 Step 10）
     *
     * ⚠️ 重要：现在返回 main.c 执行 __enable_irq() + vTaskStartScheduler()
     */
    debug_printf(INFO_ORDINARY, "CanTaskInit done.\r\n");
}