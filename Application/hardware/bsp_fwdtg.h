/**
 * @file    bsp_fwdtg.h
 * @brief   看门狗驱动头文件 — 片内 FWDGT + 外置 SGM706
 */

#ifndef _BSP_FWDTG_H_
#define _BSP_FWDTG_H_

#include "include.h"

/* ============================================================
 * 看门狗 API 函数声明
 * ============================================================ */

/* 【固定区】完整看门狗初始化：片内 FWDGT 配置 + 外置 SGM706 GPIO 初始化 */
void Bsp_fwdtgInit(void);

/* 【固定区】使能片内看门狗（必须先配置，再使能） */
void Fwdtg_enabale(void);

/* 喂狗函数（注意：函数名拼写为 Feet_Fwdtg，实际功能为 Feed） */
void Feet_Fwdtg(void);

/* ============================================================
 * 仅外置看门狗（SGM706）相关 API
 * ============================================================ */

/* 仅初始化外置看门狗 GPIO（不涉及片内 FWDGT） */
void Bsp_fwdtgInit_hw(void);

/* 仅喂外置看门狗（GPIO 翻转，不重装载 FWDGT） */
void Feed_Fwdtg_hw(void);

#endif /* _BSP_FWDTG_H_ */
