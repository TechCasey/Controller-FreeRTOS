/**
 * @file    systick.c
 * @brief   SysTick 延时驱动 — 带注释学习版（GD32A50x）
 *
 * =============================================================================
 * 功能说明：
 *   - 初始化 SysTick 为 FreeRTOS 心跳（1ms 中断）
 *   - 提供微秒/毫秒/秒延时函数
 *   - 运行时自动区分 RTOS 和裸机模式
 *
 * 核心变量：
 *   ucFac_us ── 微秒分频系数 = SystemCoreClock / 1000000
 *              （100MHz 系统 → 100，即 1us 需要计数 100 次）
 *   usFac_ms ── 毫秒分频系数 = 1000 / configTICK_RATE_HZ
 *              （1000Hz tick rate → 1，即 1ms = 1 个 tick）
 * =============================================================================
 */

#include "gd32a50x.h"
#include "systick.h"

/* ============================================================
 * 【固定区】静态变量
 * ─────────────────────────────────────────────────────────────
 * ucFac_us ── 微秒分频系数
 *   计算方法：SystemCoreClock / 1000000
 *   100MHz / 1000000 = 100
 *   即：每 1 微秒需要 SysTick 计数 100 次
 *
 * usFac_ms ── 毫秒分频系数
 *   计算方法：1000 / configTICK_RATE_HZ
 *   1000 / 1000 = 1
 *   即：1 毫秒 = 1 个 FreeRTOS tick
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：这两个变量由 SysTick_Init() 初始化，不要手动修改！
 * ============================================================ */
static uint8_t  ucFac_us = 0;
static uint16_t usFac_ms = 0;

/* ============================================================
 * 【运行时步骤 1/2/3/4】SysTick 初始化（FreeRTOS 心跳基准）
 * ─────────────────────────────────────────────────────────────
 * 搭建步骤：
 *   Step 1 ──► NVIC_SetPriority ──► 设置 SysTick 中断优先级（最高）
 *   Step 2 ──► ucFac_us = SystemCoreClock / 1000000 ──► 计算微秒分频系数
 *   Step 3 ──► uiReload = ucFac_us × 1000000 / configTICK_RATE_HZ ──► 计算重载值
 *   Step 4 ──► usFac_ms = 1000 / configTICK_RATE_HZ ──► 计算毫秒分频系数
 *   Step 5 ──► SysTick->LOAD + SysTick->CTRL ──► 配置并使能 SysTick
 * ─────────────────────────────────────────────────────────────
 * configTICK_RATE_HZ = 1000 → SysTick 每 1ms 中断一次
 * SysTick 心跳是 FreeRTOS 调度器的脉搏：
 *   - vTaskDelay() 依赖此心跳工作
 *   - 软件定时器依赖此心跳工作
 *   - 任务切换依赖此心跳触发
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】如需修改心跳频率：
 *   - 修改 configTICK_RATE_HZ（在 FreeRTOSConfig.h 中）
 *   - 当前 configTICK_RATE_HZ = 1000（1ms tick）
 * ============================================================ */
void SysTick_Init(void)
{
    uint32_t uiReload = 0;

    /* Step 1 ──► 设置 SysTick 中断优先级为最高
     * NVIC_SetPriority(SysTick_IRQn, (1 << __NVIC_PRIO_BITS) - 1)
     * NVIC_PRIO_BITS = 4（GD32A50x），最高优先级 = 15
     */
    NVIC_SetPriority(SysTick_IRQn, (1 << __NVIC_PRIO_BITS) - 1);

    /* Step 2 ──► 计算微秒分频系数
     * SystemCoreClock = 100MHz（100_000_000 Hz）
     * ucFac_us = 100_000_000 / 1_000_000 = 100
     * 即：1 微秒 = SysTick 计数 100 次
     */
    ucFac_us = SystemCoreClock / 1000000;

    /* Step 3 ──► 计算 SysTick 重载值（1ms 中断一次）
     * uiReload = ucFac_us
     * uiReload *= 1_000_000 / configTICK_RATE_HZ
     *          = 100 × 1_000_000 / 1000
     *          = 100 × 1000 = 100_000
     * 即：1ms = SysTick 计数 100_000 次
     * 配置后 SysTick 每 100_000 次计数溢出 → 触发中断
     */
    uiReload = ucFac_us;
    uiReload *= 1000000 / configTICK_RATE_HZ;

    /* Step 4 ──► 计算毫秒分频系数
     * configTICK_RATE_HZ = 1000
     * usFac_ms = 1000 / 1000 = 1
     * 即：1ms = 1 个 FreeRTOS tick
     */
    usFac_ms = 1000 / configTICK_RATE_HZ;

    /* Step 5 ──► 配置 SysTick 寄存器并使能
     * SysTick->VAL   = 0UL ──► 当前计数值清零
     * SysTick->LOAD  = uiReload - 1 ──► 重载值 = 100_000 - 1
     * SysTick->CTRL:
     *   CLKSOURCE = 1 ──► 时钟源 = AHB 时钟（SystemCoreClock）
     *   TICKINT   = 1 ──► 使能 SysTick 中断
     *   ENABLE    = 1 ──► 使能 SysTick 计数器
     */
    SysTick->VAL   = 0UL;
    SysTick->LOAD   = uiReload - 1;
    SysTick->CTRL   = SysTick_CTRL_CLKSOURCE_Msk |  /* AHB 时钟 = 100MHz */
                       SysTick_CTRL_TICKINT_Msk   |  /* 使能中断 */
                       SysTick_CTRL_ENABLE_Msk;       /* 使能计数 */
}

