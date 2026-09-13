# FreeRTOS 常用 API 速查

基于本仓库 FreeRTOS Kernel V11.1.0，包含任务、队列、信号量、互斥量、事件组、任务通知和软件定时器。

以下只列接口签名与简短注释。标注“宏”的接口按等效函数签名展示参数类型，不是实际函数声明；接口是否可用取决于 `FreeRTOSConfig.h` 配置。

## 通用约定

```c
pdMS_TO_TICKS( xTimeInMs )       // 宏：毫秒转 Tick
portMAX_DELAY                   // 最大 Tick 值；支持无限等待的接口通常还要求 INCLUDE_vTaskSuspend == 1
pdTRUE / pdFALSE               // 真 / 假
pdPASS / pdFAIL                // 成功 / 失败，具体含义以接口为准
```

`xTicksToWait`、`xBlockTime` 的单位为 Tick，取 0 表示不等待。`FromISR` 接口不阻塞，且调用中断的优先级必须满足端口要求。

```c
BaseType_t xHigherPriorityTaskWoken = pdFALSE; // 每次 ISR 入口初始化，可供多次 FromISR 调用共用
portYIELD_FROM_ISR( xHigherPriorityTaskWoken ); // 宏：在 ISR 末尾按需请求任务切换
```

## 1. 任务

头文件：`task.h`。创建成功后任务进入就绪态；没有单独的“启动某个任务”接口，统一由调度器调度。

```c
// 动态创建；uxStackDepth 的单位为 StackType_t 元素数，pxCreatedTask 输出句柄
BaseType_t xTaskCreate( TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       const configSTACK_DEPTH_TYPE uxStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask );

// 静态创建；调用者提供栈和 TCB 存储区，返回任务句柄
TaskHandle_t xTaskCreateStatic( TaskFunction_t pxTaskCode,
                               const char * const pcName,
                               const configSTACK_DEPTH_TYPE uxStackDepth,
                               void * const pvParameters,
                               UBaseType_t uxPriority,
                               StackType_t * const puxStackBuffer,
                               StaticTask_t * const pxTaskBuffer );

void vTaskStartScheduler( void );                      // 启动调度器
void vTaskDelete( TaskHandle_t xTaskToDelete );         // 删除任务，NULL 表示自身
void vTaskSuspend( TaskHandle_t xTaskToSuspend );       // 挂起任务，NULL 表示自身
void vTaskResume( TaskHandle_t xTaskToResume );         // 恢复被挂起的任务
BaseType_t xTaskResumeFromISR( TaskHandle_t xTaskToResume ); // ISR 恢复任务，返回是否需要切换

void vTaskDelay( const TickType_t xTicksToDelay );      // 相对延时
BaseType_t xTaskDelayUntil( TickType_t * const pxPreviousWakeTime,
                           const TickType_t xTimeIncrement ); // 周期延时，更新上次唤醒时间

UBaseType_t uxTaskPriorityGet( const TaskHandle_t xTask ); // 查询优先级
void vTaskPrioritySet( TaskHandle_t xTask, UBaseType_t uxNewPriority ); // 设置优先级
TaskHandle_t xTaskGetCurrentTaskHandle( void );         // 当前任务句柄
eTaskState eTaskGetState( TaskHandle_t xTask );          // 查询任务状态
UBaseType_t uxTaskGetStackHighWaterMark( TaskHandle_t xTask ); // 历史最小剩余栈，单位为栈元素
TickType_t xTaskGetTickCount( void );                   // 当前 Tick
TickType_t xTaskGetTickCountFromISR( void );            // ISR 查询 Tick

void vTaskSuspendAll( void );                          // 挂起调度器，中断仍可运行
BaseType_t xTaskResumeAll( void );                     // 恢复调度器，返回是否已请求切换

taskYIELD()                                           // 宏：主动让出 CPU
taskENTER_CRITICAL()                                  // 宏：进入任务临界区
taskEXIT_CRITICAL()                                   // 宏：退出任务临界区
taskENTER_CRITICAL_FROM_ISR()                         // 宏：ISR 临界区入口，返回原屏蔽状态
taskEXIT_CRITICAL_FROM_ISR( xSavedInterruptStatus )    // 宏：恢复原屏蔽状态
tskIDLE_PRIORITY                                      // 宏：空闲任务优先级
```

