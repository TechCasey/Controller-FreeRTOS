/**
 * @file    gd32a50x_it.c
 * @brief   中断服务程序 — 学习版（FreeRTOS 中断安全模式）
 *
 * =============================================================================
 * 核心学习目标：硬件中断如何安全地与 FreeRTOS 任务通信
 * =============================================================================
 *
 * 规则①：中断处理函数（ISR）中不能直接调用普通 FreeRTOS API
 *         → 必须使用 "FromISR" 版本的 API
 *
 * 规则②：中断优先级必须 > configMAX_SYSCALL_INTERRUPT_PRIORITY 才能调用 FromISR
 *         → 本项目中 configMAX_SYSCALL_INTERRUPT_PRIORITY = 5
 *         → 中断优先级 0~4 可以调用 FromISR，5~15 不行
 *
 * 规则③：FromISR 调用后如果触发了更高优先级任务，需要请求 PendSV 切换
 *         → 使用 portYIELD_FROM_ISR(xHigherPriorityTaskWoken)
 *
 * 规则④：进入/退出 ISR 时需要使用临界段保护
 *         → taskENTER_CRITICAL_FROM_ISR() / taskEXIT_CRITICAL_FROM_ISR()
 *         → 防止中断嵌套导致数据竞争
 *
 * =============================================================================
 * 程序运行流程（CAN 中断为例）：
 *
 * Step 1 — CAN 总线收到数据 → 触发 CAN0_Message_IRQHandler
 * Step 2 — 读取 CAN 硬件 FIFO
 * Step 3 — 进入临界段（taskENTER_CRITICAL_FROM_ISR）
 * Step 4 — 打包为 CAN_Msg_t 结构体
 * Step 5 — xQueueSendToBackFromISR 发送到队列（FromISR 版本）
 * Step 6 — portYIELD_FROM_ISR 请求任务切换
 * Step 7 — 退出临界段（taskEXIT_CRITICAL_FROM_ISR）
 * Step 8 — 中断返回，PendSV 触发 → 切换到 CAN 接收任务处理数据
 * =============================================================================
 */

#include "gd32a50x_it.h"
#include "include.h"

/* ============================================================
 * 外部变量声明（由 CanComm.c / RCM.c 定义）
 * ============================================================ */
extern CanComm_t s_CanComm;

/* ============================================================
 * CAN 中断服务程序
 * ============================================================ */

/**
 * CAN0 中断处理函数
 * ─────────────────────────────────────────────────────────────
 * Step 1 — 触发：CAN0 总线收到数据
 * Step 2 — 读取 CAN 硬件 RX FIFO
 * Step 3 — 进入临界段（防止中断嵌套）
 * Step 4 — 打包 CAN 帧
 * Step 5 — xQueueSendToBackFromISR → 发送到接收队列
 * Step 6 — portYIELD_FROM_ISR → 请求任务切换
 * Step 7 — 退出临界段
 * Step 8 — 中断返回 → PendSV 触发
 * ─────────────────────────────────────────────────────────────
 *
 * 调用 xQueueSendToBackFromISR 的效果：
 *   CAN 帧进入队列 → 接收任务解除阻塞 → 任务获得 CPU → 处理数据
 */
void CAN0_Message_IRQHandler(void)
{
    /* Step 3 ──► 进入临界段（嵌套中断保护）
     * taskENTER_CRITICAL_FROM_ISR() 读取当前中断优先级，
     * 并屏蔽所有优先级 <= 当前优先级的中断。
     * 返回值 ulReturn 用于退出时恢复之前的中断状态。
     */
    uint32_t ulReturn = taskENTER_CRITICAL_FROM_ISR();

    /* Step 2 ──► 检查 CAN 中断标志（FIFO 有数据） */
    if (RESET != can_interrupt_flag_get(CAN0, CAN_INT_FLAG_FIFO_AVAILABLE)) {

        /* 读取 CAN RX FIFO */
        can_rx_fifo_struct can0_rx_fifo;
        can_rx_fifo_read(CAN0, &can0_rx_fifo);

        /* Step 4 ──► 打包 CAN 消息帧 */
        CAN_Msg_t msg = {0};
        msg.ide  = can0_rx_fifo.ide;
        msg.id   = can0_rx_fifo.id;
        msg.dlc  = can0_rx_fifo.dlc;
        memcpy(msg.data, can0_rx_fifo.data, msg.dlc);

        /* Step 5 ──► 发送到队列（FromISR 版本）
         * xHigherPriorityTaskWoken 用于判断是否需要任务切换。
         * 如果发送前队列是满的，发送后变非满，不会切换。
         * 如果发送前队列是空的，发送后变非空，
         *   且接收任务优先级高于当前中断前运行的任务 → 设为 pdTRUE。
         */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendToBackFromISR(s_CanComm.RecvQueue, &msg, &xHigherPriorityTaskWoken);

        /* Step 6 ──► 请求 PendSV 中断触发任务切换
         * 如果 xHigherPriorityTaskWoken = pdTRUE，
         * portYIELD_FROM_ISR 会触发 PendSV，
         * 中断返回后 CPU 会切换到更高优先级的接收任务。
         * 如果 = pdFALSE，则不触发，无额外开销。
         */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

        /* 清除 CAN FIFO 中断标志 */
        can_interrupt_flag_clear(CAN0, CAN_INT_FLAG_FIFO_AVAILABLE);
    }

    /* Step 7 ──► 退出临界段（恢复之前的中断状态）
     * taskEXIT_CRITICAL_FROM_ISR(ulReturn) 根据之前的
     * 中断状态恢复中断使能位。
     */
    taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}

