/**
 * @file    bsp_can.c
 * @brief   CAN 总线驱动 — 实现文件
 *
 * 保留原始 CAN 驱动代码和互斥量保护模式。
 * 删除了所有业务相关注释。
 */

#include "bsp_can.h"

/* ============================================================
 * 静态变量：CAN 总线互斥量
 * ─────────────────────────────────────────────────────────────
 * Step 1 ──► xSemaphoreCreateMutex() 在 CAN_Init 中创建
 * Step 2 ──► Can0SendMsg / Can1SendMsg 使用 xSemaphoreTake/Give 保护
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 框架固定区：互斥量是 CAN 发送保护的唯一正确方式，不要删除！
 * ============================================================ */
static SemaphoreHandle_t CAN0_MutexHandle = NULL;
static SemaphoreHandle_t CAN1_MutexHandle = NULL;

/* ============================================================
 * 静态函数声明
 * ============================================================ */
static void CAN0_GPIO_Init(void);
static void CAN0_Config(uint16_t Bps);
static void CAN0_FIFO_Config(void);
static void CAN1_GPIO_Init(void);
static void CAN1_Config(uint16_t Bps);
static void CAN1_FIFO_Config(void);

/* ============================================================
 * Step 1 — CAN0 互斥量初始化
 * ============================================================ */
static void CAN0_MutexInit(void)
{
    if (CAN0_MutexHandle != NULL) return;

    /* Step 1 ──► 创建互斥量
     * xSemaphoreCreateMutex() 创建优先级继承互斥量
     * 用于保护 CAN0 总线，同一时刻只有一个任务能发送
     */
    CAN0_MutexHandle = xSemaphoreCreateMutex();

    if (CAN0_MutexHandle == NULL) {
        debug_printf(INFO_ERR, "CAN0_Mutex create fail\r\n");
        configASSERT(CAN0_MutexHandle);
    }
}

/* ============================================================
 * Step 1 — CAN1 互斥量初始化
 * ============================================================ */
static void CAN1_MutexInit(void)
{
    if (CAN1_MutexHandle != NULL) return;

    CAN1_MutexHandle = xSemaphoreCreateMutex();

    if (CAN1_MutexHandle == NULL) {
        debug_printf(INFO_ERR, "CAN1_Mutex create fail\r\n");
        configASSERT(CAN1_MutexHandle);
    }
}

/* ============================================================
 * CAN0 GPIO 初始化
 * ============================================================ */
static void CAN0_GPIO_Init(void)
{
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_3);
    gpio_af_set(GPIOA, GPIO_AF_6, GPIO_PIN_3);

    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_4);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_4);
    gpio_af_set(GPIOA, GPIO_AF_6, GPIO_PIN_4);
}

/* ============================================================
 * CAN0 FIFO 过滤器配置
 * ============================================================ */
static void CAN0_FIFO_Config(void)
{
    can_fifo_parameter_struct cfg;
    cfg.dma_enable = DISABLE;
    cfg.filter_format_and_number = CAN_RXFIFO_FILTER_A_NUM_8;  /* 8 个过滤器槽 */
    cfg.fifo_public_filter = FIFO_FILTER_ID_EXD_A(0xFFFF00FF);

    can_rx_fifo_config(CAN0, &cfg);

    can_rx_fifo_id_filter_struct f[8];
    for (int i = 0; i < 8; i++)
        can_struct_para_init(CAN_FIFO_ID_FILTER_STRUCT, &f[i]);

    /* 过滤 ID：只接收上位机发来的下行参数设置帧 */
    f[0].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[0].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[0].id             = 0x18FF00F5;

    f[1].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[1].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[1].id             = 0x18FF00D2;

    can_rx_fifo_filter_table_config(CAN0, f);
}

/* ============================================================
 * CAN0 控制器配置
 * ============================================================ */
