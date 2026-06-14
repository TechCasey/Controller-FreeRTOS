/**
 * @file    systick.h
 * @brief   SysTick 延时驱动 — 头文件（带注释学习版）
 */

#ifndef _SYSTICK_H_
#define _SYSTICK_H_

#include "include.h"

/**
 * SysTick 初始化（FreeRTOS 心跳，1ms 中断）
 * 调用位置：main.c Step 4
 */
void SysTick_Init(void);

/**
 * 微秒延时（纯硬件延时，不使用 OS）
 * @param uiNus 微秒数
 */
void delay_xus(uint32_t uiNus);

/**
 * 毫秒延时（RTOS 感知，自动切换）
 * @param nms 毫秒数
 */
void delay_ms(uint32_t nms);

/**
 * 毫秒延时（纯硬件延时，强制使用 delay_xus）
 * @param nms 毫秒数
 */
void delay_xms(uint32_t nms);

/**
 * 秒延时（RTOS 感知）
 * @param nS 秒数
 */
void delay_s(uint32_t nS);

/**
 * 秒延时（纯硬件延时）
 * @param nS 秒数
 */
void delay_xs(uint32_t nS);

#endif /* _SYSTICK_H_ */