## 2. 队列

头文件：`queue.h`。队列按值复制固定大小的项目；`pvItemToQueue` 和 `pvBuffer` 指向至少一个项目大小的数据区。

```c
// 宏：动态创建；容量为 uxQueueLength 项，每项 uxItemSize 字节
QueueHandle_t xQueueCreate( UBaseType_t uxQueueLength, UBaseType_t uxItemSize );

// 宏：静态创建；存储区至少 uxQueueLength * uxItemSize 字节
QueueHandle_t xQueueCreateStatic( UBaseType_t uxQueueLength,
                                 UBaseType_t uxItemSize,
                                 uint8_t * pucQueueStorage,
                                 StaticQueue_t * pxQueueBuffer );

// 宏：发送到队尾；队列满时最多等待 xTicksToWait
BaseType_t xQueueSend( QueueHandle_t xQueue, const void * pvItemToQueue,
                      TickType_t xTicksToWait );
BaseType_t xQueueSendToBack( QueueHandle_t xQueue, const void * pvItemToQueue,
                            TickType_t xTicksToWait ); // 宏：同 xQueueSend
BaseType_t xQueueSendToFront( QueueHandle_t xQueue, const void * pvItemToQueue,
                             TickType_t xTicksToWait ); // 宏：发送到队首
BaseType_t xQueueOverwrite( QueueHandle_t xQueue, const void * pvItemToQueue ); // 宏：仅限长度为 1

BaseType_t xQueueReceive( QueueHandle_t xQueue, void * const pvBuffer,
                         TickType_t xTicksToWait );    // 接收并移除一项
BaseType_t xQueuePeek( QueueHandle_t xQueue, void * const pvBuffer,
                      TickType_t xTicksToWait );       // 读取一项但不移除

// 宏：ISR 发送；pxHigherPriorityTaskWoken 输出切换请求
BaseType_t xQueueSendFromISR( QueueHandle_t xQueue, const void * pvItemToQueue,
                             BaseType_t * pxHigherPriorityTaskWoken );
BaseType_t xQueueReceiveFromISR( QueueHandle_t xQueue, void * const pvBuffer,
                                BaseType_t * const pxHigherPriorityTaskWoken ); // ISR 接收

UBaseType_t uxQueueMessagesWaiting( const QueueHandle_t xQueue ); // 当前项目数
UBaseType_t uxQueueSpacesAvailable( const QueueHandle_t xQueue ); // 剩余空位数
BaseType_t xQueueReset( QueueHandle_t xQueue );         // 宏：清空队列
void vQueueDelete( QueueHandle_t xQueue );              // 删除队列
```

## 3. 信号量

头文件：`semphr.h`。以下接口均为宏。二值信号量创建后计数为 0；计数信号量由 `uxInitialCount` 指定初值。

```c
SemaphoreHandle_t xSemaphoreCreateBinary( void );      // 创建二值信号量
SemaphoreHandle_t xSemaphoreCreateBinaryStatic( StaticSemaphore_t * pxStaticSemaphore ); // 静态创建

SemaphoreHandle_t xSemaphoreCreateCounting( UBaseType_t uxMaxCount,
                                           UBaseType_t uxInitialCount ); // 最大计数、初始计数
SemaphoreHandle_t xSemaphoreCreateCountingStatic( UBaseType_t uxMaxCount,
                                                 UBaseType_t uxInitialCount,
                                                 StaticSemaphore_t * pxSemaphoreBuffer ); // 静态创建

BaseType_t xSemaphoreTake( SemaphoreHandle_t xSemaphore, TickType_t xBlockTime ); // 获取，计数减 1
BaseType_t xSemaphoreGive( SemaphoreHandle_t xSemaphore ); // 提供，计数加 1；已满则失败，不阻塞

BaseType_t xSemaphoreTakeFromISR( SemaphoreHandle_t xSemaphore,
                                 BaseType_t * pxHigherPriorityTaskWoken ); // ISR 获取，不等待
BaseType_t xSemaphoreGiveFromISR( SemaphoreHandle_t xSemaphore,
                                 BaseType_t * pxHigherPriorityTaskWoken ); // ISR 提供

UBaseType_t uxSemaphoreGetCount( SemaphoreHandle_t xSemaphore ); // 当前计数
void vSemaphoreDelete( SemaphoreHandle_t xSemaphore );          // 删除信号量
```

