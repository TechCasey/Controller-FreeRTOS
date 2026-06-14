/**
 * @file    simple_example.c
 * @brief   FreeRTOS 最简示例 — 实现文件
 *
 * =============================================================================
 * 目标：理解 FreeRTOS 的最基本工作流程
 * =============================================================================
 *
 * 这个示例展示了 FreeRTOS 最核心的四个概念：
 *
 * ① 任务（Task）    — 一个永不返回的 while(1) 循环函数
 * ② 消息队列（Queue）— 任务之间传递数据
 * ③ 软件定时器（Timer）— 周期性触发回调
 * ④ 信号量（Semaphore）— 任务间/ISR→任务 同步
 *
 * =============================================================================
 * 程序运行流程
 * =============================================================================
 *
 * 【初始化阶段】（在 SimpleTaskInit() 中）
 *
 * Step 1 ──► 定义任务参数（名字/栈/优先级）
 * Step 2 ──► 创建消息队列（xQueueCreate）
 * Step 3 ──► 创建二值信号量（xSemaphoreCreateBinary）
 * Step 4 ──► 创建软件定时器（xTimerCreate）
 * Step 5 ──► 创建任务（xTaskCreate）
 * Step 6 ──► 启动软件定时器（xTimerStart）
 *
 * 【运行阶段】（任务主循环 / 定时器回调）
 *
 * 【SimpleTask 任务】（Step 7~13，主循环运行）
 *
 * Step 7 ──► 等待信号量（xSemaphoreTake）→ 阻塞，直到定时器触发
 * Step 8 ──► 收到信号量后，清除标志
 * Step 9 ──► 打印任务运行状态（vTaskGetRunTimeStats 可用）
 * Step 10 ──► 打印 Free Heap 剩余量（内存监控）
 * Step 11 ──► 切换 LED 状态（硬件控制）
 * Step 12 ──► 进入延时，等待下一个信号量（回到 Step 7）
 * Step 13 ──► [自定义业务逻辑] 在这里添加你自己的代码
 *
 * 【SimpleTimer_Callback 定时器回调】（每 1 秒执行一次）
 *
 * Step 14 ──► 释放二值信号量 → SimpleTask 解除阻塞
 * Step 15 ──► 返回（定时器是自动重载的，下一秒再次触发）
 *
 * =============================================================================
 * 【核心自定义区】如何修改这个任务
 * =============================================================================
 *   Step 13 — 在 SimpleTask 中添加你的业务逻辑
 *   修改 SIMPLE_TIMER_PERIOD_TICK 可改变定时器周期
 *   修改 SIMPLE_TASK_PRIORITY 可改变任务优先级
 *   xQueueReceive 可以替换为 xQueueReceive(queue, &data, portMAX_DELAY)
 *   实现任务间数据传递
 * =============================================================================
 */

#include "simple_example.h"

/* ============================================================
 * 静态变量（文件作用域）
 * ============================================================ */
/* 【核心自定义区】消息队列句柄（内部使用） */
static QueueHandle_t s_simpleQueue = NULL;

/* 【核心自定义区】二值信号量句柄（内部使用） */
static SemaphoreHandle_t s_simpleSemaphore = NULL;

/* 【核心自定义区】定时器句柄（内部使用） */
static TimerHandle_t s_simpleTimer = NULL;

/* ============================================================
 * Step 15 — 定时器回调函数（每 1 秒执行一次）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 定时器回调在 Timer Daemon 任务中执行（高优先级）
 * ⚠️ 回调函数中不能调用会阻塞的 API！
 * ⚠️ 回调函数中不能调用 vTaskDelay！
 * ─────────────────────────────────────────────────────────────
 * 作用：定时释放信号量，唤醒 SimpleTask
 *
 * 定时器是自动重载的（pdTRUE），
 * 执行完后会自动重装周期，下一秒再次触发。
 * ============================================================ */
