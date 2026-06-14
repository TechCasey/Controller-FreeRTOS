/**
 * @file    bsp_uart.c
 * @brief   UART 驱动 — 带注释学习版（GD32A50x）
 *
 * =============================================================================
 * 功能说明：
 *   UART0 ── RS232 ── 上位机/调试终端
 *   UART1 ── RS485 ── 工业外设通信
 *   UART2 ── TTL   ── 子芯片通信（当前代码注释掉）
 *
 * 工程技巧：
 *   ① GPIO 复用功能配置（AF5/AF4）
 *   ② USART 参数配置（波特率/字长/停止位/校验位）
 *   ③ DMA 通道配置（内存→外设，USART0 TX）
 *   ④ 三路 UART 互斥量创建（xSemaphoreCreateMutex）
 *   ⑤ 互斥量保护轮询发送（Uart_SendData）
 *   ⑥ DMA 发送（uart_dma_send，等待上一帧完成）
 *   ⑦ printf 重定向到 USART（fputc + _sys_exit）
 * =============================================================================
 */

#include "bsp_uart.h"

/* ============================================================
 * 【固定区】宏定义
 * ============================================================ */
#define DMA_BUF_SIZE    2048   /* DMA 发送缓冲区大小 */

/* ============================================================
 * 【固定区】互斥量句柄（三路 UART 各一个互斥量）
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► CreateMutexInit() 中 xSemaphoreCreateMutex 创建
 *   Step 2 ──► Uart_SendData 中 xSemaphoreTake/Give 使用
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：互斥量机制不要改动！
 *   多个任务可能同时调用 Uart_SendData，互斥量防止总线冲突。
 * ============================================================ */
static SemaphoreHandle_t UART0_MutexHandle = NULL;
static SemaphoreHandle_t UART1_MutexHandle = NULL;
static SemaphoreHandle_t UART2_MutexHandle = NULL;

/* ============================================================
 * 【固定区】DMA 发送缓冲区
 * ─────────────────────────────────────────────────────────────
 * uart_dma_send() 将待发送数据拷贝到此缓冲区，
 * 然后由 DMA 控制器自动将数据从内存搬到 USART TX 寄存器。
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：DMA 缓冲区大小和类型不要改动！
 * ============================================================ */
static uint8_t Uart_DMA_TX_Buf[DMA_BUF_SIZE + 1] = {0};

/* ============================================================
 * 【运行时步骤 1/2/3】UART0 初始化（RS232，上位机通信）
 * ─────────────────────────────────────────────────────────────
 * 搭建步骤：
 *   Step 1 ──► rcu_periph_clock_enable(RCU_USART0) ──► 使能 USART0 时钟
 *   Step 2 ──► gpio_af_set(GPIOB, AF5, PIN13/14) ──► 配置 TX/RX 引脚复用
 *   Step 3 ──► gpio_mode_set + gpio_output_options_set ──► 配置引脚模式
 *   Step 4 ──► usart_deinit + usart_xxx_set ──► 配置 USART 参数
 *   Step 5 ──► usart_enable + nvic_irq_enable ──► 使能 USART 和中断
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】可修改内容：
 *   - 波特率 baudval 参数（默认 19200）
 *   - 中断优先级（当前 NVIC 优先级 7）
 *   - 字长/停止位/校验位
 * ============================================================ */
void Uart0_Init(uint32_t baudval)
{
    /* Step 1 ──► 使能 USART0 时钟 */
    rcu_periph_clock_enable(RCU_USART0);

    /* Step 2 ──► 配置 TX 引脚复用（PB13 = USART0_TX，AF5） */
    gpio_af_set(GPIOB, GPIO_AF_5, GPIO_PIN_13);
    /* Step 2 ──► 配置 RX 引脚复用（PB14 = USART0_RX，AF5） */
    gpio_af_set(GPIOB, GPIO_AF_5, GPIO_PIN_14);

    /* Step 3 ──► 配置 USART TX 为推挽输出（PB13） */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_13);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_13);

    /* Step 3 ──► 配置 USART RX 为复用推挽输入（PB14） */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_14);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_14);

    /* Step 4 ──► USART 参数配置 */
    usart_deinit(USART0);
    usart_word_length_set(USART0, USART_WL_8BIT);        /* 8 位字长 */
    usart_stop_bit_set(USART0, USART_STB_1BIT);         /* 1 位停止位 */
    usart_parity_config(USART0, USART_PM_NONE);         /* 无校验 */
    usart_baudrate_set(USART0, baudval);                /* 波特率（可自定义） */
    usart_receive_config(USART0, USART_RECEIVE_ENABLE); /* 使能接收 */
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE); /* 使能发送 */

    /* Step 5 ──► 使能 USART，清除接收标志 */
    usart_enable(USART0);
    usart_flag_clear(USART0, USART_FLAG_RBNE);

    /* Step 5 ──► 配置 NVIC 中断，使能 USART0 RX 中断 */
    nvic_irq_enable(USART0_IRQn, 7, 0);                 /* 【自定义区】优先级可调 */
    usart_interrupt_enable(USART0, USART_INT_RBNE);     /* 接收非空中断 */
}