## 4. 互斥量

头文件：`semphr.h`。以下接口均为宏。创建后可立即获取；由持有任务释放，支持优先级继承，不在 ISR 中获取或释放。

```c
SemaphoreHandle_t xSemaphoreCreateMutex( void );       // 创建普通互斥量
SemaphoreHandle_t xSemaphoreCreateMutexStatic( StaticSemaphore_t * pxMutexBuffer ); // 静态创建

BaseType_t xSemaphoreTake( SemaphoreHandle_t xSemaphore, TickType_t xBlockTime ); // 获取普通互斥量
BaseType_t xSemaphoreGive( SemaphoreHandle_t xSemaphore ); // 释放普通互斥量
TaskHandle_t xSemaphoreGetMutexHolder( SemaphoreHandle_t xSemaphore ); // 查询持有者

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex( void ); // 创建递归互斥量
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic( StaticSemaphore_t * pxStaticSemaphore ); // 静态创建
BaseType_t xSemaphoreTakeRecursive( SemaphoreHandle_t xMutex, TickType_t xBlockTime ); // 同一任务可重复获取
BaseType_t xSemaphoreGiveRecursive( SemaphoreHandle_t xMutex ); // 与递归获取次数配对

void vSemaphoreDelete( SemaphoreHandle_t xSemaphore ); // 删除互斥量
```

## 5. 事件组

头文件：`event_groups.h`。事件位表达条件；重复置同一位不会累计次数。等待接口返回事件位快照，需要检查目标位是否满足。

```c
EventGroupHandle_t xEventGroupCreate( void );          // 动态创建，事件位初始为 0
EventGroupHandle_t xEventGroupCreateStatic( StaticEventGroup_t * pxEventGroupBuffer ); // 静态创建

EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup,
                               const EventBits_t uxBitsToSet ); // 设置指定事件位
EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToClear ); // 清除指定事件位，返回清除前的值
EventBits_t xEventGroupGetBits( EventGroupHandle_t xEventGroup ); // 宏：查询事件位

// 等待事件；xClearOnExit 为真时满足条件后清位，xWaitForAllBits 为真等待全部位、否则任意位
EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait );

// 设置自身事件位，并等待所有目标位；用于多个任务同步
EventBits_t xEventGroupSync( EventGroupHandle_t xEventGroup,
                            const EventBits_t uxBitsToSet,
                            const EventBits_t uxBitsToWaitFor,
                            TickType_t xTicksToWait );

// ISR 置位/清位：通常为宏，按 Trace 配置也可为函数；投递给定时器守护任务延后执行
BaseType_t xEventGroupSetBitsFromISR( EventGroupHandle_t xEventGroup,
                                    const EventBits_t uxBitsToSet,
                                    BaseType_t * pxHigherPriorityTaskWoken );
BaseType_t xEventGroupClearBitsFromISR( EventGroupHandle_t xEventGroup,
                                      const EventBits_t uxBitsToClear );
EventBits_t xEventGroupGetBitsFromISR( EventGroupHandle_t xEventGroup ); // ISR 直接查询
void vEventGroupDelete( EventGroupHandle_t xEventGroup ); // 删除事件组
```

## 6. 任务通知

头文件：`task.h`。通知存储在任务 TCB 中，无需单独创建或删除。以下通知 API 均为宏；默认使用通知索引 0，等待和获取操作针对当前任务。

