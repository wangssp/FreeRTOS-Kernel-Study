# FreeRTOS 链表阅读笔记——list.h 与 list.c

FreeRTOS 使用的是**带哨兵节点的循环双向侵入式链表**。

- 循环双向链表：节点可以向前、向后遍历。
- 哨兵节点：`xListEnd` 统一处理空链表、头节点和尾节点边界。
- 侵入式链表：`ListItem_t` 直接嵌入 TCB、软件定时器等对象，不单独分配节点。
- 有序插入：`vListInsert()` 按 `xItemValue` 升序插入。

源码：[list.h](Z:/FreeRTOS-Kernel/include/list.h)、[list.c](Z:/FreeRTOS-Kernel/src/list.c)。

## 一、核心结构

### 1. ListItem_t：普通链表节点

```c
struct xLIST_ITEM
{
    TickType_t xItemValue;          /* 节点值，通常用于排序。 */
    struct xLIST_ITEM * pxNext;     /* 后继节点。 */
    struct xLIST_ITEM * pxPrevious; /* 前驱节点。 */
    void * pvOwner;                 /* 拥有该节点的对象，通常是 TCB。 */
    struct xLIST * pxContainer;     /* 该节点所在的链表。 */
};
typedef struct xLIST_ITEM ListItem_t;
```

- `xItemValue` 的含义由使用场景决定，例如任务唤醒时间、事件等待优先级或定时器到期时间。
- `pvOwner` 用于从节点找到其所属对象。
- `pxContainer` 用于从节点找到所在链表；节点未入链时为 `NULL`。

### 2. MiniListItem_t：精简节点

```c
struct xMINI_LIST_ITEM
{
    TickType_t xItemValue;
    struct xLIST_ITEM * pxNext;
    struct xLIST_ITEM * pxPrevious;
};
typedef struct xMINI_LIST_ITEM MiniListItem_t;
```

它只用于 `xListEnd` 哨兵，因此省略 `pvOwner` 和 `pxContainer`。若 `configUSE_MINI_LIST_ITEM == 0`，`MiniListItem_t` 与 `ListItem_t` 相同。

### 3. List_t：链表控制结构

```c
typedef struct xLIST
{
    UBaseType_t uxNumberOfItems; /* 真实节点数，不包含哨兵。 */
    ListItem_t * pxIndex;        /* 上次遍历返回的节点。 */
    MiniListItem_t xListEnd;     /* 哨兵/结束标记。 */
} List_t;
```

初始化后的空链表：

```text
                    ┌──────────────┐
                    ▼              │
pxIndex ───────> [ xListEnd ] ─────┘
                    ▲              │
                    └──────────────┘

uxNumberOfItems = 0
xListEnd.xItemValue = portMAX_DELAY
```

加入节点后：

```text
xListEnd ⇄ [10] ⇄ [20] ⇄ [30] ⇄ xListEnd
```

`xListEnd.pxNext` 指向头节点，`xListEnd.pxPrevious` 指向尾节点。`xListEnd` 不是业务节点，不计入节点数量。

## 二、list.c 的五个函数

| 函数 | 作用 | 关键点 |
|---|---|---|
| `vListInitialise()` | 初始化链表 | 建立哨兵自环，清零节点数，初始化 `pxIndex` |
| `vListInitialiseItem()` | 初始化节点 | 将 `pxContainer` 设为 `NULL` |
| `vListInsert()` | 有序插入 | 按 `xItemValue` 升序，查找位置为 O(n) |
| `vListInsertEnd()` | 轮转插入 | 插入到 `pxIndex` 前面，使新节点在当前遍历周期中最后被选中 |
| `uxListRemove()` | 删除节点 | 利用双向指针和 `pxContainer` 实现 O(1) 删除 |

## 三、list.h 的核心宏

### 节点 owner 与节点值

- `listSET_LIST_ITEM_OWNER()`
- `listGET_LIST_ITEM_OWNER()`
- `listSET_LIST_ITEM_VALUE()`
- `listGET_LIST_ITEM_VALUE()`

### 头节点、后继节点和哨兵

- `listGET_ITEM_VALUE_OF_HEAD_ENTRY()`
- `listGET_HEAD_ENTRY()`
- `listGET_NEXT()`
- `listGET_END_MARKER()`
- `listGET_OWNER_OF_HEAD_ENTRY()`

### 链表状态和节点归属

- `listLIST_IS_EMPTY()`
- `listCURRENT_LIST_LENGTH()`
- `listIS_CONTAINED_WITHIN()`
- `listLIST_ITEM_CONTAINER()`
- `listLIST_IS_INITIALISED()`

### 遍历和内联操作

- `listGET_OWNER_OF_NEXT_ENTRY()`
- `listREMOVE_ITEM()`
- `listINSERT_END()`

`listGET_OWNER_OF_NEXT_ENTRY()` 使用 `pxIndex` 循环遍历，并自动跳过哨兵。调用前必须保证链表非空。

`listREMOVE_ITEM()` 和 `listINSERT_END()` 分别是删除、插入函数的内联宏版本，用于减少热点路径中的函数调用开销。

## 四、关键规则

1. `uxNumberOfItems` 只统计真实节点，不包含 `xListEnd`。
2. 空链表中，哨兵的前后指针和 `pxIndex` 都指向哨兵自身。
3. 已入链节点的 `pxContainer` 指向所在链表，未入链节点为 `NULL`。
4. 同一个 `ListItem_t` 不能同时加入两个链表。
5. `listGET_OWNER_OF_NEXT_ENTRY()` 和 `listGET_OWNER_OF_HEAD_ENTRY()` 只能在非空链表上使用。
6. 修改有序链表中节点的 `xItemValue` 前，应先删除节点，修改后重新插入。
7. 链表代码自身不提供并发保护，调用者负责在合适的临界区或调度器保护下操作。
8. 宏参数不要使用带自增、函数调用等副作用的表达式。

## 五、值得记录的设计

- **哨兵节点**：减少空链表、头节点和尾节点的特殊判断。
- **侵入式节点**：无需为链表节点额外分配内存。
- **双向关系**：`pvOwner` 找宿主，`pxContainer` 找链表，支持 O(1) 删除。
- **有序插入**：插入时排序，使读取最近到期节点只需访问表头。
- **轮转索引**：`pxIndex` 配合 `vListInsertEnd()` 实现公平的循环遍历。
