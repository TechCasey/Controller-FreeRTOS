# 车载控制器 — FreeRTOS 工程样例库

> GD32A50x (ARM Cortex-M33) 车载控制器固件，FreeRTOS V10.2.1。保留完整 FreeRTOS 内核源码和四大 RTOS 编程模式，按功能拆分为多个独立学习模块，适合已有嵌入式基础想快速落地 FreeRTOS 工程的开发者。

---

## 📦 模块总览

| 模块 | 目录 | 内容 | 难度 |
|------|------|------|------|
| **Module 0** 基础 | `modules/base/` | 任务/队列/定时器/信号量/钩子函数 | ⭐ 入门 |
| **Module 1** CAN-OTA | `modules/can-ota/` | CAN 总线 OTA 升级、环形缓冲区、CRC16、广播模式 | ⭐⭐⭐ 进阶 |
| **Module 2** UART-DMA | `modules/uart-dma/` | 三路 UART + DMA + 互斥量 + printf 重定向 | ⭐⭐ 中级 |
| **Module 3** Watchdog | `modules/watchdog/` | 片内 FWDGT + 外置 SGM706 双重看门狗 | ⭐⭐ 中级 |

---

## 🗂️ 目录结构

```
freertos-project-model/          ← 根目录 = Keil MDK 项目根
├── README.md                    ← 本文件（总览）
├── modules/                    ← 模块文档（学习指南）
│   ├── base/                   ← Module 0：FreeRTOS 基础
│   │   └── README.md
│   ├── can-ota/                ← Module 1：CAN 总线 OTA 升级
│   │   └── README.md
│   ├── uart-dma/              ← Module 2：UART + DMA 通信
│   │   └── README.md
│   └── watchdog/               ← Module 3：双重看门狗
│       └── README.md
├── Application/               ← 应用层源码
│   ├── app/                    ← 应用任务（含 4 个模块的源码）
│   │   ├── CanComm.c/.h        ← FreeRTOS 综合示例（CAN 通信）
│   │   ├── simple_example.c/.h ← 最简 FreeRTOS 示例
│   │   └── upgrade.c/.h        ← OTA 升级模块（Module 1）
│   └── hardware/               ← 板级驱动
│       ├── bsp_can.c/.h        ← CAN 总线驱动（含互斥量）
│       ├── bsp_uart.c/.h       ← UART+DMA 驱动（Module 2）
│       └── bsp_fwdtg.c/.h     ← 看门狗驱动（Module 3）
├── USER/                       ← CMSIS 层 + 系统配置
│   ├── Inc/
│   │   ├── FreeRTOSConfig.h   ← FreeRTOS 内核配置
│   │   ├── config.h / type.h / include.h
│   │   └── main.h / systick.h / osbasetimer.h
│   └── Src/
│       ├── main.c              ← 系统入口（15 步初始化）
│       ├── FreeRTOSPerfect.c   ← 5 种钩子函数
│       ├── gd32a50x_it.c       ← 中断服务程序（含 ISR→队列）
│       ├── osbasetimer.c       ← 运行时统计
│       ├── systick.c           ← 系统延时（RTOS 感知）
│       └── debug.c/.h          ← 日志封装
├── Drivers/                    ← 芯片驱动
│   ├── CMSIS/                  ← ARM Cortex-M33 启动文件
│   └── GD32A50x_standard_peripheral/  ← GD32 标准外设库
├── Middlewares/
│   ├── FreeRTOS/              ← FreeRTOS V10.2.1 完整内核
│   │   ├── Source/
│   │   │   ├── crt.c / event_groups.c / list.c / queue.c
│   │   │   ├── stream_buffer.c / tasks.c / timers.c
│   │   │   ├── include/       ← 所有公共头文件
│   │   │   └── portable/       ← 处理器相关移植层
│   │   └── README.md
│   └── SEGGER_RTT/             ← 调试日志输出
└── MDK-ARM/
    └── FreeRTOS_Project_Model.uvprojx  ← Keil MDK 项目文件
```

---

## 🚀 快速开始

### 1. 克隆仓库
```bash
git clone git@github.com:lqz8802/Controller-FreeRTOS.git
cd Controller-FreeRTOS
```

### 2. 用 Keil 打开项目
```
MDK-ARM/FreeRTOS_Project_Model.uvprojx
```

### 3. 编译并下载
- Target: GD32A50x
- 编译无报错后下载到开发板

---

## 📚 推荐学习路径