```c
// 发送通知；eAction 决定如何修改通知值
BaseType_t xTaskNotify( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction );
BaseType_t xTaskNotifyFromISR( TaskHandle_t xTaskToNotify, uint32_t ulValue,
                             eNotifyAction eAction,
                             BaseType_t * pxHigherPriorityTaskWoken ); // ISR 发送

// 等待通知；两个掩码指定进入/退出时清除的位，pulNotificationValue 输出通知值
BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t * pulNotificationValue,
                           TickType_t xTicksToWait );

BaseType_t xTaskNotifyGive( TaskHandle_t xTaskToNotify ); // 通知值加 1
void vTaskNotifyGiveFromISR( TaskHandle_t xTaskToNotify,
                            BaseType_t * pxHigherPriorityTaskWoken ); // ISR 通知值加 1
uint32_t ulTaskNotifyTake( BaseType_t xClearCountOnExit,
                           TickType_t xTicksToWait ); // 等待非零值；退出时清零/减 1，返回修改前的值

BaseType_t xTaskNotifyStateClear( TaskHandle_t xTask ); // 清除通知待处理状态，不清通知值
uint32_t ulTaskNotifyValueClear( TaskHandle_t xTask, uint32_t ulBitsToClear ); // 清除指定通知值位

// 带索引接口；索引范围为 0 到 configTASK_NOTIFICATION_ARRAY_ENTRIES - 1
BaseType_t xTaskNotifyIndexed( TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify,
                              uint32_t ulValue, eNotifyAction eAction );
BaseType_t xTaskNotifyWaitIndexed( UBaseType_t uxIndexToWaitOn,
                                  uint32_t ulBitsToClearOnEntry,
                                  uint32_t ulBitsToClearOnExit,
                                  uint32_t * pulNotificationValue,
                                  TickType_t xTicksToWait );
uint32_t ulTaskNotifyTakeIndexed( UBaseType_t uxIndexToWaitOn,
                                  BaseType_t xClearCountOnExit, TickType_t xTicksToWait );

// eNotifyAction 枚举值
eNoAction                     // 仅发送通知，不修改通知值
eSetBits                      // 按位 OR
eIncrement                    // 加 1
eSetValueWithOverwrite        // 覆盖通知值
eSetValueWithoutOverwrite     // 无待处理通知时才写入，否则返回失败
```

## 7. 软件定时器

头文件：`timers.h`。创建后尚未启动。回调在定时器守护任务中执行，应避免阻塞。

启动、停止、复位、改周期和删除通过命令队列异步执行；返回 `pdPASS` 表示命令入队成功。这里的 `xTicksToWait` 表示等待命令队列空间的时间。

```c
// 创建；周期非零，xAutoReload 为 pdTRUE 表示周期重载、pdFALSE 表示一次性
TimerHandle_t xTimerCreate( const char * const pcTimerName,
                           const TickType_t xTimerPeriodInTicks,
                           const BaseType_t xAutoReload,
                           void * const pvTimerID,
                           TimerCallbackFunction_t pxCallbackFunction );

TimerHandle_t xTimerCreateStatic( const char * const pcTimerName,
                                 const TickType_t xTimerPeriodInTicks,
                                 const BaseType_t xAutoReload,
                                 void * const pvTimerID,
                                 TimerCallbackFunction_t pxCallbackFunction,
                                 StaticTimer_t * pxTimerBuffer ); // 静态创建

// 用户自定义回调的签名
void vTimerCallback( TimerHandle_t xTimer );

BaseType_t xTimerStart( TimerHandle_t xTimer, TickType_t xTicksToWait ); // 宏：启动/重新计时
BaseType_t xTimerStop( TimerHandle_t xTimer, TickType_t xTicksToWait );  // 宏：停止
BaseType_t xTimerReset( TimerHandle_t xTimer, TickType_t xTicksToWait ); // 宏：从本次调用时刻重新计时
BaseType_t xTimerChangePeriod( TimerHandle_t xTimer, TickType_t xNewPeriod,
                              TickType_t xTicksToWait ); // 宏：改周期并启动/重新计时
BaseType_t xTimerDelete( TimerHandle_t xTimer, TickType_t xTicksToWait ); // 宏：删除

BaseType_t xTimerStartFromISR( TimerHandle_t xTimer,
                             BaseType_t * pxHigherPriorityTaskWoken ); // 宏：ISR 启动
BaseType_t xTimerStopFromISR( TimerHandle_t xTimer,
                            BaseType_t * pxHigherPriorityTaskWoken ); // 宏：ISR 停止
BaseType_t xTimerResetFromISR( TimerHandle_t xTimer,
                             BaseType_t * pxHigherPriorityTaskWoken ); // 宏：ISR 复位
BaseType_t xTimerChangePeriodFromISR( TimerHandle_t xTimer, TickType_t xNewPeriod,
                                    BaseType_t * pxHigherPriorityTaskWoken ); // 宏：ISR 改周期

BaseType_t xTimerIsTimerActive( TimerHandle_t xTimer ); // 是否正在计时
TickType_t xTimerGetPeriod( TimerHandle_t xTimer );     // 周期 Tick 数
TickType_t xTimerGetExpiryTime( TimerHandle_t xTimer ); // 活动定时器的绝对到期 Tick
void * pvTimerGetTimerID( const TimerHandle_t xTimer ); // 获取用户 ID
void vTimerSetTimerID( TimerHandle_t xTimer, void * pvNewID ); // 设置用户 ID
```

