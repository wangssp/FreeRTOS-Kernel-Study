# FreeRTOS 任务调度学习笔记——tasks.c

`tasks.c` 负责创建和删除任务、管理任务状态、处理延时与超时，以及选择下一个运行任务。

本文针对当前工程：FreeRTOS V11.1.0、单核、抢占式调度、开启时间片和动态内存分配。

源码：[tasks.c](Z:/FreeRTOS-Kernel/src/tasks.c)、[task.h](Z:/FreeRTOS-Kernel/include/task.h)。

## 一、TCB 和任务状态

TCB 保存任务的全部内核状态。下面只列最重要的字段：

```c
typedef struct tskTaskControlBlock
{
    volatile StackType_t * pxTopOfStack; /* 当前栈顶，必须是首成员。 */
    ListItem_t xStateListItem;           /* 就绪、延时、挂起等状态节点。 */
    ListItem_t xEventListItem;           /* 队列、信号量等事件等待节点。 */
    UBaseType_t uxPriority;              /* 数值越大，优先级越高。 */
    StackType_t * pxStack;               /* 任务栈起始地址。 */
    char pcTaskName[ configMAX_TASK_NAME_LEN ];
    /* 其余字段由配置决定。 */
} TCB_t; /* 精简示意，不是完整源码。 */
```

`TaskHandle_t` 本质上是 TCB 指针。一个 TCB 需要两个链表节点，因为任务等待事件并带超时时，可以同时存在于两个链表：

```text
xStateListItem → 延时链表
xEventListItem → 事件等待链表
```

任务状态主要由 `xStateListItem` 所在的链表表示。

| 链表 | 作用 |
|---|---|
| `pxReadyTasksLists[]` | 每个优先级一个就绪链表 |
| `pxDelayedTaskList` | 本轮 Tick 回绕前到期的任务 |
| `pxOverflowDelayedTaskList` | Tick 回绕后到期的任务 |
| `xPendingReadyList` | 调度器挂起期间等待进入 Ready 的任务 |
| `xSuspendedTaskList` | 被挂起或无限期等待的任务 |
| `xTasksWaitingTermination` | 等待空闲任务回收的已删除任务 |

## 二、任务创建和启动

```text
xTaskCreate()
    → 分配任务栈和 TCB
    → prvInitialiseNewTask()
        → 初始化 TCB 和链表节点
        → pxPortInitialiseStack() 构造初始栈帧
    → prvAddNewTaskToReadyList()
        → 必要时调用 prvInitialiseTaskLists()
        → 加入对应优先级的就绪链表
```

当前版本的 `xTaskCreate()` 直接分配栈和 TCB，没有 `prvCreateTask()`。当前配置关闭静态分配，第一遍可以略过 `xTaskCreateStatic()`。

启动过程：

```text
vTaskStartScheduler()
    → prvCreateIdleTasks()
    → xTimerCreateTimerTask()
    → 设置调度器状态
    → xPortStartScheduler()
        → 配置 SysTick、PendSV 和 SVC
        → 恢复第一个任务
```

空闲任务始终处于最低优先级，并负责回收自删除任务的内存。

## 三、任务选择和 PendSV

```text
调度请求
    → portYIELD() 设置 PendSV
    → xPortPendSVHandler() 保存当前任务现场
    → vTaskSwitchContext() 选择最高优先级任务
    → 更新 pxCurrentTCB
    → PendSV 恢复新任务现场
```

- `vTaskSwitchContext()` 只选择任务，不产生 PendSV。
- PendSV 负责保存和恢复寄存器及 PSP。
- 同一优先级通过就绪链表的 `pxIndex` 轮转。
- `xYieldPendings[0]` 是待切换软件标志；真正触发 PendSV的是 `portYIELD()`。

## 四、任务延时和 Tick 唤醒

任务进入延时状态：

```text
vTaskDelay()
    → vTaskSuspendAll()
    → prvAddCurrentTaskToDelayedList()
        → 从就绪链表删除当前任务
        → 计算绝对唤醒 Tick
        → 加入当前或溢出延时链表
    → xTaskResumeAll()
    → 请求 PendSV
```

