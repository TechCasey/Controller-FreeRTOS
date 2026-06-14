/**
 * @file    systick.c
 * @brief   SysTick 延时驱动 — 实现文件
 *
 * 功能：
 *   - 初始化 SysTick 为 FreeRTOS 心跳（1ms 中断）
 *   - 提供微秒/毫秒/秒延时函数
 *   - OS 运行时自动使用 vTaskDelay()，OS 未运行时使用硬件延时
 */

#include "systick.h"
#include "task.h"  /* xTaskGetSchedulerState, taskSCHEDULER_NOT_STARTED */

static uint8_t ucFac_us = 0;
static uint16_t usFac_ms = 0;

/**
 * 初始化 SysTick
 * ─────────────────────────────────────────────────────────────
 * Step 1 ──► 设置 SysTick 中断优先级（最高）
 * Step 2 ──► 计算微秒分频系数
 * Step 3 ──► 计算毫秒分频系数
 * Step 4 ──► 配置重载值和使能
 * ─────────────────────────────────────────────────────────────
 * configTICK_RATE_HZ = 1000 → 1ms 中断一次
 * ucFac_us = SystemCoreClock / 1000000（100MHz/1MHz = 100）
 * usFac_ms = 1000 / configTICK_RATE_HZ = 1
 */
void SysTick_Init(void)
{
    uint32_t uiReload = 0;

    /* Step 1 ──► SysTick 中断优先级（最高） */
    NVIC_SetPriority(SysTick_IRQn, (1 << __NVIC_PRIO_BITS) - 1);

    /* Step 2 ──► 微秒分频系数：SystemCoreClock / 1000000 */
    ucFac_us = SystemCoreClock / 1000000;  /* 100MHz → 100 */

    /* Step 3 ──► 计算重载值：1ms 对应的计数次数 */
    uiReload = ucFac_us;
    uiReload *= 1000000 / configTICK_RATE_HZ;  /* 1ms */

    /* Step 4 ──► 毫秒分频系数 */
    usFac_ms = 1000 / configTICK_RATE_HZ;  /* = 1 */

    /* 配置并使能 SysTick */
    SysTick->VAL = 0UL;
    SysTick->LOAD = uiReload - 1;  /* 1ms 中断一次 */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* AHB 时钟 */
                    SysTick_CTRL_TICKINT_Msk   |   /* 使能中断 */
                    SysTick_CTRL_ENABLE_Msk;       /* 使能计数 */
}

/**
 * 微秒延时（纯硬件延时）
 * 原理：读取当前 SysTick VAL 并计算经过的滴答数
 */
void delay_xus(uint32_t uiNus)
{
    uint32_t ticks = uiNus * ucFac_us;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;

    told = SysTick->VAL;

    while (tcnt < ticks) {
        tnow = SysTick->VAL;
        if (told > tnow) {
            tcnt += told - tnow;
        } else {
            tcnt += told + reload - tnow;
        }
        told = tnow;
    }
}

/**
 * 毫秒延时（OS 感知）
 * 如果 OS 调度器已启动，使用 vTaskDelay（不阻塞），
 * 否则使用纯硬件延时
 */
void delay_ms(uint32_t nms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        if (nms >= usFac_ms) {
            vTaskDelay(nms / usFac_ms);  /* OS 延时 */
        }
        nms %= usFac_ms;
    }
    delay_xus(nms * 1000);  /* 剩余毫秒用硬件延时 */
}

void delay_xms(uint32_t nms)
{
    for (uint32_t i = 0; i < nms; i++)
        delay_xus(1000);
}

void delay_s(uint32_t nS)
{
    for (uint32_t i = 0; i < nS; i++)
        delay_ms(1000);
}

void delay_xs(uint32_t nS)
{
    for (uint32_t i = 0; i < nS; i++)
        delay_xms(1000);
}