# 阅读笔记
基于本仓库 FreeRTOS Kernel V11.1.0 版本，阅读笔记如下：

## 后续阅读流程

》链表list->任务tasks->队列queues(信号量互斥量)->事件events->任务通知->定时器timers
》接口port->启动流程->内存管理

## 任务切换
运行逻辑是在CPU的寄存器中运行计算的，速度极快，然后在RAM中模仿寄存器的模式存储寄存器内容。
总流程是CPU和RAM间通过切换寄存器内容切换任务上下文，实现任务之间的切换。
pendsv异常中断 用于触发任务切换。硬件保存栈其它相关寄存器，在中断中手动进行保存栈r4-r11。内容被压栈到任务栈中，由指针pxTopOfStack指向当前栈顶。同时查看下一优先级最高得就绪任务，通过指针取栈顶相关寄存器值，恢复任务上下文，实现任务切换。

## 栈内容
任务栈是 RAM 中一块连续的、后进先出的内存区域，每个任务独享一份。它用于保存：
1.任务切换时的寄存器上下文
2.函数调用返回地址、参数、栈帧
3.局部变量和临时数据
4.中断嵌套时的额外现场(叠加)
5.浮点上下文（如果启用 FPU）
任务栈由 TCB 中的两个指针管理：pxStack 和 pxTopOfStack。

任务启动前会伪造初始现场pxPortInitialiseStack() 从高地址向低地址写入一组寄存器值，模拟任务刚被中断打断的状态。

以 ARM Cortex-M 为例，初始化后的栈布局：

text
高地址  ┌──────────────┐
        │    xPSR      │
        │    PC        │  ← 任务入口地址
        │    LR        │  ← prvTaskExitError
        │  R12, R3~R0  │
        │  EXC_RETURN  │
        │  R11 ~ R4    │
低地址  └──────────────┘
         ↑ pxTopOfStack 指向 R4
这个“假现场”让任务第一次被调度时，能像从没运行过一样直接跳转到入口函数执行。
完整栈布局示意（向下增长）：
高地址  ┌──────────────────────┐
        │  中断嵌套保存区        │
        ├──────────────────────┤
        │  ISR 局部变量/栈帧     │
        ├──────────────────────┤
        │  当前函数的局部变量     │
        ├──────────────────────┤
        │  函数调用链的栈帧       │
        ├──────────────────────┤
        │  硬件自动保存区         │
        ├──────────────────────┤
        │  软件手动保存区         │
低地址  └──────────────────────┘
         ↑ pxTopOfStack（切换时指向这里）
         ↑ pxStack（始终指向最低地址）

切换流程：
触发切换
  │
  ▼
挂起 PendSV
  │
  ▼
硬件自动保存旧任务：xPSR, PC, LR, R12, R3~R0 → 旧任务栈
  │
  ▼
软件手动保存旧任务：R4~R11 → 旧任务栈，更新 pxTopOfStack
  │
  ▼
vTaskSwitchContext()：选择新任务，更新 pxCurrentTCB
  │
  ▼
软件手动恢复新任务：从 pxTopOfStack 弹出 R4~R11，更新 PSP
  │
  ▼
异常返回：硬件自动弹出 R0~R3, R12, LR, PC, xPSR
  │
  ▼
新任务继续执行