/* ============================================================
 * CAN1 中断处理函数
 * 流程同 CAN0（参考上面 Step 1~8）
 * CAN1 用于连接下层设备（排种器/施肥电机控制单元）
 * ============================================================ */
extern QueueHandle_t s_RCM_RecvQueue;  /* 在 RCM.c 中定义 */

void CAN1_Message_IRQHandler(void)
{
    uint32_t ulReturn = taskENTER_CRITICAL_FROM_ISR();

    if (RESET != can_interrupt_flag_get(CAN1, CAN_INT_FLAG_FIFO_AVAILABLE)) {

        can_rx_fifo_struct can1_rx_fifo;
        can_rx_fifo_read(CAN1, &can1_rx_fifo);

        CAN_Msg_t msg = {0};
        msg.ide = can1_rx_fifo.ide;
        msg.id  = can1_rx_fifo.id;
        msg.dlc = can1_rx_fifo.dlc;
        memcpy(msg.data, can1_rx_fifo.data, msg.dlc);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        /* s_RCM_RecvQueue 在 RCM.c 中定义 */
        if (s_RCM_RecvQueue != NULL) {
            xQueueSendToBackFromISR(s_RCM_RecvQueue, &msg, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }

        can_interrupt_flag_clear(CAN1, CAN_INT_FLAG_FIFO_AVAILABLE);
    }

    taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}

/* ============================================================
 * 系统定时器中断（TIMER5，1ms 周期）
 * 用于 ADC 看门狗和通用计时
 * ============================================================ */

/**
 * TIMER5 中断处理（1ms 周期）
 * ⚠️ 注意：这个中断优先级较低，不能调用 FromISR 系列 API
 *        只能做简单计数或设置标志，由任务轮询处理
 */
void TIMER5_DAC_IRQHandler(void)
{
    if (SET == timer_interrupt_flag_get(TIMER5, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);
    }
}

/* ============================================================
 * 异常处理（硬件错误）
 * ============================================================ */
void NMI_Handler(void) { }
void DebugMon_Handler(void) { }

/**
 * HardFault 中断处理
 * 当程序执行遇到不可恢复的错误时触发（总线错误、内存错误等）
 */
void printHardFault(uint32_t *hardfaultArgs)
{
    /* 打印 ARM 寄存器（R0~R12, LR, PC, PSR）辅助调试 */
    debug_printf(INFO_ERR, "\r\n[Hard Fault]\r\n");
    debug_printf(INFO_ERR, "R0  = %#x\r\n", (unsigned int)hardfaultArgs[0]);
    debug_printf(INFO_ERR, "R1  = %#x\r\n", (unsigned int)hardfaultArgs[1]);
    debug_printf(INFO_ERR, "R2  = %#x\r\n", (unsigned int)hardfaultArgs[2]);
    debug_printf(INFO_ERR, "R3  = %#x\r\n", (unsigned int)hardfaultArgs[3]);
    debug_printf(INFO_ERR, "R12 = %#x\r\n", (unsigned int)hardfaultArgs[4]);
    debug_printf(INFO_ERR, "LR  = %#x\r\n", (unsigned int)hardfaultArgs[5]);
    debug_printf(INFO_ERR, "PC  = %#x\r\n", (unsigned int)hardfaultArgs[6]);
    debug_printf(INFO_ERR, "PSR = %#x\r\n", (unsigned int)hardfaultArgs[7]);
    debug_printf(INFO_ERR, "BFAR = %#x\r\n", (*((volatile unsigned int *)(0xE000ED38))));
    debug_printf(INFO_ERR, "CFSR  = %#x\r\n", (*((volatile unsigned int *)(0xE000ED28))));
    debug_printf(INFO_ERR, "HFSR  = %#x\r\n", (*((volatile unsigned int *)(0xE000ED2C))));

    while (1) { /* 停在这里等待调试器连接 */ }
}

void HardFault_Handler(void)
{
    __asm volatile (
        "   tst lr, #4                                       \n"
        "   ite eq                                           \n"
        "   mrseq r0, msp                                    \n"
        "   mrsne r0, psp                                    \n"
        "   ldr r1, =printHardFault                          \n"
        "   bx r1                                            \n"
    );
}

void MemManage_Handler(void)
{
    while (1) { debug_printf(INFO_ERR, "MemManage_Handler\r\n"); }
}
void BusFault_Handler(void)
{
    while (1) { debug_printf(INFO_ERR, "BusFault_Handler\r\n"); }
}
void UsageFault_Handler(void)
{
    while (1) { debug_printf(INFO_ERR, "UsageFault_Handler\r\n"); }
}