Tick 唤醒任务：

```text
xPortSysTickHandler()
    → xTaskIncrementTick()
        → xTickCount++
        → 检查延时链表头部
        → 将到期任务移入就绪链表
        → 判断是否需要抢占或时间片轮转
    → 需要切换时设置 PendSV
```

延时链表按绝对唤醒 Tick 升序排列，因此只需检查表头。`xNextTaskUnblockTime` 缓存最近唤醒时间，在到期前可以跳过链表检查。

若 `xTimeToWake < xTickCount`，说明计算发生回绕，任务进入溢出延时链表；`xTickCount` 回绕为 0 时交换两个延时链表。

`vTaskDelay()` 是相对延时；固定周期任务适合使用 `xTaskDelayUntil()`，避免任务执行时间累积到周期中。

## 五、事件阻塞和唤醒

任务等待队列、信号量等事件时：

```text
vTaskPlaceOnEventList()
    → xEventListItem 加入事件等待链表
    → xStateListItem 加入延时链表，负责超时
```

事件发生时，`xTaskRemoveFromEventList()` 删除两个节点，并将任务加入就绪链表。超时先发生时，`xTaskIncrementTick()` 也会删除两个节点，避免之后重复唤醒。

事件等待链表通常按反向优先级值排序，资源可用时优先唤醒最高优先级任务。

## 六、调度器挂起和恢复

`vTaskSuspendAll()` 禁止任务切换，但不会关闭中断。它可以嵌套，只有最外层 `xTaskResumeAll()` 才真正恢复调度。

挂起期间暂存三类工作：

| 名称 | 含义 |
|---|---|
| `xPendingReadyList` | 事件已经满足，但尚未正式进入 Ready 的任务 |
| `xPendedTicks` | 已经发生但尚未处理的 Tick 数 |
| `xYieldPendings[0]` | 已经需要但尚未执行的任务切换 |

恢复顺序：

```text
处理 xPendingReadyList
→ 重新计算 xNextTaskUnblockTime
→ 逐个补处理 xPendedTicks
→ 检查 xYieldPendings[0]
→ 必要时请求 PendSV
```

`xPendingReadyList` 使用 `xEventListItem`，因为此时 `xStateListItem` 通常仍在延时链表或挂起链表中。

如果挂起期间事件和超时都已满足，恢复时 Pending 事件先处理，因此事件可能胜过尚未补处理的超时。这不会造成重复挂载，但说明 RTOS 阻塞超时不是严格的硬件截止时间。

调度器挂起应尽量短，期间不能调用可能阻塞当前任务的 API。

## 七、删除任务

当前任务删除自己时，不能立即释放正在使用的栈：

```text
vTaskDelete(NULL)
    → 从调度链表删除当前任务
    → 加入 xTasksWaitingTermination
    → 切换到其他任务
    → 空闲任务调用 prvCheckTasksWaitingTermination()
    → 释放任务栈和 TCB
```

## 八、推荐复读顺序

```text
TCB_t 和全局链表
→ xTaskCreate()
→ prvInitialiseNewTask()
→ prvAddNewTaskToReadyList()
→ vTaskStartScheduler()
→ vTaskSwitchContext()
→ xTaskIncrementTick()
→ vTaskDelay()
→ prvAddCurrentTaskToDelayedList()
→ vTaskPlaceOnEventList()
→ xTaskRemoveFromEventList()
→ vTaskSuspendAll() / xTaskResumeAll()
→ vTaskDelete() / prvIdleTask()
```

第一遍可以跳过 SMP、MPU、Trace、运行时间统计、TLS、任务标签和静态分配分支。

## 九、值得记录的设计

- 链表节点的移动直接表示任务状态迁移。
- TCB 的两个节点同时表达事件等待和超时等待。
- 两个延时链表通过交换指针处理 Tick 回绕。
- `xNextTaskUnblockTime` 减少 Tick 中断中的链表访问。
- Pending 机制让调度器挂起期间的事件、Tick 和切换请求能够延后处理。
- 自删除任务由空闲任务安全回收资源。