/* ============================================================
 * 【运行时步骤 1/2/3】UART0 DMA 配置
 * ─────────────────────────────────────────────────────────────
 * 搭建步骤：
 *   Step 1 ──► rcu_periph_clock_enable(RCU_DMA0 + RCU_DMAMUX) ──► 使能 DMA 时钟
 *   Step 2 ──► dma_deinit + dma_struct_para_init + dma_init ──► 初始化 DMA 通道
 *   Step 3 ──► dma_circulation_disable ──► 关闭循环模式
 *   Step 4 ──► dma_channel_enable ──► 使能 DMA 通道
 *   Step 5 ──► usart_dma_transmit_config ──► 使能 USART DMA 发送
 * ─────────────────────────────────────────────────────────────
 * DMA 传输方向：内存 → 外设（USART0 TX 寄存器）
 * DMA 通道：DMA0_CH2（USART0_TX 专用通道）
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】可修改内容：
 *   - DMA 通道（DMA_REQUEST_USART0_TX 固定）
 *   - 内存宽度/外设宽度（当前 8bit）
 *   - DMA 优先级
 * ============================================================ */
void Uart0_DMA_Config(void)
{
    /* Step 1 ──► 使能 DMA0 和 DMAMUX 时钟 */
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_DMAMUX);

    dma_parameter_struct dma_data_param;

    /* Step 2 ──► 初始化 DMA 通道 2（USART0 TX） */
    dma_deinit(DMA0, DMA_CH2);
    dma_struct_para_init(&dma_data_param);
    dma_data_param.request      = DMA_REQUEST_USART0_TX;   /* USART0 发送请求 */
    dma_data_param.direction    = DMA_MEMORY_TO_PERIPHERAL; /* 内存→外设 */
    dma_data_param.memory_addr  = (uint32_t)Uart_DMA_TX_Buf; /* 源地址：DMA 缓冲区 */
    dma_data_param.memory_inc   = DMA_MEMORY_INCREASE_ENABLE; /* 内存地址递增 */
    dma_data_param.memory_width = DMA_MEMORY_WIDTH_8BIT;     /* 8 位宽度 */
    dma_data_param.number       = 0;                          /* 【自定义区】初始长度 0 */
    dma_data_param.periph_addr  = ((uint32_t)&USART_TDATA(USART0)); /* 目标：USART TX 寄存器 */
    dma_data_param.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;     /* 外设地址固定 */
    dma_data_param.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;        /* 8 位宽度 */
    dma_data_param.priority     = DMA_PRIORITY_ULTRA_HIGH;          /* 最高优先级 */
    dma_init(DMA0, DMA_CH2, &dma_data_param);

    /* Step 3 ──► 关闭循环模式和内存到内存传输 */
    dma_circulation_disable(DMA0, DMA_CH2);
    dma_memory_to_memory_disable(DMA0, DMA_CH2);
    dmamux_synchronization_disable(DMAMUX_MULTIPLEXER_CH2);

    /* Step 4 ──► 使能 DMA 通道 2 */
    dma_channel_enable(DMA0, DMA_CH2);

    /* Step 5 ──► 使能 USART0 DMA 发送功能 */
    usart_dma_transmit_config(USART0, USART_TRANSMIT_DMA_ENABLE);
}

/* ============================================================
 * 【运行时步骤 1/2/3】UART1 初始化（RS485，工业外设）
 * ─────────────────────────────────────────────────────────────
 * 搭建步骤：
 *   Step 1 ──► rcu_periph_clock_enable(RCU_USART1) ──► 使能 USART1 时钟
 *   Step 2 ──► gpio_af_set ──► 配置 TX/RX 引脚复用（PB15 TX, PD8 RX）
 *   Step 3 ──► gpio_mode_set + gpio_output_options_set ──► 配置引脚模式
 *   Step 4 ──► usart_deinit + usart_xxx_set ──► 配置 USART 参数
 *   Step 5 ──► usart_enable + nvic_irq_enable ──► 使能 USART 和中断
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】可修改内容：
 *   - 波特率（默认 9600）
 *   - 引脚（RS485 通常 PB15=TX, PD8=RX）
 * ============================================================ */
