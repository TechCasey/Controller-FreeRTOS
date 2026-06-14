/**
 * @file    debug.c
 * @brief   调试日志输出（SEGGER RTT 封装）
 *
 * 功能：通过 J-Link RTT 通道输出带级别的日志信息。
 * 编译控制：DEBUG_MODE = 0 时只输出错误和警告。
 */

#include "debug.h"

extern uint8_t DEBUG_MODE;

void debug_printf(uint8_t info_level, const char *fmt, ...)
{
    /* DEBUG_MODE 在 config.h 中定义 */
#if (DEBUG_MODE == 1)
    (void)info_level;  /* 调试模式全开，忽略级别 */
#else
    /* 非调试模式：只输出错误和警告 */
    if (info_level > INFO_WARNING) return;
#endif

    SEGGER_RTT_SetTerminal(info_level);

    va_list ParamList;
    va_start(ParamList, fmt);
    SEGGER_RTT_vprintf(0, fmt, ParamList);
    va_end(ParamList);
}