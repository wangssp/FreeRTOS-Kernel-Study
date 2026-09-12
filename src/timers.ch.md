# FreeRTOS 软件定时器阅读笔记——timers.c

## 代码阅读顺序

1. `Timer_t`、`DaemonTaskMessage_t`：理解定时器对象和命令消息。
2. `prvCheckForValidListAndQueue()`：查看两条活动链表和命令队列的创建。
3. `xTimerCreate()`、`prvInitialiseNewTimer()`：理解“创建不等于启动”。
4. `xTimerGenericCommandFromTask()`、`xTimerGenericCommandFromISR()`：理解 API 如何投递命令。
5. `prvTimerTask()`：掌握定时器守护任务主循环。
6. `prvProcessTimerOrBlockTask()`：看等待最近到期时间和执行回调。
7. `prvProcessReceivedCommands()`、`prvInsertTimerInActiveList()`：看命令如何改变定时器状态。
8. `prvSampleTimeNow()`、`prvSwitchTimerLists()`：最后理解 Tick 溢出。

源码：[timers.c](Z:/FreeRTOS-Kernel/src/timers.c)、[timers.h](Z:/FreeRTOS-Kernel/include/timers.h)。

## 一、核心结构

`Timer_t` 的主要成员：

| 成员 | 作用 |
|---|---|
| `xTimerListItem` | 挂入活动定时器链表，值为到期 Tick |
| `xTimerPeriodInTicks` | 定时周期 |
| `pvTimerID` | 用户自定义标识 |
| `pxCallbackFunction` | 到期回调 |
| `ucStatus` | 激活、自动重载、静态分配等状态 |

`DaemonTaskMessage_t` 表示发给定时器守护任务的命令，包含命令类型、目标定时器和可选时间值。
```c
typedef struct tmrTimerControl
{
    const char * pcTimerName;
    ListItem_t xTimerListItem;
    TickType_t xTimerPeriodInTicks;
    void * pvTimerID;
    portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction;
    uint8_t ucStatus;
} xTIMER;
```
## 二、整体运行模型

```text
任务或 ISR 调用定时器 API
        -> 命令写入 xTimerQueue
        -> 定时器守护任务取出命令
        -> 增删或调整活动定时器链表
        -> 到期后在守护任务上下文执行回调
```

启动、停止、复位、修改周期等 API 通常不是直接修改定时器链表，而是向 `xTimerQueue` 发送命令。

这样可以让活动链表只由守护任务管理，减少并发修改。API 返回成功只表示命令成功入队，不表示命令已经执行。

## 三、创建与启动

`xTimerCreate()` 分配并初始化 `Timer_t`，但不会自动启动定时器。

初始化内容包括：

- 名称、周期、ID、回调。
- 自动重载标志。
- 链表节点及其 owner。

之后调用 `xTimerStart()`、`xTimerReset()` 等宏，最终进入通用命令发送函数。

## 四、守护任务主循环

`prvTimerTask()` 重复执行：

1. 取得当前链表中最近的到期时间。
2. 若已到期，移除定时器并执行回调。
3. 若未到期，就阻塞在命令队列上，最长等待到该定时器到期。
4. 醒来后处理队列中的命令。

因此守护任务既会被定时器到期唤醒，也会被新的控制命令唤醒。

回调在守护任务中串行执行，不能长时间阻塞，否则其他定时器命令和回调都会延迟。

## 五、一次性与自动重载

- 一次性定时器到期后变为非活动状态。
- 自动重载定时器到期后，根据上一次理论到期时间计算下一次到期时间并重新插入链表。

按理论到期时间推进可以减少回调执行延迟带来的长期周期漂移。如果处理时已经跨过多个周期，内核会补处理相应到期过程。

## 六、两条活动链表

定时器按照绝对到期 Tick 有序排列：

- `pxCurrentTimerList`：当前 Tick 周期内到期。
- `pxOverflowTimerList`：Tick 溢出后到期。

检测到 Tick 回绕时，`prvSwitchTimerLists()` 处理旧链表中剩余项目，再交换两条链表指针。

这个设计与 `tasks.c` 的延时任务链表相同，避免直接用可能回绕的 Tick 做复杂比较。

## 七、ISR 接口

ISR 版本只负责把命令写入定时器队列：

- 不能等待队列空间。
- 通过 `pxHigherPriorityTaskWoken` 返回是否唤醒了更高优先级的守护任务。
- 真正的链表操作和回调仍在守护任务中完成。

## 八、值得记录的设计

- 用“命令队列 + 单一守护任务”串行化定时器状态修改。
- 用有序链表直接找到最近到期的定时器。
- 用双链表处理 Tick 回绕，复用了任务延时管理的思路。
- 创建对象与启动计时分离，定时器的控制操作统一走命令通道。