void Uart1_Init(uint32_t baudval)
{
    /* Step 1 ──► 使能 USART1 时钟 */
    rcu_periph_clock_enable(RCU_USART1);

    /* Step 2 ──► 配置 TX 引脚（PB15 = USART1_TX，AF4） */
    gpio_af_set(GPIOB, GPIO_AF_4, GPIO_PIN_15);
    /* Step 2 ──► 配置 RX 引脚（PD8 = USART1_RX，AF4） */
    gpio_af_set(GPIOD, GPIO_AF_4, GPIO_PIN_8);

    /* Step 3 ──► 配置 USART TX（PB15） */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_15);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_15);

    /* Step 3 ──► 配置 USART RX（PD8） */
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_8);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_8);

    /* Step 4 ──► USART 参数配置 */
    usart_deinit(USART1);
    usart_word_length_set(USART1, USART_WL_8BIT);
    usart_stop_bit_set(USART1, USART_STB_1BIT);
    usart_parity_config(USART1, USART_PM_NONE);
    usart_baudrate_set(USART1, baudval);                  /* 【自定义区】波特率可调 */
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);

    /* Step 5 ──► 使能 USART1，清除接收标志 */
    usart_enable(USART1);
    usart_flag_clear(USART1, USART_FLAG_RBNE);

    /* Step 5 ──► 配置 NVIC 中断，使能 USART1 RX 中断 */
    nvic_irq_enable(USART1_IRQn, 7, 0);
    usart_interrupt_enable(USART1, USART_INT_RBNE);      /* 接收非空中断 */
}

/* ============================================================
 * 【固定区】UART2 初始化（TTL，子芯片通信）
 * ─────────────────────────────────────────────────────────────
 * 当前代码注释掉，保留接口备选。
 * 如需启用，取消注释并根据硬件连接配置引脚。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】如需启用 UART2：
 *   1. 取消本函数注释
 *   2. 使能 RCU_USART2 时钟
 *   3. 配置 TX=PA5, RX=PA6（AF5）
 *   4. 配置中断和 NVIC
 * ============================================================ */
// void Uart2_Init(uint32_t baudval)
// {
//     rcu_periph_clock_enable(RCU_USART2);
//     gpio_af_set(GPIOA, GPIO_AF_5, GPIO_PIN_5);
//     gpio_af_set(GPIOA, GPIO_AF_5, GPIO_PIN_6);
//     gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_5);
//     gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_5);
//     gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_6);
//     gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_6);
//     usart_deinit(USART2);
//     usart_word_length_set(USART2, USART_WL_8BIT);
//     usart_stop_bit_set(USART2, USART_STB_1BIT);
//     usart_parity_config(USART2, USART_PM_NONE);
//     usart_baudrate_set(USART2, baudval);
//     usart_receive_config(USART2, USART_RECEIVE_ENABLE);
//     usart_transmit_config(USART2, USART_TRANSMIT_ENABLE);
//     usart_enable(USART2);
//     usart_flag_clear(USART2, USART_FLAG_RBNE);
//     nvic_irq_enable(USART2_IRQn, 7, 0);
//     usart_interrupt_enable(USART2, USART_INT_RBNE);
// }

/* ============================================================
 * 【固定区】三路 UART 互斥量创建
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► xSemaphoreCreateMutex() 创建三个互斥量
 *   Step 2 ──► 检查创建是否成功（打印错误信息）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：互斥量创建不要改动！
 *   互斥量用于保护 Uart_SendData 轮询发送，
 *   确保同一时刻只有一个任务使用某路 UART 总线。
 * ============================================================ */
static void CreateMutexInit(void)
{
    /* Step 1 ──► 创建 UART0 互斥量 */
    UART0_MutexHandle = xSemaphoreCreateMutex();
    if (UART0_MutexHandle == NULL) {
        debug_printf(INFO_ERR, "UART0_MutexHandle create fail\r\n");
    }

    /* Step 1 ──► 创建 UART1 互斥量 */
    UART1_MutexHandle = xSemaphoreCreateMutex();
    if (UART1_MutexHandle == NULL) {
        debug_printf(INFO_ERR, "UART1_MutexHandle create fail\r\n");
    }

    /* Step 1 ──► 创建 UART2 互斥量 */
    UART2_MutexHandle = xSemaphoreCreateMutex();
    if (UART2_MutexHandle == NULL) {
        debug_printf(INFO_ERR, "UART2_MutexHandle create fail\r\n");
    }
}

