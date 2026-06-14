# Module 3 — 双重看门狗保护（Watchdog）

> 本模块展示车载控制器的看门狗双重保护方案：片内 GD32 FWDGT（防止程序死锁）+ 外置 SGM706（防止系统断电），含超时时间计算、喂狗策略等工程实践。

## 📁 源码位置

| 文件 | 说明 |
|------|------|
| `Application/hardware/bsp_fwdtg.c` | 看门狗驱动（带完整中文注释） |
| `Application/hardware/bsp_fwdtg.h` | 看门狗头文件 |

## 🗺️ 双重保护架构

```
┌─────────────────────────────────────────────┐
│                  系统异常                     │
│  ┌─────────────┐    ┌──────────────────┐  │
│  │ 程序死锁/跑飞 │    │   系统断电/电源异常  │  │
│  └──────┬──────┘    └────────┬─────────┘  │
│         │                     │             │
│  片内 FWDGT              外置 SGM706        │
│  (GD32 内部)              (GPIO PE1 脉冲)   │
│  定期喂狗复位             脉冲停止则不复位    │
└─────────┼───────────────────┼───────────────┘
          │                   │
       芯片复位           硬件看门狗复位
```

## 🔬 片内看门狗（FWDGT）原理

GD32 内置独立看门狗 timer，使用内部 40KHz IRC 时钟：

```
喂狗周期 T = (4 × 256 × reload) / 40000  秒
           = reload × 0.0256              秒（约 26ms × reload）

reload 为 12 位，范围 1~4095
最大超时 ≈ 4095 × 26ms ≈ 106.5 秒
```

## 🔬 外置看门狗（SGM706）原理

外置芯片接收 GPIO PE1 的脉冲信号：
- 收到脉冲 → 内部计数器清零
- 脉冲停止 1.6 秒内 → 复位系统
- 芯片本身也需要定期喂狗脉冲

本项目 GPIO PE1 翻转脉冲同时重载片内看门狗，实现双重保护。

## 🏗 初始化步骤

```
Set_FwdtgTime(1)   → 计算 reload 值，配置 FWDGT 分频
                    └─ fwdgt_config(reload, FWDGT_PSC_DIV256)

Bsp_fwdtgInit()    → GPIO PE1 复用功能配置（输出）
                    └─ gpio_mode_set(GPIOE, GPIO_MODE_OUTPUT...)
                    └─ Set_FwdtgTime(HardWatchdogTime)
                    └─ Fwdtg_enabale() → fwdgt_counter_reload() + fwdgt_enable()
```

## ⚙️ 核心 API

```c
// 初始化
void Bsp_fwdtgInit(void);      // 片内 + 外置看门狗初始化
void Bsp_fwdtgInit_hw(void);   // 仅外置看门狗 GPIO（BootLoader 用）

// 喂狗
void Fwdtg_enabale(void);       // 使能片内看门狗（初始化最后调用一次）
void Feed_Fwdtg(void);          // 完整喂狗：GPIO PE1 翻转 + fwdgt_counter_reload()
void Feed_Fwdtg_hw(void);       // 仅喂外置：GPIO PE1 翻转

// 时间配置
void Set_FwdtgTime(uint16_t time_s);  // 设置片内看门狗超时时间（秒）
```

## 📐 片内看门狗超时计算

```c
// IRC = 40000 Hz（内部 40K 时钟）
// 分频 = 256
// 时钟频率 = 40000 / 256 ≈ 156.25 Hz
// 超时(秒) = reload / 156.25

void Set_FwdtgTime(uint16_t time_s) {
    uint16_t reload = time_s * 40000 / 256;  // IRC / 256
    if (reload > 4095) reload = 4095;        // 12 位上限
    fwdgt_config(reload, FWDGT_PSC_DIV256);
}
```

## 🔄 喂狗策略

| 场景 | 函数 | 说明 |
|------|------|------|
| 正常运行时 | `Feed_Fwdtg()` | GPIO 翻转 PE1 + 重载片内计数器 |
| BootLoader 模式 | `Feed_Fwdtg_hw()` | 仅 GPIO 翻转（片内 WDT 未启用） |
| 初始化 | `Fwdtg_enabale()` | 使能片内 WDT + 立即喂一次 |

**典型喂狗任务：**
```c
void WatchdogTask(void *param) {
    while(1) {
        Feed_Fwdtg();           // 脉冲喂外置 + 重载片内
        vTaskDelay(pdMS_TO_TICKS(500));  // 每 500ms 喂一次
    }
}
```

## ⚠️ 工程注意事项

1. **超时时间选择**：片内看门狗超时 > 喂狗周期，通常设为 1~2 秒
2. **BootLoader 中禁用片内 WDT**：BootLoader 运行时间短，只需外置看门狗
3. **喂狗任务优先级**：建议适中优先级，确保不被高频任务饿死
4. **GPIO PE1 翻转频率**：SGM706 通常要求每 1.6 秒内至少一次脉冲
5. **掉电保护**：外置看门狗独立于芯片，即使芯片完全死机也能复位