static void CAN0_Config(uint16_t Bps)
{
    /* APB2 = 100MHz, CAN 时钟 = 100MHz/2 = 50MHz */
    rcu_can_clock_config(CAN0, RCU_CANSRC_PCLK2_DIV_2);
    rcu_periph_clock_enable(RCU_CAN0);

    can_deinit(CAN0);

    can_parameter_struct p;
    can_struct_para_init(CAN_INIT_STRUCT, &p);
    p.internal_counter_source  = CAN_TIMER_SOURCE_BIT_CLOCK;
    p.self_reception           = DISABLE;
    p.mb_tx_order              = CAN_TX_HIGH_PRIORITY_MB_FIRST;
    p.mb_tx_abort_enable       = ENABLE;
    p.local_priority_enable    = DISABLE;
    p.mb_rx_ide_rtr_type       = CAN_IDE_RTR_FILTERED;
    p.mb_remote_frame          = CAN_STORE_REMOTE_REQUEST_FRAME;
    p.rx_private_filter_queue_enable = DISABLE;
    p.edge_filter_enable       = DISABLE;
    p.protocol_exception_enable = DISABLE;
    p.rx_filter_order          = CAN_RX_FILTER_ORDER_FIFO_FIRST;
    p.memory_size              = CAN_MEMSIZE_32_UNIT;
    p.mb_public_filter         = 0x0;
    p.resync_jump_width         = 1;
    p.prop_time_segment        = 2;
    p.time_segment_1           = 4;
    p.time_segment_2           = 3;
    p.prescaler                = 50000 / Bps / (1 + 2 + 4 + 3);

    can_init(CAN0, &p);

    /* CAN 中断优先级 = 6（低于 configMAX_SYSCALL_INTERRUPT_PRIORITY=5）
     * ⚠️ 重要：此优先级下不能调用 FreeRTOS FromISR API！
     * 如果需要 FromISR 调用，请将优先级改为 0~4
     */
    nvic_irq_enable(CAN0_Message_IRQn, 6, 0);

    can_interrupt_enable(CAN0, CAN_INT_FIFO_AVAILABLE);  /* FIFO 非空中断 */
    CAN0_FIFO_Config();
    can_operation_mode_enter(CAN0, CAN_NORMAL_MODE);
}

/* ============================================================
 * CAN1 GPIO 初始化
 * ============================================================ */
static void CAN1_GPIO_Init(void)
{
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_12);
    gpio_af_set(GPIOC, GPIO_AF_6, GPIO_PIN_12);

    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_0);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_0);
    gpio_af_set(GPIOD, GPIO_AF_6, GPIO_PIN_0);
}

/* ============================================================
 * CAN1 FIFO 过滤器配置
 * ============================================================ */
static void CAN1_FIFO_Config(void)
{
    can_fifo_parameter_struct cfg;
    cfg.dma_enable = DISABLE;
    cfg.filter_format_and_number = CAN_RXFIFO_FILTER_A_NUM_8;
    cfg.fifo_public_filter = FIFO_FILTER_ID_EXD_A(0xFFFF0000);

    can_rx_fifo_config(CAN1, &cfg);

    can_rx_fifo_id_filter_struct f[8];
    for (int i = 0; i < 8; i++)
        can_struct_para_init(CAN_FIFO_ID_FILTER_STRUCT, &f[i]);

    /* CAN1 连接下层设备（排种器从控/施肥电机驱动） */
    f[0].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[0].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[0].id             = 0xA101;

    f[1].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[1].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[1].id             = 0xB101;

    f[2].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[2].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[2].id             = 0x18FF20D2;

    f[3].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[3].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[3].id             = 0x18FF00D2;

    f[4].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[4].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[4].id             = 0x18BD0000;

    f[5].remote_frame   = CAN_DATA_FRAME_ACCEPTED;
    f[5].extended_frame = CAN_EXTENDED_FRAME_ACCEPTED;
    f[5].id             = 0x17000000;

    can_rx_fifo_filter_table_config(CAN1, f);
}

/* ============================================================
 * CAN1 控制器配置
 * ============================================================ */
static void CAN1_Config(uint16_t Bps)
{
    rcu_can_clock_config(CAN1, RCU_CANSRC_PCLK2_DIV_2);
    rcu_periph_clock_enable(RCU_CAN1);

    can_deinit(CAN1);

    can_parameter_struct p;
    can_struct_para_init(CAN_INIT_STRUCT, &p);
    p.internal_counter_source  = CAN_TIMER_SOURCE_BIT_CLOCK;
    p.self_reception           = DISABLE;
    p.mb_tx_order              = CAN_TX_HIGH_PRIORITY_MB_FIRST;
    p.mb_tx_abort_enable       = ENABLE;
    p.local_priority_enable    = DISABLE;
    p.mb_rx_ide_rtr_type       = CAN_IDE_RTR_FILTERED;
    p.mb_remote_frame          = CAN_STORE_REMOTE_REQUEST_FRAME;
    p.rx_private_filter_queue_enable = DISABLE;
    p.edge_filter_enable       = DISABLE;
    p.protocol_exception_enable = DISABLE;
    p.rx_filter_order          = CAN_RX_FILTER_ORDER_MAILBOX_FIRST;
    p.memory_size              = CAN_MEMSIZE_32_UNIT;
    p.mb_public_filter         = 0x0;
    p.resync_jump_width         = 1;
    p.prop_time_segment        = 2;
    p.time_segment_1           = 4;
    p.time_segment_2           = 3;
    p.prescaler                = 50000 / Bps / (1 + 2 + 4 + 3);

    can_init(CAN1, &p);

    nvic_irq_enable(CAN1_Message_IRQn, 6, 0);
    can_interrupt_enable(CAN1, CAN_INT_FIFO_AVAILABLE);
    CAN1_FIFO_Config();
    can_operation_mode_enter(CAN1, CAN_NORMAL_MODE);
}

