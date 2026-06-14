# Module 2 — UART + DMA 通信（UART-DMA）

> 本模块展示车载控制器的三路 UART 通信（RS232/RS485/TTL）驱动实现，含 DMA 发送加速、互斥量线程保护、FreeRTOS 延时自动区分、printf 重定向。

## 📁 源码位置

| 文件 | 说明 |
|------|------|
| `Application/hardware/bsp_uart.c` | UART 驱动（带完整中文注释） |
| `Application/hardware/bsp_uart.h` | UART 头文件 |
| `USER/Src/systick.c` | SysTick 配置与延时函数 |
| `USER/Inc/systick.h` | SysTick 头文件 |

## 🗺️ 三路 UART 硬件配置

| 通道 | 接口 | 用途 | 特点 |
|------|------|------|------|
| UART0 | RS232（PB13/PB14） | 调试串口 | printf 重定向 |
| UART1 | RS485（PB15/PD8） | 半双工通信 | 互斥量保护 |
| UART2 | TTL（PA5/PA6） | 连接外部芯片 | 互斥量保护 |

## 🏗 搭建步骤（初始化顺序）

### UART 初始化
1. `rcu_periph_clock_enable(RCU_USARTx)` → 使能 USART 时钟
2. `gpio_af_set()` → 配置 GPIO 复用功能
3. `gpio_mode_set()` + `gpio_output_options_set()` → GPIO 模式配置
4. `usart_deinit()` → 复位 USART
5. `usart_word_length_set()` / `usart_stop_bit_set()` / `usart_parity_config()` → 帧格式
6. `usart_baudrate_set()` → 波特率
7. `usart_receive/transmit_config()` → 收发使能
8. `usart_enable()` → 使能 USART
9. `usart_interrupt_enable(USART_INT_RBNE)` → 开启接收中断

### DMA 配置（UART0 发送）
1. `rcu_periph_clock_enable(RCU_DMA0)` → 使能 DMA 时钟
2. `dma_deinit()` → 复位 DMA 通道
3. 配置 `dma_data_param`：内存地址、方向（Memory→Peripheral）、宽度、通道数
4. `dma_circulation_disable()` → 关闭循环模式
5. `dma_channel_enable()` → 使能 DMA 通道
6. `usart_dma_transmit_config(USART_TRANSMIT_DMA_ENABLE)` → USART 关联 DMA

### 互斥量创建
1. `xSemaphoreCreateMutex()` → 为每路 UART 创建互斥量
2. 后续 `Uart_SendData()` 中 `xSemaphoreTake/Give` 配对使用

## ⚙️ 核心 API

```c
// 初始化
void Bsp_UartInit(void);              // 初始化入口（UART + 互斥量）
void Uart0_Init(uint32_t baudval);   // 指定波特率初始化 UART0
void Uart1_Init(uint32_t baudval);   // 指定波特率初始化 UART1

// 发送（线程安全）
void Uart_SendData(uint32_t usart_periph, uint8_t *pdata, uint16_t lens);
//   └─ 自动互斥量保护，portMAX_DELAY 等待锁

// DMA 发送（适合大数据量）
void uart_dma_send(uint8_t *txbuf, uint16_t len);
//   └─ 自动等待上一帧 DMA 发完（dma_transfer_number_get）

// printf 重定向（自动路由到 UART0）
int fputc(int ch, FILE *f);           // 重定向到 DEBUG_USART
void _sys_exit(int x);               // 标准库依赖
```

## 🔐 互斥量保护流程

```
任务A: Uart_SendData(USART1, data, len)
  ├─ xSemaphoreTake(UART1_MutexHandle, portMAX_DELAY)  ← 等锁
  ├─ usart_data_transmit() × len                        ← 临界区
  └─ xSemaphoreGive(UART1_MutexHandle)                  ← 释放锁

任务B: Uart_SendData(USART1, data, len)
  └─ xSemaphoreTake(UART1_MutexHandle, portMAX_DELAY)  ← 等锁（任务A释放后继续）
```

## ⏱ SysTick 延时框架

| 函数 | 说明 | RTOS 行为 |
|------|------|----------|
| `delay_xus(us)` | 微秒延时 | 始终用 SysTick 计数器（裸机精确延时） |
| `delay_ms(ms)` | 毫秒延时 | ≥1ms 时用 `vTaskDelay`，<1ms 用 `delay_xus` |
| `delay_xms(ms)` | 固定毫秒延时 | 始终用 `delay_xus` × 1000（精确） |
| `delay_s/sx(s)` | 秒延时 | 循环调用 `delay_ms/xs` |

**`delay_ms` 自动判断逻辑：**
```c
if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    // FreeRTOS 已运行 → 用 vTaskDelay（释放 CPU）
    vTaskDelay(nms / usFac_ms);
} else {
    // 裸机模式 → 用 SysTick 精确延时
    delay_xus(nms * 1000);
}
```

## 🔧 printf 重定向配置

```c
// bsp_uart.c 中已实现，无需修改
int fputc(int ch, FILE *f) {
    usart_data_transmit(DEBUG_COM, (uint8_t)ch);
    while (!usart_flag_get(DEBUG_COM, USART_FLAG_TBE));
    return ch;
}

// 使用示例
printf("System clock: %d Hz\r\n", SystemCoreClock);
printf("Hello UART0 at 19200 baud\r\n");
```

## ⚠️ 工程注意事项

1. **互斥量必须配对**：`Take` 和 `Give` 必须成对，避免死锁
2. **DMA 发送前等上一帧完成**：`dma_transfer_number_get() != 0` 时等待
3. **临界区长度最小化**：仅在 `usart_data_transmit` 循环内持锁
4. **波特率配置**：传入 `baudval` 参数，由外层调用者指定
5. **RELEASE 模式注意**：fputc 依赖 USART0 外设，调试时确保 UART0 已初始化