```
第1步 → README.md（本文档）→ 理解整体结构
  ↓
第2步 → modules/base/README.md
         └─ simple_example.c → 最小可运行 FreeRTOS 程序
  ↓
第3步 → main.c → 系统启动流程（15 步初始化注释）
  ↓
第4步 → FreeRTOSConfig.h → 内核配置参数
  ↓
第5步 → gd32a50x_it.c → 中断中如何使用 FreeRTOS API
  ↓
第6步 → 按需深入各模块：
         • CAN-OTA: modules/can-ota/README.md → upgrade.c
         • UART-DMA: modules/uart-dma/README.md → bsp_uart.c
         • Watchdog: modules/watchdog/README.md → bsp_fwdtg.c
```

---

## 🔑 FreeRTOS API 速查

### 任务
```c
xTaskCreate(taskFunc, name, stackWords, param, priority, handleOut);
vTaskDelay(ticks);                       // 延时（RTOS 运行中）
vTaskDelete(handle);                     // 删除任务
vTaskSuspend(handle);                    // 挂起任务
vTaskResume(handle);                     // 恢复任务
uxTaskPriorityGet(handle);               // 获取优先级
vTaskPrioritySet(handle, priority);      // 设置优先级
```

### 队列
```c
QueueHandle_t xQueueCreate(uxQueueLength, uxItemSize);
BaseType_t xQueueSend(xQueue, &data, xTicksToWait);
BaseType_t xQueueReceive(xQueue, &data, xTicksToWait);
BaseType_t xQueueSendFromISR(xQueue, &data, &pxHigherPriorityTaskWoken);
```

### 软件定时器
```c
TimerHandle_t xTimerCreate(const char *, TickType_t, UBaseType_t, void *, TimerCallbackFunction_t);
BaseType_t xTimerStart(TimerHandle_t, xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t, xTicksToWait);
BaseType_t xTimerReset(TimerHandle_t, xTicksToWait);
```

### 信号量
```c
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t, &pxHigherPriorityTaskWoken);
```

### 其他
```c
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskGetNumberOfTasks(void);
char *pcTaskGetTaskName(TaskHandle_t);
BaseType_t xTaskGetSchedulerState(void);
void vTaskList(char *pcWriteBuffer);        // 任务列表（调试）
void vTaskGetRunTimeStats(char *pcWriteBuffer);  // 运行时统计
```

---

## ⚙️ FreeRTOSConfig.h 关键配置

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `configUSE_PREEMPTION` | 1 | 抢占式调度（推荐） |
| `configUSE_TIME_SLICING` | 1 | 时间片轮转 |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 1 | 使用硬件 CLZ 指令加速优先级查找 |
| `configUSE_TICKLESS_IDLE` | 0 | 禁用电缆模式（低功耗） |
| `configUSE_IDLE_HOOK` | 1 | 启用空闲钩子（常用于喂狗） |
| `configUSE_TICK_HOOK` | 1 | 启用 Tick 钩子 |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | 内存分配失败钩子 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 栈溢出检测（方法 2，更安全） |
| `configTOTAL_HEAP_SIZE` | 32*1024 | 堆内存大小（32KB） |
| `configTICK_RATE_HZ` | 1000 | 系统 Tick 频率（1ms） |
| `configMAX_PRIORITIES` | 32 | 最大优先级数 |
| `configMINIMAL_STACK_SIZE` | 128 | 最小任务栈大小（字） |

---

## 📐 代码注释标注说明

所有核心源码均包含以下四类标注：

| 标注 | 含义 | 建议 |
|------|------|------|
| `【固定区】` | 框架/协议核心代码 | 不要改动 |
| `【自定义区】` | 业务逻辑可在此添加修改 | 根据需求修改 |
| `【搭建步骤 1/2/3...】` | 初始化/创建的顺序 | 按顺序理解 |
| `【运行时步骤 1/2/3...】` | 程序运行时的执行顺序 | 按顺序理解 |

---

## 🧩 各模块核心模式总结

| 模块 | FreeRTOS 模式 | 关键文件 |
|------|-------------|---------|
| Base | 任务+队列+定时器+信号量+钩子 | simple_example.c, main.c |
| CAN-OTA | 队列+软件定时器+任务挂起/恢复 | upgrade.c |
| UART-DMA | 互斥量+临界区 | bsp_uart.c |
| Watchdog | 空闲钩子喂狗 | CanComm.c (WatchdogTask) |

---

## ⚠️ 免责声明

本仓库仅供学习与参考。如需商用，请自行评估法律风险。
