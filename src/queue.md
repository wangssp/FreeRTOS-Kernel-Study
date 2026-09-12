# FreeRTOS 队列族阅读笔记——queue.c

## 代码阅读顺序

1. `Queue_t`、`QueuePointers_t`、`SemaphoreData_t`：先看统一对象的结构。
```c
typedef struct QueueDefinition {
    int8_t * pcHead;               // 存储区起始
    int8_t * pcWriteTo;            // 下一个写入位置
    union {
        QueuePointers_t xQueue;    // 队列专用
        SemaphoreData_t xSemaphore;// 信号量专用
    } u;
    List_t xTasksWaitingToSend;    // 等待发送的任务
    List_t xTasksWaitingToReceive; // 等待接收的任务
    volatile UBaseType_t uxMessagesWaiting; // 当前消息数
    UBaseType_t uxLength;          // 队列长度（项数）
    UBaseType_t uxItemSize;        // 每项大小
    volatile int8_t cRxLock;       // 接收锁计数
    volatile int8_t cTxLock;       // 发送锁计数
    #if ( configUSE_QUEUE_SETS == 1 )
        struct QueueDefinition * pxQueueSetContainer;
    #endif
} xQUEUE;

typedef struct QueuePointers
{
    int8_t * pcTail;
    int8_t * pcReadFrom;
} QueuePointers_t;

typedef struct SemaphoreData
{
    TaskHandle_t xMutexHolder;
    UBaseType_t uxRecursiveCallCount;
} SemaphoreData_t;
```
2. `xQueueGenericCreate()`、`prvInitialiseNewQueue()`、`xQueueGenericReset()`：理解队列初始化。
```c
xQueueGenericCreate(): 申请结构体加消息队列空间
    -> prvInitialiseNewQueue(): 初始化结构体队列成员
        -> xQueueGenericReset(): 重置队列、初始化读写链表
```
3. `xQueueGenericSend()`、`prvCopyDataToQueue()`：阅读任务发送主线。
```c
xQueueGenericSend(): 发送数据
    -> prvCopyDataToQueue(): 复制数据
//留的问题
//1.在prvCopyDataToQueue中，读指针指向哪里
```
4. `xQueueReceive()`、`prvCopyDataFromQueue()`：阅读任务接收主线。
```c
xQueueReceive(): 接收数据
    -> prvCopyDataFromQueue(): 复制数据
//问题1. 上来先移位    pxQueue->u.xQueue.pcReadFrom += pxQueue->uxItemSize;
```
5. `xQueueGenericSendFromISR()`、`xQueueReceiveFromISR()`、`portYIELD_FROM_ISR()`：比较 ISR 版本。
```c
xQueueGenericSendFromISR(): 发送数据
    -> prvCopyDataToQueue(): 复制数据
xQueueReceiveFromISR(): 接收数据
    -> prvCopyDataFromQueue(): 复制数据
```
``` text
和非ISR版本的区别：
在任务中消息收发阶段会挂起所有任务，同时会prvLockQueue()，将队列锁定。
队列锁定期间，ISR 只修改队列数据并记录收发次数；(?):如果不上锁，中断和任务中将同时对队列进行操作，导致数据不一致。
任务状态的实际转换由锁定队列的任务在解锁、恢复调度器时完成。
```
6. `prvUnlockQueue()`：最后理解调度器挂起时的延迟唤醒。
``` text
在任务中消息收发阶段会挂起所有任务，同时会prvLockQueue()，将队列锁定。
队列锁定期间，ISR 只修改队列数据并记录收发次数；(?):如果不上锁，中断和任务中将同时对队列进行操作，导致数据不一致。
任务状态的实际转换由锁定队列的任务在解锁、恢复调度器时完成。
```
7. `xQueueSemaphoreTake()`：理解信号量和互斥量如何复用队列。
```text
信号量:二值-binary/计数-counting/递归-recursive-mutex/互斥-mutex    
```
``` c
#define queueQUEUE_TYPE_BASE                  ( ( uint8_t ) 0U )
#define queueQUEUE_TYPE_SET                   ( ( uint8_t ) 0U )
#define queueQUEUE_TYPE_MUTEX                 ( ( uint8_t ) 1U )
#define queueQUEUE_TYPE_COUNTING_SEMAPHORE    ( ( uint8_t ) 2U )
#define queueQUEUE_TYPE_BINARY_SEMAPHORE      ( ( uint8_t ) 3U )
#define queueQUEUE_TYPE_RECURSIVE_MUTEX       ( ( uint8_t ) 4U )
```
8. `xQueueCreateMutex()`：创建互斥量队列。
```c
xQueueCreateMutex()
    -> xQueueGenericCreate()
    -> prvInitialiseMutex()
```

