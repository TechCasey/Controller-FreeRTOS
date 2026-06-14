/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS 内核配置参数 — 学习版
 *
 * =============================================================================
 * 什么是 FreeRTOSConfig.h？
 * =============================================================================
 * 这是 FreeRTOS 内核的"配置文件"，在 kernel 编译之前被读取。
 * 它定义了：
 *   - 内核行为开关（开/关某个功能）
 *   - 硬件相关参数（CPU 时钟频率、Tick 时钟源）
 *   - 资源上限（最大优先级数量、堆大小）
 *   - 钩子函数开关
 *
 * 这些参数决定了 FreeRTOS 内核如何运行，是学习的第一站。
 * =============================================================================
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "gd32a50x.h"           /* 芯片头文件，提供 SystemCoreClock 变量 */

/* ============================================================
 * Step 1 — 硬件时钟配置
 * ============================================================
 * configCPU_CLOCK_HZ：CPU 主频，必须和实际芯片配置一致
 * 这里由 gd32a50x.h 提供，在 system_gd32a50x.c 中初始化为 100MHz
 * ============================================================ */
#define configCPU_CLOCK_HZ          (SystemCoreClock)

/* ============================================================
 * Step 2 — Tick 时钟配置（系统心跳）
 * ============================================================
 * configTICK_RATE_HZ：SysTick 中断频率
 * 设置为 1000 意味着每 1ms 触发一次 SysTick 中断
 * → vTaskDelay(5) = 延时 5ms
 * → 软件定时器 period = 100 = 100ms
 *
 * ⚠️ 修改此值会影响所有基于 vTaskDelay 和定时器的延时
 * ============================================================ */
#define configTICK_RATE_HZ          ((TickType_t)1000)

/* ============================================================
 * Step 3 — 调度器模式
 * ============================================================
 * configUSE_PREEMPTION：抢占式调度开关
 *   1 = 抢占式（高优先级任务就绪时立即抢走 CPU）
 *   0 = 协作式（任务必须主动延时或让出 CPU 才能切换）
 * 默认使用抢占式（推荐）
 * ============================================================ */
#define configUSE_PREEMPTION        1

/* ============================================================
 * Step 4 — 内存分配方式
 * ============================================================
 * configSUPPORT_STATIC_ALLOCATION：静态分配（手动提供 TCB/栈内存）
 * configSUPPORT_DYNAMIC_ALLOCATION：动态分配（从 heap 区自动分配）
 *
 * 本项目两者都开启，一般用动态分配更方便。
 * ============================================================ */
#define configSUPPORT_STATIC_ALLOCATION       0
#define configSUPPORT_DYNAMIC_ALLOCATION      1

/* ============================================================
 * Step 5 — 优先级配置
 * ============================================================
 * configMAX_PRIORITIES：最大优先级数量（0 ~ configMAX_PRIORITIES-1）
 * 数字越小优先级越低，configMAX_PRIORITIES-1 是最高优先级
 * 这里设 32，即优先级 0~31
 *
 * osPriority_t 枚举定义在 type.h 中：
 *   osPriorityIdle      = 0      （最低）
 *   osPriorityLow       = 1
 *   osPriorityNormal    = 7
 *   osPriorityHigh      = 15
 *   osPriorityRealtime  = 25
 *   osPriorityISR       = 31      （最高，供中断延迟处理使用）
 * ============================================================ */
#define configMAX_PRIORITIES        32

/* ============================================================
 * Step 6 — 栈与堆大小
 * ============================================================
 * configMINIMAL_STACK_SIZE：最小任务栈大小（单位：word = 4字节）
 * 设 128 即 128 × 4 = 512 字节
 *
 * configTOTAL_HEAP_SIZE：动态内存池大小（单位：字节）
 * 所有 xTaskCreate / xQueueCreate / xTimerCreate 的内存从这里分配
 * 设 25KB = 25 × 1024 字节
 * ⚠️ 如果创建任务/队列时提示 heap 不足，可以增大此值
 * ============================================================ */
#define configMINIMAL_STACK_SIZE    ((uint16_t)128)
#define configTOTAL_HEAP_SIZE       ((size_t)25 * 1024)

/* ============================================================
 * Step 7 — 任务名称长度
 * ============================================================
 * configMAX_TASK_NAME_LEN：任务名字符串最大长度
 * ============================================================ */
#define configMAX_TASK_NAME_LEN     32

