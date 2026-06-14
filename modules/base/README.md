# Module 0 — FreeRTOS 基础（Base）

> FreeRTOS 核心编程模式基础，适合作为第一个学习的模块。

## 📁 源码位置

| 文件 | 说明 |
|------|------|
| `Application/app/CanComm.c/.h` | CAN 双总线通信（任务 + 队列 + 定时器 + 信号量） |
| `Application/app/simple_example.c/.h` | 最简 FreeRTOS 示例（任务 + 队列 + 定时器 + 信号量） |
| `USER/Src/main.c` | 系统启动入口 |
| `USER/Src/FreeRTOSPerfect.c` | 5 种钩子回调函数 |
| `USER/Src/gd32a50x_it.c` | 中断服务程序（含 ISR→队列示例） |
| `USER/Src/osbasetimer.c` | 运行时统计定时器 |
| `USER/Src/systick.c` | 系统延时（RTOS 感知） |
| `USER/Inc/FreeRTOSConfig.h` | FreeRTOS 内核配置 |
| `USER/Inc/config.h` / `type.h` / `include.h` | 类型定义和头文件包含 |

## 🗺️ 学习路径（建议顺序）

1. `README.md`（根目录）→ 项目整体结构
2. `simple_example.c` → 最小可运行 FreeRTOS 程序（4 大模式全覆盖）
3. `main.c` → 系统启动流程（15 步初始化注释）
4. `FreeRTOSConfig.h` → 内核配置参数详解
5. `gd32a50x_it.c` → 中断中如何使用 FreeRTOS API（队列）
6. `CanComm.c` → 综合实战：CAN 双总线 + 互斥量 + 看门狗喂狗
7. `FreeRTOSPerfect.c` → 5 种钩子函数（内存/任务/空闲/栈溢出/心跳）

## 🔑 核心 API

```c
// 任务
xTaskCreate(TaskFunction_t, name, stack, param, priority, handle);
vTaskDelete(handle);

// 队列
xQueueCreate(len, itemSize);
xQueueSend(queue, &data, timeout);
xQueueReceive(queue, &data, timeout);

// 定时器
xTimerCreate(name, period, autoReload, id, callback);
xTimerStart(timer, timeout);

// 信号量
xSemaphoreCreateMutex();
xSemaphoreTake(sem, timeout);
xSemaphoreGive(sem);

// 中断安全
xQueueSendFromISR(queue, &data, &higherPriWoken);
xSemaphoreGiveFromISR(sem, &higherPriWoken);
```

## 📖 四大模式快速定位

| 模式 | 文件 | 关键函数 |
|------|------|---------|
| 任务创建与调度 | simple_example.c | `vTask1/2` |
| 队列（任务间通信） | simple_example.c | `xQueueCreate/Send/Receive` |
| 软件定时器 | simple_example.c | `xTimerCreate/Start` |
| 互斥量（资源保护） | bsp_can.c / CanComm.c | `xSemaphoreCreateMutex` |
| 中断→队列 | gd32a50x_it.c | `xQueueSendFromISR` |
| 看门狗喂狗 | CanComm.c | `WatchdogTask` |

## ⚙️ 默认区 / 自定义区标记说明

- `【固定区】` — 协议/框架核心，不要改动
- `【自定义区】` — 业务逻辑可在此添加修改
- `【搭建步骤 1/2/3...】` — 初始化顺序
- `【运行时步骤 1/2/3...】` — 运行时执行顺序
