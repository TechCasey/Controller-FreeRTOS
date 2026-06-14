/**
 * @file    FreeRTOSPerfect.c
 * @brief   FreeRTOS 回调钩子函数 — 学习版
 *
 * =============================================================================
 * 什么是 Hook 函数（钩子函数）？
 * =============================================================================
 * 钩子函数是 FreeRTOS 内核在特定事件发生时自动调用的用户函数。
 * 它们不是业务逻辑，而是"系统级"的响应机制。
 *
 * 使用场景：
 *   - vApplicationIdleHook       → 看门狗喂狗、LED 状态指示、低功耗
 *   - vApplicationStackOverflowHook → 栈溢出告警
 *   - vApplicationMallocFailedHook  → 内存分配失败告警
 *
 * ⚠️ 注意：这些函数必须简短，不能在内部调用会阻塞的 API！
 * =============================================================================
 */

#include "FreeRTOS.h"
#include "task.h"

/* ============================================================
 * 外部声明（用于打印）
 * ============================================================ */
extern void debug_printf(uint8_t info_level, const char *fmt, ...);
#define PRINT_INFO  debug_printf

/* ============================================================
 * Step 1 — Idle 任务钩子（vApplicationIdleHook）
 * ============================================================
 * 调用时机：当 CPU 没有任何任务处于就绪状态时调用
 * 触发频率：持续调用（CPU 一直处于空闲状态）
 *
 * 用途示例：
 *   - 喂硬件看门狗（防止复位）
 *   - 切换状态指示灯（表示系统正常运行）
 *   - 进入低功耗模式（sleep）
 *
 * ⚠️ 重要：Idle 钩子里不能调用会阻塞的 API（如 vTaskDelay）
 *   因为 Idle 任务必须始终保持就绪状态
 * ⚠️ 重要：如果 configIDLE_SHOULD_YIELD = 1，
 *   Idle 任务会在每个时间片结束时主动让出 CPU
 * ============================================================ */
void vApplicationIdleHook(void)
{
    /* 示例：喂看门狗
     * Feed_Fwdtg() 必须在每个周期内调用一次，
     * 否则硬件看门狗会在超时后复位 MCU。
     * 这里在 Idle 时喂狗是安全的（CPU 空闲时喂）。
     */
    /* Feed_Fwdtg(); */

    /* 示例：切换运行指示灯
     * SetLedToggle(LED_RUN) 每调用一次切换一次 LED 状态，
     * 可以用来指示系统是否在正常运行。
     */
    /* SetLedToggle(LED_RUN); */

    /* 示例：简单延时（⚠️ 不推荐在 Idle 钩子里用延时）
     * delay_xs(1) 会让 CPU 进入忙等待，功耗较高。
     * 推荐用 __WFI() 进入睡眠模式。
     */
}

/* ============================================================
 * Step 2 — 栈溢出检测钩子（vApplicationStackOverflowHook）
 * ============================================================
 * 调用时机：当 FreeRTOS 检测到任务栈溢出时调用
 * 触发条件：configCHECK_FOR_STACK_OVERFLOW = 1 或 2
 *
 * 参数说明：
 *   xTask      — 溢出任务的句柄
 *   pcTaskName — 溢出任务的名字（字符串）
 *
 * ⚠️ 检测到溢出后，系统已经处于不稳定状态，
 *   此钩子应该立即关闭中断、打印错误信息并进入死循环，
 *   防止数据破坏或安全事件。
 * ============================================================ */
#if configCHECK_FOR_STACK_OVERFLOW > 0
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* 关闭所有中断，防止中断继续破坏数据 */
    taskDISABLE_INTERRUPTS();

    /* 打印溢出任务的名字 */
    PRINT_INFO(0, "Stack Overflow in Task: %s", pcTaskName);

    /* 进入死循环，等待看门狗复位或人工干预 */
    while (1) {
        uint32_t delay = UINT32_MAX;
        while (--delay);  /* 延时，防止串口打印死循环 */
        PRINT_INFO(0, "Stack Overflow in Task: %s", pcTaskName);
    }
}
#endif

/* ============================================================
 * Step 3 — 内存分配失败钩子（vApplicationMallocFailedHook）
 * ============================================================
 * 调用时机：当 xTaskCreate / xQueueCreate / xTimerCreate 等
 *           申请 heap 内存失败时调用
 * 触发条件：configUSE_MALLOC_FAILED_HOOK = 1
 *
 * 原因分析（常见）：
 *   - configTOTAL_HEAP_SIZE 太小
 *   - 内存碎片化（频繁创建/删除任务/队列）
 *   - 泄漏（创建了资源但从未删除）
 *
 * 排查方法：
 *   1. 增大 configTOTAL_HEAP_SIZE
 *   2. 开启 configGENERATE_RUN_TIME_STATS，
 *      用 vTaskGetRunTimeStats() 查看每个任务的栈使用情况
 *   3. 用 xPortGetFreeHeapSize() 查看当前剩余 heap
 * ============================================================ */
#if configUSE_MALLOC_FAILED_HOOK == 1
void vApplicationMallocFailedHook(void)
{
    /* 关闭所有中断 */
    taskDISABLE_INTERRUPTS();

    /* 打印错误信息 */
    while (1) {
        uint32_t delay = UINT32_MAX;
        while (--delay);
        PRINT_INFO(0, "\r\nIn vApplicationMallocFailedHook\r\n");
    }
}
#endif

/* ============================================================
 * Step 4 — Timer Daemon 启动钩子（vApplicationDaemonTaskStartupHook）
 * ============================================================
 * 调用时机：Timer Daemon（定时器守护）任务启动时调用
 * 触发条件：configUSE_DAEMON_TASK_STARTUP_HOOK = 1 且 configUSE_TIMERS = 1
 *
 * 用途示例：
 *   - 在定时器任务启动后做额外的初始化
 *   - 启动所有软件定时器（确保定时器任务已就绪）
 * ============================================================ */
#if (configUSE_DAEMON_TASK_STARTUP_HOOK == 1) && (configUSE_TIMERS == 1)
void vApplicationDaemonTaskStartupHook(void)
{
    /* 示例：在定时器任务启动后启动所有定时器
     * xTimerStart(TimerHandle, portMAX_DELAY);
     */
}
#endif

/* ============================================================
 * Step 5 — 统计/调试辅助宏
 * ============================================================
 * 以下宏用于在钩子函数中输出诊断信息
 * 可在调试阶段开启，帮助了解系统状态
 * ============================================================ */

/* 打印当前剩余 heap 大小（单位：字节）
 * 用法：在某个定时任务里每 1 秒打印一次
 *   PRINT_INFO(0, "Free Heap: %d bytes\r\n", xPortGetFreeHeapSize());
 */

/* 打印历史最小剩余 heap
 *   PRINT_INFO(0, "Min Free Heap: %d bytes\r\n", xPortGetMinimumEverFreeHeapSize());
 */

/* 打印任务栈高水位（剩余最少栈空间）
 *   PRINT_INFO(0, "%s Stack HWM: %u\r\n",
 *              pcTaskName, uxTaskGetStackHighWaterMark(NULL));
 */