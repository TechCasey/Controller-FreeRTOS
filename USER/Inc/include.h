/**
 * @file    include.h
 * @brief   全局头文件 — 学习版（FreeRTOS 聚焦版）
 *
 * =============================================================================
 * 精简说明：
 * 原始 include.h 包含了大量业务模块头文件，这里只保留：
 *   1. C 标准库
 *   2. FreeRTOS 核心头文件
 *   3. GD32 芯片头文件
 *   4. BSP 硬件抽象层头文件
 *   5. 类型定义
 *
 * 删除了所有业务模块头文件（jingbo_seed.c, seed.c, alarm.c 等），
 * 只保留 simple_example.c 和 CanComm.c 示例。
 * =============================================================================
 */

#ifndef __INCLUDE_H__
#define __INCLUDE_H__

/* ============================================================
 * C 标准库
 * ============================================================ */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================
 * GD32 芯片头文件
 * ============================================================ */
#include "gd32a50x.h"
#include "gd32a50x_libopt.h"

/* ============================================================
 * SEGGER RTT 调试输出（通过 J-Link 在调试时打印）
 * ============================================================ */
#include "SEGGER_RTT.h"
#include "SEGGER_SYSVIEW.h"

/* ============================================================
 * FreeRTOS 核心头文件
 * ============================================================ */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"
#include "event_groups.h"

/* ============================================================
 * 项目内部头文件
 * ============================================================ */
#include "config.h"
#include "type.h"
#include "systick.h"
#include "main.h"
#include "debug.h"

/* ============================================================
 * BSP 硬件抽象层头文件（保留硬件驱动）
 * ============================================================ */
#include "bsp_fwdtg.h"
#include "bsp_gpio.h"
#include "bsp_can.h"
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "bsp_timer.h"
#include "bsp_pwm.h"
#include "bsp_timer_input_capture.h"
#include "bsp_i2c.h"
#include "bsp_fmc.h"
#include "bsp_adc.h"
#include "w25qxx.h"
#include "eeprom.h"

/* ============================================================
 * 示例模块头文件（CUSTOMIZE: 在此添加你自己的模块头文件）
 * ============================================================ */
#include "simple_example.h"
#include "CanComm.h"

#endif /* __INCLUDE_H__ */