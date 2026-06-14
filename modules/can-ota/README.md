# Module 1 — CAN 总线 OTA 升级（CAN-OTA）

> 本模块展示通过 CAN 总线对从机进行 OTA 在线固件升级的完整协议实现，含环形缓冲区、CRC16 校验、软件定时器超时、广播升级模式。

## 📁 源码位置

| 文件 | 说明 |
|------|------|
| `Application/app/upgrade.c` | OTA 升级核心逻辑（带完整中文注释） |
| `Application/app/upgrade.h` | OTA 数据结构与常量定义 |

## 🔬 协议概览

```
主机（上车控制器）                    从机（RCM/排种器）
    │                                    │
    │──── upgrade 请求 ──────────────────→│  从机进 BootLoader
    │←─────── permit ─────────────────────│
    │──── 版本帧数 ──────────────────────→│
    │←─────── ACK ───────────────────────│
    │──── 分帧固件数据（512字节/帧）──────→│  每帧等 ACK
    │←─────── ACK:Index ─────────────────│
    │      （重复直到发完）                │
    │──── 最后一帧 + MoveFlash ──────────→│
    │←─────── ACK:MoveFlash ─────────────│  从机执行 Flash 搬运
    │←─────── ACK:UpdateSuccess ─────────│
```

## 🏗 搭建步骤（初始化顺序）

1. `RecvBufInit()` → 分配接收缓冲区 + 初始化环形缓冲区
2. `UpgradeTask_Init()` → 创建升级任务（`xTaskCreate`）
3. 任务内等待 `s_upgradeTask.init_flag = 1` 外部触发
4. 任务创建软件定时器（`xTimerCreate` → `upgradeTimer` 回调）

## ⚙️ OTA 状态机（主机侧 UpgradeTask_Process）

```
eInit → eWaitVer → eWaitUpdate1stPack → eWaitUpdatePack → eMoveFlash
```

**各状态说明：**

| 状态 | 说明 | 关键操作 |
|------|------|---------|
| `eInit` | 初始化 | 擦除 Flash / 打开文件系统，发 permit |
| `eWaitVer` | 等版本确认 | 收 TBOX 版本消息，发 ACK |
| `eWaitUpdate1stPack` | 等第一帧 | 环形缓冲区拼包，CRC 校验，写 Flash |
| `eWaitUpdatePack` | 等后续帧 | 循环收包、写 Flash，超时重发 |
| `eMoveFlash` | 发搬运指令 | 发 MoveFlash ACK，升级成功/失败处理 |

## 🔑 核心 API（OTA 专用）

```c
// 环形缓冲区
CircularBufferInit(&ctx, buf, size, elemSize);
CircularBufferPushBack(&ctx, &byte);
CircularBufferPopFront(&ctx, data, len);
CircularBufferPeek(&ctx, offset, &ptr);
CircularBufferSize(&ctx);

// CRC16（跳字节校验）
GetSkipCrc16OfRam(upgrade_t);  // 每8字节跳1字节序号

// 软件定时器
EnableTimer(true, timeout_10ms);  // 启动超时定时器
GetTimerOut();                      // 查询超时标志
// 回调：upgradeTimer() — 定时器周期调用，超时置位

// 任务挂起/恢复（升级期间停止业务）
vTaskSuspend(taskHandle);
xTimerStop(timerHandle, portMAX_DELAY);
vTaskResume(taskHandle);
xTimerStart(timerHandle, portMAX_DELAY);

// 分帧发送
SendUpgradeData(buf, frameIndex, totalFrame);
// 每帧 600 字节 = 2B 帧头 + 2B 序号 + 2B 总帧数 + 520B 数据 + 2B CRC
// 数据区：每 8 字节插 1 字节帧序号 → 512 有效数据

// ACK 响应
SendACK(ack_type, need_index);
// ack_type: eACK_Simple/Index/CheckErr/MoveFlash/UpdateSuccess
```

## 📐 数据帧格式

```
结构体 upgrade_t（600 字节）：
├── head1='S' (1B)
├── head2='S' (1B)
├── num (2B)         ← 数据区有效字节数
├── index_frame (2B)  ← 当前帧序号（0~N）
├── total_frame (2B)   ← 总帧数
├── data[520]         ← 512B有效数据 + 8B帧序号填充
└── checksum (2B)     ← CRC16（跳过序号字节）
```

## 📐 ACK 帧格式

```
字节[0..3] = "ACK:"
字节[4]    = ack_type（状态码）
字节[5..6] = need_index（期望帧序号，高低字节）
字节[7]    = 0x00
```

## 🚀 广播升级模式（Upgrade_BroadcastMode）

同时对所有从机发送固件，无需逐个等待 ACK，适合批量部署。

## ⚠️ 工程注意事项

1. 升级期间 `SuspendAllTasks()` 挂起所有业务任务，防止干扰
2. 升级完成后 `ResumeAllTasks()` 恢复所有任务
3. 主机自身升级完成后调用 `NVIC_SystemReset()` 软复位
4. 环形缓冲区线程安全（中断写/任务读需加临界区保护）
5. CRC 计算时跳过每 8 字节的序号位，与从机 BootLoader 端算法一致
