/**
 * @file    osbasetimer.h
 * @brief   FreeRTOS 运行时间统计定时器 — 头文件
 */

#ifndef OS_BASETIMER_H_
#define OS_BASETIMER_H_

#include "gd32a50x_libopt.h"

/* ============================================================
 * 外部声明
 * ============================================================ */
extern unsigned long long FreeRTOSRunTimeTicks;

/* ============================================================
 * 函数声明
 * ============================================================ */
/**
 * 初始化 Timer7 为 50µs 周期定时器
 * 用于 FreeRTOS 运行时间统计（configGENERATE_RUN_TIME_STATS）
 */
void Timer7_50us(void);

#endif /* OS_BASETIMER_H_ */