static void SimpleTimer_Callback(TimerHandle_t xTimer)
{
    /* Step 14 ──► 释放二值信号量
     * xSemaphoreGive(s_simpleSemaphore) 会让 SimpleTask 解除阻塞，
     * SimpleTask 将在下一个调度周期获得 CPU。
     *
     * 注意：这是从定时器任务（Timer Daemon）调用的，
     * 不是普通任务，所以用 xSemaphoreGive 而不是 xSemaphoreGiveFromISR。
     */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 也可以用 FromISR 版本，更规范（虽然这里不是 ISR，但 FromISR 也可以在任务中用） */
    xSemaphoreGive(s_simpleSemaphore);
    (void)xHigherPriorityTaskWoken; /* 未使用，消除编译警告 */
}

/* ============================================================
 * Step 7~13 — 简单任务主函数（永不返回）
 * ─────────────────────────────────────────────────────────────
 * 这是一个 FreeRTOS 任务函数的典型模板：
 *   - 参数是 void *param（可以为 NULL）
 *   - 函数永不返回（while(1) 循环）
 *   - 通过 vTaskDelay() 让出 CPU，避免忙等待
 * ─────────────────────────────────────────────────────────────
 * 运行流程：
 *   等待信号量 → 收到 → 处理 → 延时 → 等待信号量 → ...
 * ============================================================ */
static void SimpleTask(void *param)
{
    (void)param;  /* 未使用参数，消除编译警告 */

    /* 任务启动时的初始化代码写在这里 */
    debug_printf(INFO_ORDINARY, "[SimpleTask] Started.\r\n");

    while (1) {
        /* ---------------------------------------------------------------
         * Step 7 — 等待信号量（阻塞等待）
         * ---------------------------------------------------------------
         * xSemaphoreTake(s_simpleSemaphore, portMAX_DELAY)：
         *   - 尝试获取信号量
         *   - 如果信号量不可用（定时器未触发），任务进入阻塞状态
         *   - portMAX_DELAY = 无限等待，直到信号量可用
         *   - 信号量可用时，函数立即返回 pdPASS，任务继续执行
         *
         * 效果：任务在这里"睡觉"，不消耗 CPU，直到定时器释放信号量
         * --------------------------------------------------------------- */
        BaseType_t ret = xSemaphoreTake(s_simpleSemaphore, portMAX_DELAY);

        if (ret == pdPASS) {
            /* ---------------------------------------------------------------
             * Step 8 — 收到信号量后的处理
             * ---------------------------------------------------------------
             * 可以在这里添加业务逻辑，
             * 例如：读取传感器数据、处理通信帧、控制执行器
             */

            /* 打印当前系统状态（调试用） */
            debug_printf(INFO_ORDINARY, "[SimpleTask] Timer triggered at %lu ms\r\n",
                         (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));

            /* 打印剩余 heap 大小（监控内存使用情况） */
            debug_printf(INFO_ORDINARY, "[SimpleTask] Free heap: %d bytes\r\n",
                         (int)xPortGetFreeHeapSize());

            /* ---------------------------------------------------------------
             * Step 9 — 打印每个任务的 CPU 使用时间（需要开启 configGENERATE_RUN_TIME_STATS）
             * ---------------------------------------------------------------
             * vTaskGetRunTimeStats() 会填充一个字符串，
             * 包含每个任务的名字、运行时间、CPU 占用百分比。
             * 在调试阶段开启，帮助了解各任务的 CPU 消耗。
             */
            /* char buf[512];
             * vTaskGetRunTimeStats(buf);
             * debug_printf(INFO_ORDINARY, "%s\r\n", buf);
             */

            /* ---------------------------------------------------------------
             * Step 10 — 打印任务栈高水位（检测栈是否即将溢出）
             * ---------------------------------------------------------------
             * uxTaskGetStackHighWaterMark() 返回任务栈的"高水位"，
             * 即任务运行过程中剩余的最小栈空间。
             * 如果这个值接近 0，说明栈即将溢出。
             */
            /* UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
             * debug_printf(INFO_ORDINARY, "Stack HWM: %u words\r\n", (unsigned int)hwm);
             */

            /* ---------------------------------------------------------------
             * Step 11 — LED 状态切换（硬件控制示例）
             * ---------------------------------------------------------------
             * SetLedToggle(LED_RUN) 每调用一次切换一次 LED 状态，
             * 用于指示系统是否在正常运行（心跳灯）。
             * --------------------------------------------------------------- */
            /* SetLedToggle(LED_RUN); */

            /* ---------------------------------------------------------------
             * Step 12 — 任务延时（让出 CPU，给其他任务运行机会）
             * ---------------------------------------------------------------
             * vTaskDelay(100)：延时 100ms（configTICK_RATE_HZ=1000）
             * 延时期间 CPU 可以运行其他任务。
             * 延时结束后任务重新进入就绪状态，等待调度。
             *
             * ⚠️ 注意：vTaskDelay 是"相对延时"（从调用时刻往后延时），
             *   如果需要精确的周期执行（如每 1 秒执行一次），
             *   应该用 vTaskDelayUntil。
             *
             * 示例（精确 1 秒周期）：
             *   static TickType_t LastWakeTime = 0;
             *   while (1) {
             *       vTaskDelayUntil(&LastWakeTime, 1000);
             *       // 精确每 1000ms 执行一次
             *   }
             */
            vTaskDelay(100);  /* 100ms 后回到循环开头再次等待信号量 */

            /* ---------------------------------------------------------------
             * Step 13 — 【核心自定义区】在这里添加你的业务逻辑
             * ---------------------------------------------------------------
             * 示例代码：
             *
             *   // 1. 从队列读取数据
             *   MyData_t data;
             *   if (xQueueReceive(s_simpleQueue, &data, 0) == pdPASS) {
             *       ProcessData(&data);
             *   }
             *
             *   // 2. 发送数据到队列（给其他任务）
             *   xQueueSendToBack(s_otherQueue, &data, 0);
             *
             *   // 3. 互斥量保护共享资源
             *   xSemaphoreTake(s_mutex, portMAX_DELAY);
             *   shared_resource.counter++;
             *   xSemaphoreGive(s_mutex);
             *
             *   // 4. 获取任务句柄
             *   TaskHandle_t handle = xTaskGetHandle("OtherTask");
             *   eTaskState state = eTaskGetState(handle);
             *
             *   // 5. 删除自己（如果任务完成）
             *   if (taskCompleted) {
             *       vTaskDelete(NULL);
             *   }
             * --------------------------------------------------------------- */
        }
    }
}