/* ============================================================
 * Step 8 — 统计功能
 * ============================================================
 * configGENERATE_RUN_TIME_STATS：运行时统计开关
 * 开启后可用 vTaskGetRunTimeStats() 获取每个任务的 CPU 使用时间
 *
 * 需要配合：
 *   - portGET_RUN_TIME_COUNTER_VALUE() 返回当前运行时间计数
 *   - portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() 初始化统计用定时器
 *
 * 这两个宏在 osbasetimer.c 中定义，使用 Timer7（50µs 周期）计数
 * ============================================================ */
#define configGENERATE_RUN_TIME_STATS             0
#define portGET_RUN_TIME_COUNTER_VALUE()           (FreeRTOSRunTimeTicks)
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  (Timer7_50us())

/* ============================================================
 * Step 9 — 软件定时器
 * ============================================================
 * configUSE_TIMERS：软件定时器子系统开关
 *   1 = 开启，定时器守护任务（Timer Daemon）自动创建
 *
 * configTIMER_TASK_PRIORITY：定时器守护任务优先级
 * 设 为 configMAX_PRIORITIES - 1 = 31（最高优先级之一）
 *
 * configTIMER_QUEUE_LENGTH：定时器命令队列长度
 * xTimerStart/xTimerStop 等命令通过此队列发给守护任务
 *
 * configTIMER_TASK_STACK_DEPTH：定时器任务栈大小
 * ============================================================ */
#define configUSE_TIMERS                     1
#define configTIMER_TASK_PRIORITY            (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH             20
#define configTIMER_TASK_STACK_DEPTH        (configMINIMAL_STACK_SIZE * 2)

/* ============================================================
 * Step 10 — 互斥量
 * ============================================================
 * configUSE_MUTEXES：互斥量开关
 *   1 = 开启，可使用 xSemaphoreCreateMutex()
 * configUSE_RECURSIVE_MUTEXES：递归互斥量开关
 * configUSE_COUNTING_SEMAPHORES：计数信号量开关
 * ============================================================ */
#define configUSE_MUTEXES                1
#define configUSE_RECURSIVE_MUTEXES      1
#define configUSE_COUNTING_SEMAPHORES    1

/* ============================================================
 * Step 11 — 队列注册表
 * ============================================================
 * configQUEUE_REGISTRY_SIZE：队列/信号量注册表大小
 * 用于调试时给队列起名字，方便在调试器里查看
 * ============================================================ */
#define configQUEUE_REGISTRY_SIZE        8

/* ============================================================
 * Step 12 — 协程（Coroutine）
 * ============================================================
 * configUSE_CO_ROUTINES：协程开关（轻量级任务，现已很少使用）
 * configMAX_CO_ROUTINE_PRIORITIES：协程优先级数量
 * ============================================================ */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES 2

/* ============================================================
 * Step 13 — 空闲任务钩子（Idle Hook）
 * ============================================================
 * configUSE_IDLE_HOOK：是否启用空闲钩子
 *   1 = 启用，系统会在 CPU 空闲时调用 vApplicationIdleHook()
 * 用途：喂看门狗、切换状态灯、进入低功耗模式
 *
 * configIDLE_SHOULD_YIELD：空闲任务是否主动让出 CPU 给同等优先级的就绪任务
 *   0 = 不让出（空闲任务一直运行直到被高优先级任务抢占）
 * ============================================================ */
#define configUSE_IDLE_HOOK             1
#define configIDLE_SHOULD_YIELD         0

/* ============================================================
 * Step 14 — Tick 钩子
 * ============================================================
 * configUSE_TICK_HOOK：是否启用 Tick 钩子
 *   1 = 每次 SysTick 中断时调用 vApplicationTickHook()
 *   一般用于时间维护，不推荐做复杂操作
 * ============================================================ */
#define configUSE_TICK_HOOK             0

/* ============================================================
 * Step 15 — 栈溢出检测
 * ============================================================
 * configCHECK_FOR_STACK_OVERFLOW：
 *   0 = 关闭
 *   1 = 简单检测（每次任务切换时检查栈指针）
 *   2 = 深度检测（检查完整栈区，默认推荐）
 *
 * ⚠️ configCHECK_FOR_STACK_OVERFLOW = 2 时必须实现
 *   vApplicationStackOverflowHook() 函数
 * ============================================================ */
#define configCHECK_FOR_STACK_OVERFLOW  2

/* ============================================================
 * Step 16 — 内存分配失败钩子
 * ============================================================
 * configUSE_MALLOC_FAILED_HOOK：
 *   1 = 启用，heap 分配失败时调用 vApplicationMallocFailedHook()
 *   用于发现内存泄漏或 heap 太小的问题
 * ============================================================ */
#define configUSE_MALLOC_FAILED_HOOK    1

