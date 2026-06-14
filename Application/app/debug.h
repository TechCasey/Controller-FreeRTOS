/**
 * @file    debug.h
 * @brief   调试日志级别定义
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include "include.h"

/* ============================================================
 * 日志级别（数字越小越重要）
 * ============================================================ */
#define INFO_ERR                  0   /* 错误（始终输出） */
#define INFO_WARNING              1   /* 警告 */
#define INFO_REALIZE              2   /* 重要信息 */
#define INFO_PARAM_CHANCE         3   /* 参数变更 */
#define INFO_PARAM_OFTEN          4   /* 常用参数 */
#define INFO_PARAM_ALWAYS         5   /* 持续参数 */
#define INFO_PARAM_IMPORTANT      6   /* 重要参数 */
#define INFO_PARAM_CTRL           7   /* 控制参数 */
#define INFO_LFS                  8   /* 文件系统 */
#define INFO_UPGRADE              9   /* 升级相关 */
#define INFO_TEST                10   /* 测试用 */
#define INFO_ORDINARY            11   /* 普通信息 */

/* ============================================================
 * 函数声明
 * ============================================================ */
void debug_printf(uint8_t info_level, const char *fmt, ...);

#endif /* _DEBUG_H_ */