/* ============================================================
 * 【运行时步骤 1/2】BSP UART 初始化入口
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► CreateMutexInit() ──► 创建三路互斥量
 *   Step 2 ──► Uart0_Init/Uart1_Init/Uart2_Init ──► 初始化各路 UART
 *   Step 3 ──► Uart0_DMA_Config ──► 配置 UART0 DMA（如需 DMA 发送）
 * ─────────────────────────────────────────────────────────────
 * 调用位置：main.c 中 BSP 外设初始化阶段（Step 7）
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】根据需要取消注释对应初始化调用：
 *   - Uart0_Init(19200);    // RS232，上位机通信
 *   - Uart1_Init(9600);     // RS485，工业外设
 *   - Uart2_Init(9600);     // TTL，子芯片
 *   - Uart0_DMA_Config();    // DMA 发送加速
 * ============================================================ */
void Bsp_UartInit(void)
{
    /* 初始化调用默认注释，需要时取消注释对应行 */
    // Uart0_DMA_Config();
    // Uart0_Init(19200);
    // Uart1_Init(9600);
    // Uart2_Init(9600);

#ifdef BSP_SAFETY_ENABLE
    /* Step 1 ──► 创建三路 UART 互斥量（线程安全保护） */
    CreateMutexInit();
#endif
}

/* ============================================================
 * 【运行时步骤 1/2/3】UART 轮询发送（互斥量保护）
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► xSemaphoreTake ──► 获取对应 UART 的互斥量（阻塞等待）
 *   Step 2 ──► usart_data_transmit + while(USART_FLAG_TC) ──► 轮询发送每个字节
 *   Step 3 ──► xSemaphoreGive ──► 释放互斥量
 * ─────────────────────────────────────────────────────────────
 * 参数：
 *   usart_periph  ── USART0/USART1/USART2
 *   pdata         ── 待发送数据指针
 *   dataLens      ── 数据长度（字节数）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：互斥量保护逻辑不要改动！
 *   轮询发送（每个字节等待 TC 标志）适用于小数据量场景。
 *   大数据量建议使用 uart_dma_send() DMA 发送。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】可自定义内容：
 *   - 添加 DMA 模式切换逻辑
 *   - 添加发送完成回调
 * ============================================================ */
void Uart_SendData(uint32_t usart_periph, uint8_t *pdata, uint16_t dataLens)
{
#ifdef BSP_SAFETY_ENABLE
    /* Step 1 ──► 获取对应 UART 互斥量 */
    if (usart_periph == USART0)
        xSemaphoreTake(UART0_MutexHandle, portMAX_DELAY);
    if (usart_periph == USART1)
        xSemaphoreTake(UART1_MutexHandle, portMAX_DELAY);
    if (usart_periph == USART2)
        xSemaphoreTake(UART2_MutexHandle, portMAX_DELAY);
#endif

    /* Step 2 ──► 轮询发送每个字节（等待 TC 标志确保发送完成） */
    for (int i = 0; i < dataLens; i++) {
        usart_data_transmit(usart_periph, pdata[i]);
        while (usart_flag_get(usart_periph, USART_FLAG_TC) == RESET);
    }

#ifdef BSP_SAFETY_ENABLE
    /* Step 3 ──► 释放互斥量 */
    if (usart_periph == USART0)
        xSemaphoreGive(UART0_MutexHandle);
    if (usart_periph == USART1)
        xSemaphoreGive(UART1_MutexHandle);
    if (usart_periph == USART2)
        xSemaphoreGive(UART2_MutexHandle);
#endif
}

/* ============================================================
 * 【固定区】DMA 发送（旧版本，无等待上一帧逻辑）
 * ─────────────────────────────────────────────────────────────
 * 已注释，保留旧版本参考。
 * 新版本 uart_dma_send() 在此基础上增加了等待上一帧发完的逻辑。
 * ============================================================ */
// void uart_dma_send_old(uint8_t* txbuf, uint16_t len)
// {
//     dma_channel_disable(DMA0, DMA_CH2);
//     memcpy(Uart_DMA_TX_Buf, txbuf, len);
//     dma_transfer_number_config(DMA0, DMA_CH2, len);
//     dma_channel_enable(DMA0, DMA_CH2);
//     usart_dma_transmit_config(USART0, USART_TRANSMIT_DMA_ENABLE);
// }