/* ============================================================
 * 公开初始化函数
 * ============================================================ */
void CAN0_Init(uint16_t Bps)
{
    CAN0_MutexInit();   /* 创建互斥量 */
    CAN0_GPIO_Init();
    CAN0_Config(Bps);
}

void CAN1_Init(uint16_t Bps)
{
    CAN1_MutexInit();   /* 创建互斥量 */
    CAN1_GPIO_Init();
    CAN1_Config(Bps);
}

/* ============================================================
 * Step 2 — CAN0 发送（互斥量保护）
 * ─────────────────────────────────────────────────────────────
 * Step 2.1 ──► xSemaphoreTake(CAN0_MutexHandle) ──► 获取互斥量
 * Step 2.2 ──► can_message_transmit() ──► 发送 CAN 帧
 * Step 2.3 ──► xSemaphoreGive(CAN0_MutexHandle) ──► 释放互斥量
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 框架固定区：互斥量保护不要删除！
 *   多任务同时发送 CAN 帧会导致总线冲突。
 * ============================================================ */
void Can0SendMsg(uint32_t ID, uint8_t ide, uint8_t *pdata, uint8_t dataLen)
{
    /* Step 2.1 ──► 获取互斥量 */
    if (CAN0_MutexHandle != NULL) {
        xSemaphoreTake(CAN0_MutexHandle, portMAX_DELAY);
    }

    /* Step 2.2 ──► 构造并发送 CAN 帧 */
    can_trasnmit_message_struct tx_msg;
    can_struct_para_init(CAN_TRASMIT_STRUCT, &tx_msg);

    tx_msg.tx_frame_type   = CAN_TX_FRAME_DATA;
    tx_msg.tx_func_frame_code = ide ? CAN_EXTENDED_FRAME : CAN_STANDARD_FRAME;
    tx_msg.tx_buf_num     = CAN_TX_BUFFER_FIRST;
    tx_msg.id             = ID;
    tx_msg.tx_dlen        = dataLen;

    for (uint8_t i = 0; i < dataLen; i++)
        tx_msg.tx_data[i] = pdata[i];

    can_message_transmit(CAN0, &tx_msg);

    /* Step 2.3 ──► 释放互斥量 */
    if (CAN0_MutexHandle != NULL) {
        xSemaphoreGive(CAN0_MutexHandle);
    }
}

/* ============================================================
 * Step 2 — CAN1 发送（互斥量保护，同上）
 * ============================================================ */
void Can1SendMsg(uint32_t ID, uint8_t ide, uint8_t *pdata, uint8_t dataLen)
{
    if (CAN1_MutexHandle != NULL) {
        xSemaphoreTake(CAN1_MutexHandle, portMAX_DELAY);
    }

    can_trasnmit_message_struct tx_msg;
    can_struct_para_init(CAN_TRASMIT_STRUCT, &tx_msg);

    tx_msg.tx_frame_type   = CAN_TX_FRAME_DATA;
    tx_msg.tx_func_frame_code = ide ? CAN_EXTENDED_FRAME : CAN_STANDARD_FRAME;
    tx_msg.tx_buf_num     = CAN_TX_BUFFER_FIRST;
    tx_msg.id             = ID;
    tx_msg.tx_dlen        = dataLen;

    for (uint8_t i = 0; i < dataLen; i++)
        tx_msg.tx_data[i] = pdata[i];

    can_message_transmit(CAN1, &tx_msg);

    if (CAN1_MutexHandle != NULL) {
        xSemaphoreGive(CAN1_MutexHandle);
    }
}