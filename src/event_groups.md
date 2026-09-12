# FreeRTOS 事件组阅读笔记——event_groups.c

## 代码阅读顺序

1. `EventGroup_t`：先看事件位和等待任务链表。
```c
typedef struct EventGroupDef_t
{
    EventBits_t uxEventBits;
    List_t xTasksWaitingForBits;
} EventGroup_t;
```
2. `xEventGroupCreate()` `vEventGroupDelete()`：创建和删除事件组。
```c
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t xEventGroup);
```
2. `prvTestWaitCondition()`：理解等待任意位和等待全部位。
3. `xEventGroupWaitBits()`：阅读任务阻塞与返回过程。
4. `xEventGroupSetBits()`：阅读置位、匹配和批量唤醒。
5. `xEventGroupClearBits()`：理解清零语义。
6. `xEventGroupSync()`：理解“置位 + 等待”的同步点。
7. `xEventGroupSetBitsFromISR()` 相关路径：最后看为什么要借助定时器守护任务。

源码：[event_groups.c](FreeRTOS-Kernel/src/event_groups.c)、[event_groups.h](FreeRTOS-Kernel/include/event_groups.h)。

## 一、核心结构

`EventGroup_t` 很简单：

| 成员 | 作用 |
|---|---|
| `uxEventBits` | 当前事件位集合 |
| `xTasksWaitingForBits` | 等待事件条件成立的任务链表 |

一个事件位表示一个布尔事件。多个任务可以等待不同的位组合，置位一次也可能同时唤醒多个任务。

## 二、等待条件

`xEventGroupWaitBits()` 由三个条件参数决定行为：

- `uxBitsToWaitFor`：关心哪些位。
- `xWaitForAllBits`：等待全部位还是任意一位。
- `xClearOnExit`：条件成立后是否自动清除命中的等待位。

如果调用时条件已经满足，函数直接返回；否则任务进入 `xTasksWaitingForBits` 和延时链表，等待事件或超时。

任务事件链表节点的值不仅保存等待位，还编码“等待全部位”“退出时清除”等控制标志，避免为每个等待任务另外分配结构体。

## 三、置位和唤醒

`xEventGroupSetBits()` 的主要流程：

1. 将新事件位并入 `uxEventBits`。
2. 遍历所有等待任务。
3. 判断每个任务的任意/全部等待条件。
4. 将满足条件的任务从事件链表移除，使其进入就绪态。
5. 汇总需要自动清除的位，遍历结束后统一清除。

它可能唤醒多个任务，所以执行时间与等待任务数量有关。

返回值是函数处理结束时的事件位，不一定等于刚置位后的瞬间值，因为某些任务可能设置了自动清除。

## 四、超时与返回值

任务可能因事件成立或等待超时而恢复。事件组使用任务事件链表项中的控制位标记唤醒原因。

若是超时，任务恢复后会再次检查当前事件位，因为在超时处理与任务真正运行之间，事件仍可能被置位。

## 五、ISR 为什么延后置位

事件组置位可能遍历等待链表并唤醒数量不确定的任务，不适合要求执行时间可控的 ISR。

因此 `xEventGroupSetBitsFromISR()` 把操作包装成延后函数，投递到软件定时器命令队列，由定时器守护任务调用 `vEventGroupSetBitsCallback()` 完成真正置位。

所以使用 ISR 置位事件组时，需要启用软件定时器，并保证定时器命令队列有足够空间。

## 六、同步点

`xEventGroupSync()` 先置位表示“当前任务已到达”，再等待指定任务对应的所有位。它适合实现多个任务的阶段同步。

置位和进入等待在调度器挂起保护下衔接，避免在两步之间错过其他任务的事件。

## 七、值得记录的设计

- 一个整数同时表示多个事件，适合状态组合和多任务广播。
- 把等待条件编码到任务现有的事件链表节点中，不额外分配等待对象。
- 先遍历并收集待清除位，再统一清除，保证同一次置位可唤醒所有符合条件的任务。
- 将不确定耗时的 ISR 操作转移到守护任务，换取中断路径的确定性。