/* ============================================================
 * 【运行时步骤 1/2/3/4】UART DMA 发送（加速版）
 * ─────────────────────────────────────────────────────────────
 * 运行时步骤：
 *   Step 1 ──► dma_transfer_number_get ──► 检查上一帧 DMA 是否发完
 *   Step 2 ──► delay_ms(1) ──► 等待上一帧 DMA 完成（最多 4ms）
 *   Step 3 ──► memcpy ──► 拷贝数据到 DMA 缓冲区
 *   Step 4 ──► dma_transfer_number_config + dma_channel_enable ──► 启动 DMA 发送
 * ─────────────────────────────────────────────────────────────
 * 参数：
 *   txbuf ── 待发送数据指针
 *   len   ── 数据长度（字节数，自动限制 ≤ DMA_BUF_SIZE）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：DMA 发送流程不要改动！
 *   等待上一帧完成是为了防止数据覆盖。
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】可自定义内容：
 *   - DMA_BUF_SIZE 当前 2048，可根据需求调整
 *   - 超时等待次数（try_cnt > 4）
 * ============================================================ */
void uart_dma_send(uint8_t* txbuf, uint16_t len)
{
    /* Step 1 ──► 检查 DMA 通道传输计数（判断上一帧是否发完） */
    uint8_t try_cnt = 0;
    while (dma_transfer_number_get(DMA0, DMA_CH2) != 0) {
        delay_ms(1);
        if (++try_cnt > 4)
            break;  /* 最多等待 4ms，超时强制覆盖 */
    }

    /* 长度保护：超过缓冲区大小则截断 */
    if (len > DMA_BUF_SIZE) {
        len = DMA_BUF_SIZE;
    }

    /* Step 2 ──► 停止 DMA 通道，配置新数据 */
    dma_channel_disable(DMA0, DMA_CH2);

    /* Step 3 ──► 拷贝数据到 DMA 发送缓冲区 */
    memcpy(Uart_DMA_TX_Buf, txbuf, len);
    Uart_DMA_TX_Buf[DMA_BUF_SIZE] = 0;  /* 缓冲区末尾清零（调试用） */

    /* Step 4 ──► 配置 DMA 传输长度并启动 DMA */
    dma_transfer_number_config(DMA0, DMA_CH2, len);
    dma_channel_enable(DMA0, DMA_CH2);
    usart_dma_transmit_config(USART0, USART_TRANSMIT_DMA_ENABLE);
}

/* ============================================================
 * 【固定区】printf 重定向到 USART
 * ─────────────────────────────────────────────────────────────
 * printf 函数底层调用 fputc() 输出每个字符。
 * 重定义 fputc() 将字符通过 USART 发送，
 * 即可使用 printf() 向串口打印调试信息。
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 固定区：fputc 和 _sys_exit 不要改动！
 *   _sys_exit 是 ARM 标准库要求的退出函数，
 *   实现为空即可，否则链接时会报错。
 * ============================================================ */

/* 定义调试输出使用的 USART（当前使用 USART0） */
#define DEBUG_COM    DEBUG_UART
#define DEBUG_UART   USART0

__ASM (".global __use_no_semihosting");   /* 禁止半主机模式 */

/**
 * fputc — printf 输出重定向
 * ─────────────────────────────────────────────────────────────
 * 参数：
 *   ch ── 要输出的字符
 *   f  ── 文件指针（未使用，保持兼容）
 * 返回值：输出的字符
 * ─────────────────────────────────────────────────────────────
 * 原理：printf -> fputc -> usart_data_transmit -> USART TX 寄存器
 * ─────────────────────────────────────────────────────────────
 * 【自定义区】切换调试串口：
 *   - 将 DEBUG_UART 改为 USART1 或 USART2
 *   - 对应修改 Uart1_Init/Uart2_Init 的波特率
 * ============================================================ */
FILE __stdout;

int fputc(int ch, FILE *f)
{
    (void)f;  /* 未使用参数，消除警告 */
    usart_data_transmit(DEBUG_COM, (uint8_t)ch);
    while (RESET == usart_flag_get(DEBUG_COM, USART_FLAG_TBE));
    return ch;
}

/**
 * _sys_exit — ARM 标准库退出函数
 * ─────────────────────────────────────────────────────────────
 * 参数：x（未使用）
 * 返回值：void
 * ─────────────────────────────────────────────────────────────
 * 作用：标准 C 库要求在不使用半主机模式时提供此函数。
 *       实现为空即可，防止链接时报错。
 * ============================================================ */
void _sys_exit(int x)
{
    (void)x;  /* 未使用参数，消除警告 */
}
