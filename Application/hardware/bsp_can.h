/**
 * @file    bsp_can.h
 * @brief   CAN 总线驱动 — 头文件（保留互斥量保护模式）
 */

#ifndef _BSP_CAN_H_
#define _BSP_CAN_H_

#include "include.h"

/* ============================================================
 * CAN 总线波特率定义
 * ============================================================ */
#define CAN_250K_BPS    250
#define CAN_500K_BPS    500
#define CAN_1M_BPS      1000

/* ============================================================
 * 函数声明
 * ============================================================ */
/**
 * CAN0 初始化
 * @param Bps  波特率（250/500/1000）
 */
void CAN0_Init(uint16_t Bps);

/**
 * CAN1 初始化
 * @param Bps  波特率（250/500/1000）
 */
void CAN1_Init(uint16_t Bps);

/**
 * CAN0 发送消息
 * @param ID        CAN ID
 * @param ide       帧类型（1=扩展帧，0=标准帧）
 * @param pdata     数据指针
 * @param dataLen  数据长度（0~8）
 */
void Can0SendMsg(uint32_t ID, uint8_t ide, uint8_t *pdata, uint8_t dataLen);

/**
 * CAN1 发送消息
 * @param ID        CAN ID
 * @param ide       帧类型（1=扩展帧，0=标准帧）
 * @param pdata     数据指针
 * @param dataLen  数据长度（0~8）
 */
void Can1SendMsg(uint32_t ID, uint8_t ide, uint8_t *pdata, uint8_t dataLen);

#endif /* _BSP_CAN_H_ */