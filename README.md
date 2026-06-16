# FreeRTOS Engineering Patterns

> Production-ready FreeRTOS engineering template based on ARM Cortex-M33 (GD32A50x)

## What I Did

Created a modular FreeRTOS engineering reference that distills real production firmware into 4 independent learning modules. Each module demonstrates a core RTOS programming pattern used in vehicle-grade embedded controllers, extracted from a production codebase with annotated source code and standalone documentation.

## Module Overview

| Module | Pattern | Key Concepts |
|--------|---------|-------------|
| **Base** | RTOS fundamentals | Task creation, queues, timers, semaphores, mutexes, hook functions |
| **CAN-OTA** | Remote firmware upgrade | CAN-based OTA bootloader, ring buffer, CRC16, broadcast mode |
| **UART-DMA** | Efficient serial I/O | 3-channel UART + DMA + mutex-guarded printf redirect |
| **Watchdog** | Dual supervision | Internal FWDGT + external supervisor (SGM706) for fail-safe |

## Tech Stack

| Layer | Technology |
|-------|-----------|
| MCU | ARM Cortex-M33 (GD32A50x, vehicle-grade) |
| RTOS | FreeRTOS V10.2.1 |
| IDE | Keil MDK |
| Middleware | Standard peripheral library |

## Key Design Patterns

1. **Ring buffer for CAN OTA** — Receives variable-length firmware frames over CAN into a ring buffer, decouples reception from flash writing, prevents data loss under burst traffic.

2. **Mutex-guarded shared UART** — Multiple tasks share one debug UART; mutex ensures printf output isn't interleaved. DMA handles the actual byte transfer.

3. **Dual watchdog strategy** — Hardware FWDGT catches task-level hangs; external SGM706 catches total MCU failures (clock loss, power glitch). Only both feeding = system alive.