/* ============================================================
 * Step 1~6 — 任务初始化函数（在 main.c 中调用）
 * ─────────────────────────────────────────────────────────────
 * ⚠️ 此函数在 main() 的 Step 12 中调用（在 __enable_irq 之前）
 * ⚠️ 调用顺序：在 vTaskStartScheduler() 之前调用
 * ─────────────────────────────────────────────────────────────
 * 创建顺序（固定）：
 *   1. 消息队列 → 2. 信号量 → 3. 定时器 → 4. 任务 → 5. 启动定时器
 * ============================================================ */
void SimpleTaskInit(void)
{
    BaseType_t ret;

    /* ---------------------------------------------------------------
     * Step 2 — 创建消息队列（Example：每条消息 4 字节，最多 10 条）
     * ---------------------------------------------------------------
     * xQueueCreate(uxQueueLength, uxItemSize)：
     *   - uxQueueLength = 10（队列最多容纳 10 条消息）
     *   - uxItemSize = sizeof(uint32_t) = 4 字节
     *   - 返回 QueueHandle_t，失败返回 NULL
     *
     * 这里创建了队列但示例中未使用，
     * 如果需要任务间数据传递，可以在 Step 13 中用 xQueueSend/Receive
     * --------------------------------------------------------------- */
    s_simpleQueue = xQueueCreate(10, sizeof(uint32_t));

    if (s_simpleQueue == NULL) {
        debug_printf(INFO_ERR, "[SimpleTask] Queue create failed\r\n");
        configASSERT(s_simpleQueue);
    }

    /* ---------------------------------------------------------------
     * Step 3 — 创建二值信号量（用于定时器 → 任务同步）
     * ---------------------------------------------------------------
     * xSemaphoreCreateBinary()：
     *   - 初始状态：信号量不可用（值为 0）
     *   - Give 后：信号量可用（值为 1）
     *   - Take 后：信号量恢复不可用（值为 0）
     *
     * 使用场景：
     *   - 定时器回调 Give → 任务 Take（定时触发）
     *   - ISR Give → 任务 Take（异步事件响应）
     * --------------------------------------------------------------- */
    s_simpleSemaphore = xSemaphoreCreateBinary();

    if (s_simpleSemaphore == NULL) {
        debug_printf(INFO_ERR, "[SimpleTask] Semaphore create failed\r\n");
        configASSERT(s_simpleSemaphore);
    }

    /* ---------------------------------------------------------------
     * Step 4 — 创建软件定时器
     * ---------------------------------------------------------------
     * xTimerCreate(pcTimerName, xTimerPeriodInTicks, uxAutoReload,
     *              pvTimerID, pxCallbackFunction)：
     *   - pcTimerName       = "SimpleTimer"（名字，调试用）
     *   - xTimerPeriodInTicks = SIMPLE_TIMER_PERIOD_TICK = 1000（1 秒）
     *   - uxAutoReload     = pdTRUE（自动重载，每秒触发一次）
     *   - pvTimerID        = NULL（定时器 ID，用于回调中区分定时器）
     *   - pxCallbackFunction = SimpleTimer_Callback（回调函数指针）
     *
     * ⚠️ 定时器创建后不会自动启动，需要调用 xTimerStart()
     * --------------------------------------------------------------- */
    s_simpleTimer = xTimerCreate(
        "SimpleTimer",                              /* 定时器名字 */
        SIMPLE_TIMER_PERIOD_TICK,                  /* 周期：1000 tick = 1 秒 */
        pdTRUE,                                     /* pdTRUE = 自动重载 */
        (void *)NULL,                               /* 定时器 ID */
        SimpleTimer_Callback                        /* 回调函数 */
    );

    if (s_simpleTimer == NULL) {
        debug_printf(INFO_ERR, "[SimpleTask] Timer create failed\r\n");
        configASSERT(s_simpleTimer);
    }

    /* ---------------------------------------------------------------
     * Step 5 — 创建 FreeRTOS 任务
     * ---------------------------------------------------------------
     * xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters,
     *              uxPriority, pxCreatedTask)：
     *   - pvTaskCode       = SimpleTask（任务函数指针）
     *   - pcName          = "SimpleTask"（名字，调试用）
     *   - usStackDepth    = SIMPLE_TASK_STACK = 256（栈深度，单位 word）
     *   - pvParameters     = NULL（传递给任务的参数）
     *   - uxPriority      = SIMPLE_TASK_PRIORITY = osPriorityNormal = 7
     *   - pxCreatedTask   = NULL（不需要保存句柄）
     *
     * 返回值：pdPASS = 成功，errCOULD_NOT_ALLOCATE_REQUIRED_MEM = 内存不足
     * --------------------------------------------------------------- */
    ret = xTaskCreate(
        SimpleTask,                                 /* 任务函数 */
        "SimpleTask",                               /* 任务名 */
        SIMPLE_TASK_STACK,                          /* 栈深度（word） */
        NULL,                                       /* 参数（无） */
        SIMPLE_TASK_PRIORITY,                       /* 优先级 */
        NULL                                        /* 句柄（不需要） */
    );

    if (ret != pdPASS) {
        debug_printf(INFO_ERR, "[SimpleTask] Task create failed\r\n");
        configASSERT(ret);
    }

    /* ---------------------------------------------------------------
     * Step 6 — 启动软件定时器
     * ---------------------------------------------------------------
     * xTimerStart(timerHandle, xTicksToWait)：
     *   - timerHandle    = s_simpleTimer（定时器句柄）
     *   - xTicksToWait   = portMAX_DELAY（启动成功前一直等待）
     *
     * ⚠️ xTimerStart 是给调度器发送启动命令，不是立即启动
     * ⚠️ 定时器回调在 Timer Daemon 任务中执行，不是创建任务中执行
     * ⚠️ 定时器是自动重载的，会持续每秒触发一次，直到调用 xTimerStop
     * --------------------------------------------------------------- */
    ret = xTimerStart(s_simpleTimer, portMAX_DELAY);

    if (ret != pdPASS) {
        debug_printf(INFO_ERR, "[SimpleTask] Timer start failed\r\n");
        configASSERT(ret);
    }

    debug_printf(INFO_ORDINARY, "[SimpleTask] Init done.\r\n");
}