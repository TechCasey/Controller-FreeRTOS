/**
 * @file    systick.h
 * @brief   SysTick 延时驱动 — 头文件
 */

#ifndef _SYSTICK_H_
#define _SYSTICK_H_

#include "include.h"

/**
 * 初始化 SysTick（FreeRTOS 心跳，1ms 中断）
 */
void SysTick_Init(void);

/**
 * 微秒延时（纯硬件延时，不使用 OS）
 * @param uiNus 微秒数
 */
void delay_xus(uint32_t uiNus);

/**
 * 毫秒延时（检测 OS 状态，OS 运行时用 vTaskDelay）
 * @param nms 毫秒数
 */
void delay_ms(uint32_t nms);

/**
 * 毫秒延时（纯硬件延时）
 * @param nms 毫秒数
 */
void delay_xms(uint32_t nms);

/**
 * 秒延时（纯硬件延时）
 * @param nS 秒数
 */
void delay_s(uint32_t nS);

/**
 * 秒延时（纯硬件延时）
 * @param nS 秒数
 */
void delay_xs(uint32_t nS);

#endif /* _SYSTICK_H_ */