源码：[queue.c](FreeRTOS-Kernel/src/queue.c)、[queue.h](FreeRTOS-Kernel/include/queue.h)、[semphr.h](FreeRTOS-Kernel/include/semphr.h)。
writeTo
pcHead
  ↓
┌──────┬──────┬──────┬──────┐
│ 项目0 │ 项目1 │ 项目2 │ 项目3 │
└──────┴──────┴──────┴──────┘
                              ↑
                            pcTail
                            pcReadFrom
pcWriteTo：下一次从队尾发送时的写入位置。
pcReadFrom：上一次读取的位置。
指针到达结尾后回到 pcHead，形成环形缓冲区。

## 一、一个结构实现多种对象

`Queue_t` 同时支撑：

- 数据队列
- 二值信号量、计数信号量
- 普通互斥量、递归互斥量
- 队列集

关键成员：
```c
typedef struct QueueDefinition {
    int8_t * pcHead;               // 存储区起始
    int8_t * pcWriteTo;            // 下一个写入位置
    union {
        QueuePointers_t xQueue;    // 队列专用
        SemaphoreData_t xSemaphore;// 信号量专用
    } u;
    List_t xTasksWaitingToSend;    // 等待发送的任务
    List_t xTasksWaitingToReceive; // 等待接收的任务
    volatile UBaseType_t uxMessagesWaiting; // 当前消息数
    UBaseType_t uxLength;          // 队列长度（项数）
    UBaseType_t uxItemSize;        // 每项大小
    volatile int8_t cRxLock;       // 接收锁计数
    volatile int8_t cTxLock;       // 发送锁计数
} xQUEUE;
```


## 二、发送主线

`xQueueGenericSend()` 的流程：

1. 队列有空间时，在临界区内复制数据并更新计数。
2. 若接收等待链表非空，解除最高优先级接收任务的阻塞。
3. 被唤醒任务优先级更高时，请求调度。
4. 队列已满且允许等待时，将当前任务同时加入事件等待链表和延时链表。
5. 被唤醒后重新检查条件，因为资源可能已被其他任务先取得。

发送位置由参数决定：写到队尾、写到队首，或覆盖单元素队列。

## 三、接收主线

`xQueueReceive()` 与发送路径对称：

1. 有数据时复制出队并减少计数。
2. 若有任务等待发送，唤醒其中最高优先级任务。
3. 队列为空且允许等待时，把当前任务加入接收等待链表和延时链表。
4. 恢复运行后重新检查队列。

`xQueuePeek()` 只复制数据，不移动读指针，也不减少项目数。

## 四、信号量和互斥量

信号量可以理解为“项目大小为 0 的队列”：

- `give` 增加 `uxMessagesWaiting`。
- `take` 减少 `uxMessagesWaiting`。
- 等待与唤醒仍使用队列的两条事件链表。

互斥量在此基础上增加：

- `xMutexHolder`：记录持有任务。
- 优先级继承：高优先级任务等待时，临时提高持有者优先级。
- 释放时执行优先级反继承。
- 递归互斥量还使用 `uxRecursiveCallCount`。

互斥量有所有权，不能把它当成普通二值信号量随意跨任务释放，也不应在 ISR 中使用。

## 五、ISR 版本

`FromISR` 接口有这些特点：

- 不能阻塞，没有等待 Tick 参数。
- 临界区使用 ISR 专用中断屏蔽宏。
- 通过 `pxHigherPriorityTaskWoken` 告知调用者是否需要在退出中断时切换任务。
- 数据操作完成后不一定立即修改事件链表；队列被锁定时只累计锁计数。

## 六、队列锁与延迟唤醒

任务准备阻塞时，会先挂起调度器，再短暂锁定队列。锁定并不阻止 ISR 收发数据，只阻止 ISR 此时操作任务等待链表。

ISR 在锁定期间只增加 `cRxLock` 或 `cTxLock`。队列解锁时，`prvUnlockQueue()` 根据累计次数统一唤醒相应任务。

这与 `xPendingReadyList` 的思想相同：先记录事实，等处于安全上下文时再完成调度状态修改。

## 七、常见判断

- 队列满：`uxMessagesWaiting == uxLength`。
- 队列空：`uxMessagesWaiting == 0`。
- 阻塞等待必须放在循环中，醒来不代表资源仍然可用。
- 同一等待链表中优先唤醒高优先级任务；同优先级再按进入顺序处理。

## 八、值得记录的设计

- 用一个 `Queue_t` 统一数据队列、信号量和互斥量，复用阻塞与唤醒机制。
- 普通数据使用环形缓冲区，任务等待关系使用内核链表，两者职责分离。
- 队列锁不禁止 ISR 操作数据，只把任务唤醒推迟到安全时机。
- 发送和接收路径近似镜像，适合对照阅读。
