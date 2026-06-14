# FreeRTOS 工程样例库 — GD32A50x (ARM Cortex-M33)
> 本仓库从真实车载控制器固件中提取，保留 FreeRTOS V10.2.1 完整框架和四大 RTOS 编程模式，供工程参考和二次开发。
> ⚠️ 适合已有嵌入式基础，想快速落地 FreeRTOS 工程的开发者。

---

## 目录

- [一、创建步骤 — 如何在 FreeRTOS 中搭建你的系统](#一创建步骤--如何在-freertos-中搭建你的系统)
- [二、运行步骤 — 程序上电后按什么顺序执行](#二运行步骤--程序上电后按什么顺序执行)
- [三、默认代码 — 哪些代码不要动](#三默认代码--哪些代码不要动)
- [四、代码结构](#四代码结构)
- [五、四大 RTOS 编程模式速查](#五四大-rtos-编程模式速查)
- [六、API 速查表](#六api-速查表)

---

## 一、创建步骤 — 如何在 FreeRTOS 中搭建你的系统

> 搭建一个完整的 FreeRTOS 系统，按以下顺序执行。

### Step 1 — 确认硬件和系统时钟

**做什么：** 配置 MCU 时钟和 FreeRTOS 心跳基准。

**在哪里：** `FreeRTOSConfig.h` + `system_gd32a50x.c`

**固定内容（不要改）：**

```c
// FreeRTOSConfig.h — 这些配置项决定整个 OS 的行为基准
#define configCPU_CLOCK_HZ        (SystemCoreClock)   // 必须与实际芯片时钟匹配（100MHz）
#define configTICK_RATE_HZ        1000                 // SysTick 每秒中断次数 → 1 tick = 1ms
#define configTOTAL_HEAP_SIZE     (25 * 1024)          // 动态内存池大小（任务/队列/定时器从中分配）
```

**【核心自定义区】调整项：**

```c
#define configTOTAL_HEAP_SIZE     (30 * 1024)  // 内存不足时调大（不够→ xTaskCreate 返回 errCOULD_NOT_ALLOCATE_REQUIRED_MEM）
#define configTICK_RATE_HZ       1000          // 一般不改；改为 100 可节省 CPU 开销但定时精度下降
```

---

### Step 2 — 配置中断优先级（框架固定区）

**做什么：** 将 PendSV 和 SysTick 配置为最低优先级，保证 FreeRTOS 内核不受干扰。

**在哪里：** `main.c` 的最开头。

**固定代码（不要改）：**

```c
// Step 2 — 固定区：配置优先级分组
// ─────────────────────────────────────────────────────────────
// ⚠️ 框架固定区：PendSV 和 SysTick 必须设为最低优先级（数字越大优先级越低）
// 任何改动都会导致 FreeRTOS 无法正常运行。
// ⚠️ configMAX_SYSCALL_INTERRUPT_PRIORITY 以上的优先级不能调用 FromISR API！
// ─────────────────────────────────────────────────────────────
NVIC_SetPriorityGrouping(NVIC_PRIGROUP_PREEMPT_PRIORITY);
// NVIC_SetPriority(PendSV_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY);  // 已由 FreeRTOS 自动配置
// NVIC_SetPriority(SysTick_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY); // 已由 FreeRTOS 自动配置
```

---

### Step 3 — 初始化硬件（BSP 层）

**做什么：** 初始化 GPIO、CAN、UART、Timer、ADC、PWM、SPI、I2C 等外设。

**在哪里：** `main.c` Step 3。

**固定内容（不要改）：**

```c
// Step 3 — 固定区：BSP 硬件初始化（顺序固定）
Bsp_Gpio_Init();
SEGGER_RTT_Init();
SysTick_Init();        // ⚠️ SysTick 必须在 __enable_irq 之前初始化（OS 心跳基准）
// 其他 BSP...
```

**【核心自定义区】调整项：**

- 需要哪个外设就初始化哪个，不需要的不调用
- 可以在 `Application/hardware/` 下添加自定义 BSP 模块

---

### Step 4 — 加载系统参数（可选）

**做什么：** 从 Flash/W25Qxx/EEPROM 加载系统配置参数。

**在哪里：** `main.c` Step 4。

**固定内容：** 如果没有参数需要加载，跳过此步。

---

### Step 5 — 创建 FreeRTOS 资源（核心区）

**做什么：** 按固定顺序创建所有 RTOS 对象（任务 → 队列 → 定时器 → 信号量 → 互斥量）。

**在哪里：** `main.c` Step 5。

**固定顺序（不要打乱）：**

```
Step 5.1 ──► 创建 FreeRTOS 定时器守护任务时间统计定时器（Timer7，已在 osbasetimer.c 中固定）
Step 5.2 ──► 创建消息队列（xQueueCreate）
Step 5.3 ──► 创建软件定时器（xTimerCreate）
Step 5.4 ──► 创建二值信号量（xSemaphoreCreateBinary）
Step 5.5 ──► 创建互斥量（xSemaphoreCreateMutex）
Step 5.6 ──► 创建任务（xTaskCreate）
Step 5.7 ──► 启动软件定时器（xTimerStart）
```

**固定内容（不要改）：**

```c
// Step 5 — 固定顺序区
// ─────────────────────────────────────────────────────────────
// ⚠️ 框架固定区：所有 RTOS 对象必须在 vTaskStartScheduler() 之前创建！
// 创建顺序建议：队列 → 信号量 → 定时器 → 任务 → 启动定时器
// ⚠️ 在调度器启动后（vTaskStartScheduler 之后）不能再创建任务。
// ─────────────────────────────────────────────────────────────

// Step 5.2 — 创建消息队列
QueueHandle_t myQueue = xQueueCreate(10, sizeof(MyMsg_t));

// Step 5.3 — 创建软件定时器（周期/一次性由你自己决定）
TimerHandle_t myTimer = xTimerCreate(
    "MyTimer",           // 名字（调试用）
    1000,                // 周期 tick（configTICK_RATE_HZ=1000 → 1000 tick = 1秒）
    pdTRUE,              // pdTRUE=自动重载，pdFALSE=一次性
    NULL,                // 定时器 ID（回调中区分多个定时器）
    MyTimer_Callback      // 回调函数
);

// Step 5.4 — 创建二值信号量（ISR→任务 或 定时器→任务 同步）
SemaphoreHandle_t mySem = xSemaphoreCreateBinary();

// Step 5.5 — 创建互斥量（保护共享硬件资源）
SemaphoreHandle_t myMutex = xSemaphoreCreateMutex();

// Step 5.6 — 创建任务
xTaskCreate(
    MyTask,              // 任务函数
    "MyTask",            // 任务名（调试用）
    256,                 // 栈深度（word），configMINIMAL_STACK_SIZE=128 → 最小 128 word = 512 字节
    NULL,                // 参数
    osPriorityNormal,    // 优先级
    NULL                 // 句柄
);

// Step 5.7 — 启动软件定时器
xTimerStart(myTimer, portMAX_DELAY);
```

**【核心自定义区】调整项：**

- 队列长度：根据峰值数据量调整（频繁满队列 → 增大长度或优化处理速度）
- 定时器周期：根据业务周期调整（注意 tick 精度）
- 任务优先级：高优先级任务会抢占低优先级任务（不要设成一样）
- 任务栈大小：局部变量多、嵌套调用深 → 增大栈

---

### Step 6 — 启动调度器

**做什么：** 使能中断并启动 FreeRTOS 调度器，OS 正式接管。

**在哪里：** `main.c` Step 6。

**固定代码（不要改）：**

```c
// Step 6 — 固定区：启动调度器
// ─────────────────────────────────────────────────────────────
// ⚠️ 框架固定区：此步之后 CPU 由 FreeRTOS 调度器接管，main() 不会再执行
// ⚠️ __enable_irq() 必须在 vTaskStartScheduler() 之前！
// ─────────────────────────────────────────────────────────────
__enable_irq();
vTaskStartScheduler();

// 如果程序到达这里，说明内存不足（configTOTAL_HEAP_SIZE 太小）
while(1);  // 调度器启动失败
```

---

## 二、运行步骤 — 程序上电后按什么顺序执行

> 描述从芯片上电到系统稳定运行的完整执行路径。

### 上电 → Reset Handler → main()

```
芯片上电
    │
    ▼
Reset_Handler（汇编启动代码，Drivers/CMSIS/startup_gd32a50x.s）
    │  设置栈指针、PC 寄存器，跳转到 SystemInit()
    ▼
SystemInit()（system_gd32a50x.c）— 配置 PLL 时钟
    │  设置 SystemCoreClock = 100MHz
    ▼
main() — 用户代码入口
```

### main() 内部流程

```
Step 1 ──► NVIC 优先级分组配置（固定）
Step 2 ──► GPIO 初始化 + SEGGER RTT 初始化（固定）
Step 3 ──► SysTick_Init() — 配置 1ms 心跳（固定）
Step 4 ──► 各外设 BSP_Init() — CAN/UART/Timer 等（根据需要调用）
Step 5 ──► Flash 参数加载（可选，没有就跳过）
Step 6 ──► OsBaseTimer_Init() — Timer7 运行时间统计（固定）
Step 7 ──► 各模块 TaskInit() — 创建队列/信号量/定时器/任务（核心自定义区）
Step 8 ──► __enable_irq() + vTaskStartScheduler() — OS 接管
    │
    ▼
【调度器接管后，任务开始并发执行】
```

### 调度器启动后 — 任务并发运行

```
vTaskStartScheduler()
    │
    ├── 创建 Idle 任务（优先级 0，最低）— 内核自动创建
    ├── 创建 Timer Daemon 任务（优先级 configTIMER_TASK_PRIORITY）— configUSE_TIMERS=1 时自动创建
    │
    ▼
Idle Task  ←──────────────────────────────┐
    │                                     │
Timer Daemon ──► xTimerStart() ──────────┤
    │                                     │  定时器触发
CanRecvTask ──► xQueueReceive()  ←────────┤  CAN0 ISR 放入队列
    │                                     │
CanSendTask ←─ xSemaphoreTake() ←─────────┤  定时器回调 Give
    │                                     │
SimpleTask  ←─ xSemaphoreTake() ←─────────┘  定时器回调 Give
```

### 各任务/定时器的运行流程

**Idle 任务（每 tick 调度空闲时自动运行）：**
```
    │
    ▼
vApplicationIdleHook() — 每当 CPU 空闲时调用
    │  → 喂看门狗 / 切换心跳 LED
    ▼
    （继续运行或让出 CPU）
```

**Timer Daemon（Timer7 50µs 统计定时器）：**
```
    │
    ▼
OsBaseTimer_Callback() — 每 50µs 执行一次
    │  → FreeRTOSRunTimeTicks++（供 vTaskGetRunTimeStats 使用）
    ▼
    （自动重载）
```

**CAN0 中断（硬件事件触发）：**
```
    │
    ▼
CAN0_Message_IRQHandler()
    │  → 读取 CAN0 FIFO → xQueueSendToBackFromISR(s_CanComm.RecvQueue)
    │  → portYIELD_FROM_ISR() 触发 PendSV 切换到接收任务
    ▼
CanRecvTask（接收任务）
    │  → xQueueReceive() 解除阻塞 → 解析 CAN 帧
    ▼
    （继续等待下一帧）
```

**发送定时器（CanSend_Timer，每 500ms 触发）：**
```
    │
    ▼
CanSend_Timer() 回调
    │  → xSemaphoreGive(s_CanComm.sembHandle)
    ▼
CanSendTask（发送任务）
    │  → xSemaphoreTake() 解除阻塞
    │  → 循环发送多帧 CAN 数据
    │  → vTaskDelay() 让出 CPU
    ▼
    （等待下一个 500ms 周期）
```

**1s 超时定时器（通信看门狗）：**
```
    │
    ▼
Can_Timer1s() 回调 — 每 1 秒执行
    │  → 累加超时计数器
    │  → 超时 → 报警 + CAN0 软件复位
    ▼
    （自动重载）
```

---

## 三、默认代码 — 哪些代码不要动

> 以下代码是 FreeRTOS 框架的核心，运行逻辑完全由内核控制，不要随意修改。

### 框架固定区（不要改）

| 文件 | 不要改的部分 | 原因 |
|---|---|---|
| `FreeRTOSConfig.h` | `configCPU_CLOCK_HZ`、`configTICK_RATE_HZ`、`configMAX_SYSCALL_INTERRUPT_PRIORITY` | 决定 OS 心跳基准和中断安全边界 |
| `FreeRTOSConfig.h` | `configKERNEL_INTERRUPT_PRIORITY`、`configMAX_SYSCALL_INTERRUPT_PRIORITY` | 优先级设置不当会导致 FromISR API 失效 |
| `FreeRTOSConfig.h` | `configUSE_PREEMPTION=1`、`configUSE_TIME_SLICING=1` | 决定调度方式，改动影响任务切换行为 |
| `port.c` | `vPortYield` / `xPortPendSVHandler` / SysTick Handler | 内核移植层，任何改动直接导致系统崩溃 |
| `tasks.c` / `queue.c` / `timers.c` / `list.c` | FreeRTOS 内核源码 | 内核逻辑，改动 = 重写 OS |
| `main.c` Step 2 | NVIC 优先级分组配置 | 框架固定区 |
| `main.c` Step 6 | `__enable_irq()` + `vTaskStartScheduler()` | OS 启动序列，缺少任一步骤系统无法运行 |
| `gd32a50x_it.c` 中 `PendSV_Handler` | `vPortYield` 调用 | 内核调度触发，框架固定 |
| `gd32a50x_it.c` 中 `SysTick_Handler` | `xTaskIncrementTick` 调用 | OS tick 更新，框架固定 |
| `osbasetimer.c` | Timer7 初始化和 `OsBaseTimer_Callback` | 运行时间统计基准，改动导致 CPU 使用率统计失效 |
| `systick.c` | `SysTick_Init()` 中 `LOAD` 和 `CTRL` 配置 | OS 心跳源，框架固定 |

### 框架固定区 — 中断安全规则（牢记）

```
⚠️ 框架固定区：中断优先级规则
────────────────────────────────────────────────────────────
- 优先级数值越大 → 实际优先级越低
- configMAX_SYSCALL_INTERRUPT_PRIORITY = 5（阈值）
- 优先级 0~4（高优先级）：不能调用任何 FromISR API
- 优先级 5~15（低优先级）：可以调用 FromISR API
- CAN0/CAN1 IRQn 优先级设为 6 → 可安全调用 FromISR API
────────────────────────────────────────────────────────────
```

### 可以放心自定义的区域

| 位置 | 允许修改的内容 |
|---|---|
| `main.c` Step 3 | 添加/删除 BSP_Init() 调用 |
| `main.c` Step 5 | 在各 TaskInit() 中创建新的任务/队列/定时器 |
| `simple_example.c` | 改写任务函数体、调整定时器周期和任务优先级 |
| `CanComm.c` Step 15 | 在各 case 分支中添加自定义 CAN 帧解析逻辑 |
| `CanComm.c` Step 20 | 在 SendMsg() 调用中填充自定义发送数据 |
| `bsp_*.c` | 重写外设初始化代码（保持 API 接口不变） |
| `FreeRTOSConfig.h` | 调整 heap 大小、tick rate、优先级数量、栈检测级别 |

---

## 四、代码结构

```
freertos-project-model/
├── README.md                    ← 本文件
│
├── USER/
│   ├── Inc/
│   │   ├── FreeRTOSConfig.h   ← 内核配置（全 23 项逐条注释）
│   │   ├── type.h             ← RTOS 句柄类型封装
│   │   ├── config.h           ← 系统级宏（调试模式/工作模式等）
│   │   ├── include.h          ← 全局头文件汇总
│   │   ├── main.h / osbasetimer.h / systick.h
│   │   └── FreeRTOS/ → FreeRTOS.h（内核 API 汇总）
│   └── Src/
│       ├── main.c             ← 入口（15 步骤注释）
│       ├── FreeRTOSPerfect.c  ← 5 种钩子函数
│       ├── gd32a50x_it.c      ← 中断服务程序（ISR→队列模式）
│       ├── osbasetimer.c      ← Timer7 50µs 统计定时器
│       └── systick.c          ← SysTick 延时实现
│
├── Application/
│   ├── app/
│   │   ├── simple_example.c   ← 最简 RTOS 示例（14 步骤）
│   │   ├── simple_example.h
│   │   ├── CanComm.c          ← CAN0 通信（四大 RTOS 模式）
│   │   ├── CanComm.h
│   │   └── debug.c / debug.h
│   └── hardware/
│       ├── bsp_can.c/.h      ← CAN 驱动（互斥量保护）
│       └── [12个 BSP stub]    ← 占位初始化
│
├── Middlewares/
│   ├── FreeRTOS/Source/       ← FreeRTOS V10.2.1 内核（70 个文件）
│   └── SEGGER_RTT/            ← J-Link 调试输出（4 个文件）
│
├── Drivers/
│   ├── CMSIS/                 ← 启动文件 + system_gd32a50x.c
│   └── GD32A50x_standard_peripheral/ ← 标准外设库（48 个文件）
│
└── MDK-ARM/
    └── FreeRTOS.uvprojx       ← Keil MDK 项目文件
```

---

## 五、四大 RTOS 编程模式速查

> 本仓库中每个模式都有完整实现示例，搜索对应关键字即可找到。

### 模式 A：ISR → 任务（队列）

**场景：** 硬件中断接收数据，传递给任务处理（不阻塞中断）。

**示例：** `gd32a50x_it.c` → `CAN0_Message_IRQHandler`

```c
// Step A.1 ──► 进入临界段（防止中断嵌套）
ulReturn = taskENTER_CRITICAL_FROM_ISR();

// Step A.2 ──► 读取硬件数据
can_rx_fifo_read(CAN0, &fifo);

// Step A.3 ──► 放入消息队列（FromISR 版本）
xQueueSendToBackFromISR(s_CanComm.RecvQueue, &msg, &xHigherPriorityTaskWoken);

// Step A.4 ──► 触发 PendSV 切换（如果有更高优先级任务就绪）
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// Step A.5 ──► 退出临界段
taskEXIT_CRITICAL_FROM_ISR(ulReturn);
```

任务侧：

```c
// Step A.6 ──► 任务阻塞等待数据（不会浪费 CPU）
xQueueReceive(queue, &msg, portMAX_DELAY);
// 收到数据后处理...
```

---

### 模式 B：定时器 → 任务（信号量）

**场景：** 周期性任务（定时器每 N ms 触发一次，唤醒任务执行）。

**示例：** `CanComm.c` → `CanSend_Timer` + `CanSendTask_Process`

```c
// Step B.1 ──► 创建定时器（500ms 周期，自动重载）
TimerHandle_t timer = xTimerCreate("MyTimer", 500, pdTRUE, NULL, MyTimer_Callback);

// Step B.2 ──► 创建二值信号量
SemaphoreHandle_t sem = xSemaphoreCreateBinary();

// Step B.3 ──► 定时器回调（Timer Daemon 中执行）
void MyTimer_Callback(TimerHandle_t xTimer) {
    xSemaphoreGive(sem);  // 释放信号量，唤醒任务
}

// Step B.4 ──► 任务侧等待信号量
void MyTask(void *param) {
    while(1) {
        xSemaphoreTake(sem, portMAX_DELAY);  // 阻塞直到定时器触发
        // 处理业务...
    }
}

// Step B.5 ──► 启动定时器（在任务或 main 中均可）
xTimerStart(timer, portMAX_DELAY);
```

---

### 模式 C：定时器（独立看门狗）

**场景：** 定时器独立执行监控任务（不需要唤醒其他任务）。

**示例：** `CanComm.c` → `Can_Timer1s`（通信超时看门狗）

```c
// Step C.1 ──► 创建一次性定时器（1s 周期）
TimerHandle_t watchdog = xTimerCreate("Watchdog", 1000, pdTRUE, NULL, Watchdog_Callback);

// Step C.2 ──► 定时器回调中直接处理
void Watchdog_Callback(TimerHandle_t xTimer) {
    if (++timeout_cnt > THRESHOLD) {
        TriggerAlarm();
        CAN0_SoftReset();
    }
}
```

---

### 模式 D：互斥量（硬件保护）

**场景：** 多个任务共享同一个硬件外设（CAN/UART 等），需要互斥访问。

**示例：** `bsp_can.c` → `Can0SendMsg`

```c
// Step D.1 ──► 创建互斥量（在 BSP_Init 中）
SemaphoreHandle_t CAN0_Mutex = xSemaphoreCreateMutex();

// Step D.2 ──► 发送前获取互斥量（阻塞直到获得）
xSemaphoreTake(CAN0_Mutex, portMAX_DELAY);

// Step D.3 ──► 发送 CAN/UART 数据
Can0SendMsg(ID, 1, data, 8);

// Step D.4 ──► 发送完成后释放互斥量
xSemaphoreGive(CAN0_Mutex);
```

---

## 六、API 速查表

### 任务管理

```c
// 创建任务（Step 5.6 / Step 5）
xTaskCreate(
    pvTaskCode,           // 任务函数指针（函数名）
    pcName,               // 任务名（字符串，用于调试）
    usStackDepth,          // 栈深度（word），建议 ≥ configMINIMAL_STACK_SIZE
    pvParameters,         // 参数（可为 NULL）
    uxPriority,           // 优先级（0~configMAX_PRIORITIES-1，数字越大优先级越高）
    pxCreatedTask         // 输出句柄（可为 NULL）
);

// 任务延时（让出 CPU）
vTaskDelay(ticks);              // 相对延时（延时 N tick 后就绪）
vTaskDelayUntil(&lastTick, inc); // 绝对延时（每 N tick 执行一次，更精确）

// 删除任务
vTaskDelete(taskHandle);  // 传入 NULL 删除自己

// 获取任务信息
TaskHandle_t handle = xTaskGetHandle("TaskName");
eTaskState state = eTaskGetState(handle);
UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);  // 栈高水位
```

### 消息队列

```c
// 创建（Step 5.2）
QueueHandle_t q = xQueueCreate(uxQueueLength, uxItemSize);

// ISR 发送（模式 A，Step A.3）
xQueueSendToBackFromISR(q, &item, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// 任务发送
xQueueSendToBack(q, &item, portMAX_DELAY);   // 队列满则阻塞
xQueueSendToBack(q, &item, 0);                // 队列满则立即返回 errQUEUE_FULL

// 任务接收
xQueueReceive(q, &item, portMAX_DELAY);       // 队列空则阻塞
xQueueReceive(q, &item, 100);                 // 队列空则等待 100 tick 后返回 pdFALSE
```

### 软件定时器

```c
// 创建（Step 5.3）
TimerHandle_t t = xTimerCreate(
    pcTimerName,              // 名字
    xTimerPeriodInTicks,      // 周期（tick）
    uxAutoReload,             // pdTRUE=自动重载，pdFALSE=一次性
    pvTimerID,                // ID（回调中区分定时器）
    pxCallbackFunction         // 回调函数
);

// 启动/停止（Step 5.7）
xTimerStart(t, portMAX_DELAY);
xTimerStop(t, portMAX_DELAY);

// 从 ISR 启动
xTimerStartFromISR(t, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// 修改周期
xTimerChangePeriod(t, newPeriod, portMAX_DELAY);

// 获取定时器 ID
void *id = pvTimerGetTimerID(t);
```

### 信号量（二值 / 互斥量）

```c
// 创建二值信号量（模式 B，Step 5.4）
SemaphoreHandle_t sem = xSemaphoreCreateBinary();

// ISR 释放（模式 B）
xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// 任务获取（阻塞）
xSemaphoreTake(sem, portMAX_DELAY);

// 创建互斥量（模式 D，Step 5.5）
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

// 获取/释放（模式 D，Step D.2/D.3）
xSemaphoreTake(mutex, portMAX_DELAY);
// 访问共享资源...
xSemaphoreGive(mutex);
```

### 中断安全

```c
// 进入/退出临界段（ISR 和任务中均可）
taskENTER_CRITICAL_FROM_ISR();   // → 返回 uint32_t ulReturn
taskEXIT_CRITICAL_FROM_ISR(ulReturn);

// 任务中
taskENTER_CRITICAL();
taskEXIT_CRITICAL();

// 全局禁/启中断（慎用，屏蔽时间可能很长）
taskDISABLE_INTERRUPTS();
taskENABLE_INTERRUPTS();
```

### 内存 / 调试

```c
// 获取剩余 heap
size_t free = xPortGetFreeHeapSize();

// 获取 tick 计数
TickType_t ticks = xTaskGetTickCount();

// 获取运行时间计数器值
uint32_t val = portGET_RUN_TIME_COUNTER_VALUE();

// 获取 CPU 使用统计
vTaskGetRunTimeStats(buf);  // 填充 buf 字符串

// 获取任务数
UBaseType_t n = uxTaskGetNumberOfTasks();
```

### 钩子函数（在 `FreeRTOSPerfect.c` 中实现）

```c
void vApplicationIdleHook(void);                          // CPU 空闲时（喂狗、LED）
void vApplicationTickHook(void);                           // 每个 tick 中断时（很短暂）
void vApplicationStackOverflowHook(TaskHandle_t, char*);    // 栈溢出时
void vApplicationMallocFailedHook(void);                    // 内存分配失败时
void vApplicationDaemonTaskStartupHook(void);              // Timer Daemon 启动时
```