/* ============================================================
 * 【固定区】微秒延时（裸机/硬件延时）
 * ─────────────────────────────────────────────────────────────
 * 原理：读取 SysTick 当前计数值（VAL），计算经过的滴答数
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► ticks = uiNus × ucFac_us ──► 将微秒转换为滴答数
 *   Step 2 ──► told = SysTick->VAL ──► 记录初始计数值
 *   Step 3 ──► while(tcnt < ticks) ──► 循环直到经过足够滴答
 *   Step 4 ──► told > tnow ──► 递减计数器（未溢出）
 *   Step 5 ──► told <= tnow ──► 计数器已溢出（reload + told - tnow）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：此函数基于硬件 SysTick，不依赖 FreeRTOS！
 *   适用于所有场景（RTOS 运行时也可用）。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】适用场景：
 *   - 外设初始化延时（如 I2C/SPI 等待就绪）
 *   - 短延时（< 1ms），精度要求高的场景
 *   - RTOS 运行时的高精度延时
 * ============================================================ */
void delay_xus(uint32_t uiNus)
{
    /* Step 1 ──► 将微秒转换为 SysTick 滴答数 */
    uint32_t ticks = uiNus * ucFac_us;

    uint32_t told;   /* 上次 SysTick 计数值 */
    uint32_t tnow;   /* 当前 SysTick 计数值 */
    uint32_t tcnt = 0;  /* 已经过的滴答计数 */
    uint32_t reload = SysTick->LOAD;  /* SysTick 重载值 */

    /* Step 2 ──► 记录初始计数值 */
    told = SysTick->VAL;

    /* Step 3 ──► 循环等待直到经过足够滴答 */
    while (tcnt < ticks) {
        tnow = SysTick->VAL;

        /* Step 4 ──► 计数器递减（未溢出） */
        if (told > tnow) {
            tcnt += told - tnow;
        }
        /* Step 5 ──► 计数器已溢出（从 0 重新开始） */
        else {
            tcnt += told + reload - tnow;
        }
        told = tnow;
    }
}

/* ============================================================
 * 【运行时步骤 1/2】毫秒延时（RTOS 感知，自动切换）
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► xTaskGetSchedulerState() ──► 检测 RTOS 调度器是否运行
 *   Step 2 ──► RTOS 运行 → vTaskDelay() ──► OS 延时（不阻塞 CPU）
 *   Step 3 ──► RTOS 未运行 → delay_xus() ──► 硬件延时（裸机模式）
 * ─────────────────────────────────────────────────────────────
 * 延时逻辑：
 *   if (nms >= usFac_ms) {
 *       vTaskDelay(nms / usFac_ms);  // OS 延时，先处理整毫秒部分
 *   }
 *   nms %= usFac_ms;                 // 剩余 < 1ms
 *   delay_xus(nms * 1000);           // 硬件延时处理剩余微秒
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】使用说明：
 *   - delay_ms() 是最常用的延时函数
 *   - RTOS 运行时自动使用 vTaskDelay（让出 CPU）
 *   - 裸机运行时自动切换到硬件延时
 * ============================================================ */
void delay_ms(uint32_t nms)
{
    /* Step 1 ──► 检测 RTOS 调度器状态 */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        /* Step 2 ──► RTOS 运行中，使用 OS 延时
         * usFac_ms = 1（configTICK_RATE_HZ=1000 时）
         * nms / usFac_ms = nms（延时 nms 个 tick）
         * vTaskDelay 是"相对延时"，任务进入阻塞状态，让出 CPU
         */
        if (nms >= usFac_ms) {
            vTaskDelay(nms / usFac_ms);
        }
        nms %= usFac_ms;  /* 处理不足 1ms 的部分 */
    }
    /* Step 3 ──► 剩余毫秒（< usFac_ms）使用硬件延时 */
    delay_xus(nms * 1000);
}

/* ============================================================
 * 【固定区】纯硬件毫秒延时（强制使用 delay_xus）
 * ─────────────────────────────────────────────────────────────
 * 无论 RTOS 是否运行，始终使用硬件延时。
 * 用于必须在确定时间内阻塞的场景（如延时函数测试）。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】适用场景：
 *   - 裸机程序中的毫秒延时
 *   - RTOS 任务中不希望让出 CPU 的延时
 * ============================================================ */
void delay_xms(uint32_t nms)
{
    for (uint32_t i = 0; i < nms; i++) {
        delay_xus(1000);
    }
}

/* ============================================================
 * 【固定区】秒延时（纯硬件）
 * ─────────────────────────────────────────────────────────────
 * 循环调用 delay_ms(1000)，总延时 nS 秒。
 * 适用于裸机初始化阶段或启动延时。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】适用场景：
 *   - 系统启动延时（如等待外设稳定）
 *   - 裸机模式下的长时间延时
 * ============================================================ */
void delay_s(uint32_t nS)
{
    for (uint32_t i = 0; i < nS; i++) {
        delay_ms(1000);
    }
}

/* ============================================================
 * 【固定区】纯硬件秒延时（强制使用 delay_xms）
 * ─────────────────────────────────────────────────────────────
 * 无论 RTOS 是否运行，始终使用硬件延时。
 * 与 delay_s() 的区别：delay_s() 在 RTOS 下会混合使用 vTaskDelay，
 * 而 delay_xs() 强制全部使用硬件延时（阻塞 CPU）。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】适用场景：
 *   - 初始化阶段，不希望被 RTOS 调度打断的延时
 * ============================================================ */
void delay_xs(uint32_t nS)
{
    for (uint32_t i = 0; i < nS; i++) {
        delay_xms(1000);
    }
}