/* ============================================================
 * Step 17 — 追踪/调试功能
 * ============================================================
 * configUSE_TRACE_FACILITY：追踪设施（任务列表等信息）
 * configUSE_STATS_FORMATTING_FUNCTIONS：统计格式化函数
 *   vTaskList() / vTaskGetRunTimeStats() 需要开启
 * configUSE_16_BIT_TICKS：Tick 计数器位数
 *   0 = 32 位（推荐，用于 M33）
 * ============================================================ */
#define configUSE_TRACE_FACILITY                    1
#define configUSE_STATS_FORMATTING_FUNCTIONS        1
#define configUSE_16_BIT_TICKS                      0

/* ============================================================
 * Step 18 — 中断优先级配置（Cortex-M NVIC）
 * ============================================================
 * configPRIO_BITS：芯片的 NVIC 优先级位数
 * GD32A50x 使用 4 位（16 级优先级）
 *
 * configLIBRARY_LOWEST_INTERRUPT_PRIORITY：最低（数值最大）中断优先级
 *   设 15 → 优先级 15 是最低的中断优先级
 *
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY：FreeRTOS 允许调用的
 *   最高中断优先级（数值越小优先级越高）
 *   设 5 → 优先级 0~4 的中断可以调用 FromISR 系列 API
 *   优先级 5~15 的中断不能调用任何 FreeRTOS API
 *
 * configMAX_SYSCALL_INTERRUPT_PRIORITY：NVIC 寄存器值
 *   (5 << (8 - 4)) = 0x50 = 80
 *
 * ⚠️ 重要规则：
 *   中断优先级数值越小 = 中断优先级越高
 *   FreeRTOS 只能保证优先级数字 > configMAX_SYSCALL_INTERRUPT_PRIORITY
 *   的中断中可以安全调用 API
 * ============================================================ */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS               __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS               4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ============================================================
 * Step 19 — 可裁剪的 API 函数
 * ============================================================
 * 以下宏控制哪些 API 函数被编译进最终代码
 * 设置为 1 = 包含，设置为 0 = 裁剪掉（减小代码体积）
 *
 * 推荐开启：vTaskDelay、vTaskDelayUntil、vTaskDelete、
 *          vTaskSuspend、vTaskPrioritySet、uxTaskGetStackHighWaterMark
 * ============================================================ */
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskCleanUpResources        0
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_eTaskGetState               1

/* ============================================================
 * Step 20 — Heap 分配策略（内部使用）
 * ============================================================
 * USE_FreeRTOS_HEAP_4：使用 heap_4.c 分配策略
 * heap_4 特点：
 *   - 首次适配（first-fit）
 *   - 支持内存碎片合并
 *   - 适合嵌入式场景（推荐）
 *
 * 对应的源文件：Middlewares/FreeRTOS/Source/portable/MemMang/heap_4.c
 * ============================================================ */
#define USE_FreeRTOS_HEAP_4

/* ============================================================
 * Step 21 — 断言（Assert）
 * ============================================================
 * configASSERT()：类似于标准 C 的 assert()
 * 当条件不满足时，进入死循环并打印错误位置
 * 强烈建议在开发阶段开启，帮助发现 bug
 * ============================================================ */
extern void debug_printf(uint8_t info_level, const char *fmt, ...);
#define PRINT_INFO  debug_printf
#define vAssertCalled(char, int)  PRINT_INFO(0, "Error:%s,%d\r\n", char, int)
#define configASSERT(x)  \
    if ((x) == 0) { taskDISABLE_INTERRUPTS(); vAssertCalled(__FILE__, __LINE__); for (;;); }

/* ============================================================
 * Step 22 — 运行时间统计类型
 * ============================================================
 * configMESSAGE_BUFFER_LENGTH_TYPE：Message Buffer 的长度类型
 * ============================================================ */
#define configMESSAGE_BUFFER_LENGTH_TYPE   size_t

/* ============================================================
 * Step 23 — FPU / MPU / TrustZone
 * ============================================================
 * configENABLE_FPU：开启浮点单元（FPU）
 * configENABLE_MPU：开启内存保护单元（MPU）
 * configRUN_FREERTOS_SECURE_ONLY：仅在安全世界运行 FreeRTOS
 * configENABLE_TRUSTZONE：开启 TrustZone
 *
 * GD32A50x Cortex-M33 支持 FPU，本项目开启 FPU，关闭 MPU 和 TrustZone
 * ============================================================ */
#define configENABLE_FPU                  1
#define configENABLE_MPU                  0
#define configRUN_FREERTOS_SECURE_ONLY     1
#define configENABLE_TRUSTZONE             0

#endif /* FREERTOS_CONFIG_H */