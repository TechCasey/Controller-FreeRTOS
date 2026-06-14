/**
 * @file    type.h
 * @brief   FreeRTOS 资源类型封装 — 学习版
 *
 * =============================================================================
 * 为什么需要这些封装？
 * =============================================================================
 * FreeRTOS 原生 API 使用原始句柄（TaskHandle_t、TimerHandle_t 等），
 * 这个项目把句柄包装进结构体，附带元数据（名字、栈大小、优先级等），
 * 方便在多个模块之间传递和管理。
 *
 * 学习意义：
 *   - 理解 FreeRTOS 核心对象（任务、队列、定时器、信号量）
 *   - 理解任务优先级映射（osPriority_t）
 *   - 理解 CAN 通信中消息帧的结构
 * =============================================================================
 */

#ifndef __TYPE__H_
#define __TYPE__H_

#include "gd32a50x.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "timers.h"

/* ============================================================
 * Step 1 — 优先级枚举（与 FreeRTOS 配合使用）
 * ============================================================
 * FreeRTOS 优先级范围：0（最低）到 configMAX_PRIORITIES-1（最高）
 * 这里用 osPriority_t 枚举定义了几档常用优先级，供项目使用
 *
 * 使用方式：
 *   xTaskCreate(..., osPriorityNormal, ...)  // 等于 FreeRTOS 优先级 7
 * ============================================================ */
typedef enum {
    osPriorityIdle      =  0,         /* 最低优先级，预留给空闲任务 */
    osPriorityLow       =  1,         /* 低优先级 */
    osPriorityLow1      =  2,
    osPriorityLow2      =  3,
    osPriorityLow3      =  4,
    osPriorityLow4      =  5,
    osPriorityLow5      =  6,
    osPriorityNormal     =  7,         /* 普通优先级（最常用） */
    osPriorityNormal1   =  8,
    osPriorityNormal2   =  9,
    osPriorityNormal3   = 10,
    osPriorityNormal4   = 11,
    osPriorityNormal5   = 12,
    osPriorityNormal6   = 13,
    osPriorityNormal7   = 14,
    osPriorityHigh      = 15,         /* 高优先级 */
    osPriorityHigh1     = 16,
    osPriorityHigh2     = 17,
    osPriorityHigh3     = 18,
    osPriorityHigh4     = 19,
    osPriorityHigh5     = 20,
    osPriorityHigh6     = 21,
    osPriorityHigh7     = 22,
    osPriorityHigh8     = 23,
    osPriorityHigh9     = 24,
    osPriorityRealtime  = 25,         /* 实时优先级 */
    osPriorityRealtime1 = 26,
    osPriorityRealtime2 = 27,
    osPriorityRealtime3 = 28,
    osPriorityRealtime4 = 29,
    osPriorityRealtime5 = 30,
    osPriorityISR       = 31,         /* 最高优先级，预留给中断延迟处理 */
} osPriority_t;

/* ============================================================
 * Step 2 — 任务结构体封装（task_t）
 * ============================================================
 * 封装了 FreeRTOS 任务的所有元信息
 *
 * 使用方式（在 TaskInit 函数中）:
 *   s_myTask.Task_name  = "MyTask";
 *   s_myTask.Task_stackDepth = configMINIMAL_STACK_SIZE * 2;
 *   s_myTask.Task_priority = osPriorityNormal;
 *   xTaskCreate(MyTaskFunc, s_myTask.Task_name, ...);
 * ============================================================ */
typedef struct {
    char           *Task_name;        /* 任务名字符串（调试用） */
    uint16_t        Task_stackDepth;   /* 栈深度（单位：word = 4字节） */
    UBaseType_t     Task_priority;     /* 优先级（使用 osPriority_t 值） */
    TaskHandle_t    Task_handle;       /* FreeRTOS 任务句柄（创建后填充） */
} task_t;

/* ============================================================
 * Step 3 — 软件定时器结构体封装（Times_t）
 * ============================================================
 * 封装了软件定时器的所有参数
 *
 * 使用方式（在 TaskInit 函数中）:
 *   s_myTimer.Timer_name  = "MyTimer";
 *   s_myTimer.Timer_Tick  = 100;      // 100 ticks = 100ms（configTICK_RATE_HZ=1000）
 *   s_myTimer.Timer_State = pdTRUE;   // pdTRUE = 自动重载，pdFALSE = 单次
 *   s_myTimer.Timer_ID    = 0;
 *   s_myTimer.pTimer_Handle = xTimerCreate(...);
 * ============================================================ */
typedef struct {
    char            *Timer_name;      /* 定时器名字符串（调试用） */
    uint16_t         Timer_Tick;       /* 定时周期（单位：tick，1 tick = 1ms） */
    UBaseType_t      Timer_State;      /* pdTRUE = 自动重载定时器，pdFALSE = 单次定时器 */
    uint8_t          Timer_ID;        /* 定时器 ID（用于回调中区分多个定时器） */
    TimerHandle_t    pTimer_Handle;    /* FreeRTOS 定时器句柄（创建后填充） */
} Times_t;

/* ============================================================
 * Step 4 — CAN 消息帧结构体
 * ============================================================
 * 用于在消息队列中传递 CAN 数据
 *
 * 字段说明：
 *   ide  — 帧类型：1 = 扩展帧，0 = 标准帧
 *   dlc  — 数据长度（0~8 字节）
 *   id   — CAN ID（扩展帧 29 位 ID，或标准帧 11 位 ID）
 *   data — 数据负载（8 字节数组）
 * ============================================================ */
typedef struct {
    uint8_t     ide;                  /* 1=扩展帧，0=标准帧 */
    uint8_t     dlc;                  /* 数据长度：0~8 */
    uint32_t    id;                    /* CAN ID */
    uint8_t     data[8];               /* CAN 数据负载 */
} CAN_Msg_t;

#endif /* __TYPE__H_ */