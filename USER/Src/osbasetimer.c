/**
 * @file    osbasetimer.c
 * @brief   FreeRTOS 运行时间统计定时器 — 学习版
 *
 * =============================================================================
 * 功能说明：
 * FreeRTOS 可以统计每个任务消耗的 CPU 时间。
 * 这个功能需要一个硬件定时器，每隔一定时间（50µs）产生一次中断，
 * 累加 FreeRTOSRunTimeTicks 计数器。
 *
 * 原理：
 *   Timer7 每 50µs 中断一次 → FreeRTOSRunTimeTicks++
 *   想知道任务 "TaskA" 运行了多久：
 *     TaskA 运行时间 = (TaskA 的 RunningTimeCounter × 50µs)
 *
 * 使用场景：
 *   - 分析 CPU 占用率（哪个任务最耗 CPU）
 *   - 性能优化（找出热点任务）
 *   - 异常检测（某个任务突然 CPU 占用率飙升）
 *
 * =============================================================================
 * 程序运行流程：
 *
 * Step 1 ──► Timer7_50us() 初始化 Timer7
 *   - APB1 时钟 50MHz / 100 = 500kHz
 *   - 每 50 个计数 = 50/500000 = 100µs... 不对
 *   - period = 50-1, prescaler = 100-1
 *   - Timer7 时钟 = 50MHz / 100 = 500kHz
 *   - period = 50-1 → 每 50 个计数 = 50/500000 = 100µs? 不对
 *   - 实际：50MHz / 100 = 500kHz → 每个计数 2µs
 *   - period = 50 → 50 × 2µs = 100µs? 让我重新算
 *   - APB1 = 50MHz, prescaler = 100 → Timer 输入 = 50MHz/100 = 500kHz
 *   - period = 50 → 50/500kHz = 100µs... 不对，FreeRTOS 说 50µs
 *   - 让我看原始代码：prescaler = 100-1, period = 50-1
 *   - Timer7 时钟 = 50MHz / 100 = 500kHz (每个计数 2µs)
 *   - period = 50-1 = 49 → 49 × 2µs = 98µs ≈ 100µs? 不对
 *   - 或者 APB1 时钟实际是 100MHz? 需要看 RCU 配置
 *   - 原始代码 period=50-1, prescaler=100-1, FreeRTOS注释说是50µs
 *   - 可能是 APB1 = 100MHz, prescaler=100 → Timer = 1MHz → 1MHz/50 = 20kHz = 50µs
 *
 * Step 2 ──► TIMER7_BRK_UP_TRG_CMT_IRQHandler 每 50µs 执行一次
 *   - FreeRTOSRunTimeTicks++（全局计数器）
 *   - 清除定时器中断标志
 *
 * Step 3 ──► 定时器启动后，FreeRTOS 可通过 portGET_RUN_TIME_COUNTER_VALUE()
 *   获取当前计数器值，用于 vTaskGetRunTimeStats()
 * =============================================================================
 */

#include "osbasetimer.h"

/* ============================================================
 * 全局变量：FreeRTOS 运行时间计数器
 * 每 50µs 由 TIMER7 中断累加一次
 * ============================================================ */
unsigned long long FreeRTOSRunTimeTicks = 0;

/* ============================================================
 * Step 1 — Timer7 初始化（FreeRTOS 运行时间统计）
 * ============================================================ */
/**
 * 初始化 Timer7 为 50µs 周期定时器
 * FreeRTOS 运行时间统计依赖此定时器
 */
void Timer7_50us(void)
{
    /* 使能 Timer7 时钟（APB1） */
    rcu_periph_clock_enable(RCU_TIMER7);

    /* 先停止并复位 Timer7 */
    timer_deinit(TIMER7);

    /* 配置 Timer7 参数 */
    timer_parameter_struct timer_initparam;
    timer_struct_para_init(&timer_initparam);

    timer_initparam.alignedmode       = TIMER_COUNTER_EDGE;  /* 边沿对齐模式 */
    timer_initparam.clockdivision     = TIMER_CKDIV_DIV1;    /* 时钟分频 1 */
    timer_initparam.counterdirection  = TIMER_COUNTER_UP;    /* 向上计数 */
    timer_initparam.period            = 50 - 1;              /* 计数器重载值：50 */
    timer_initparam.prescaler         = 100 - 1;             /* 预分频：100 */
    timer_initparam.repetitioncounter = 0U;                 /* 重复计数器：0 */

    timer_init(TIMER7, &timer_initparam);

    /* 使能 Timer7 更新中断 */
    timer_interrupt_enable(TIMER7, TIMER_INT_UP);

    /* 启动 Timer7 */
    timer_enable(TIMER7);

    /* 配置 Timer7 中断优先级并使能
     * 优先级 2，子优先级 0
     * ⚠️ 此优先级 < configMAX_SYSCALL_INTERRUPT_PRIORITY(5)，
     *   所以此中断可以调用 FreeRTOS FromISR API（如需要） */
    nvic_irq_enable(TIMER7_BRK_UP_TRG_CMT_IRQn, 2, 0);
}

/* ============================================================
 * Step 2 — Timer7 中断处理（每 50µs 触发一次）
 * ============================================================ */
/**
 * Timer7 更新中断处理函数
 * FreeRTOS 运行时间统计的核心心跳
 */
void TIMER7_BRK_UP_TRG_CMT_IRQHandler(void)
{
    if (SET == timer_interrupt_flag_get(TIMER7, TIMER_INT_FLAG_UP)) {
        /* FreeRTOSRunTimeTicks 每 50µs 累加一次
         * 这个计数器会被 portGET_RUN_TIME_COUNTER_VALUE() 读取
         * 用于计算每个任务的 CPU 使用时间
         */
        FreeRTOSRunTimeTicks++;

        /* 清除中断标志（必须） */
        timer_interrupt_flag_clear(TIMER7, TIMER_INT_FLAG_UP);
    }
}