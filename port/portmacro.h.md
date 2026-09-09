# FreeRTOS 移植层阅读笔记——portmacro.h（Cortex-M3）

`portmacro.h` 是 FreeRTOS 移植层（Porting Layer）的核心头文件，它充当着操作系统内核与底层硬件（特别是 Cortex-M 内核）之间的“翻译官”。该文件主要包含**基础数据类型重定义**、**架构特性配置**、**任务切换机制**、**临界区保护**以及**调度优化算法**，确保 FreeRTOS 能在特定的编译器和硬件平台上高效、稳定地运行。

阅读范围：本仓库标注为 FreeRTOS Kernel V11.1.0，本文针对当前 Cortex-M3 单核移植；其他 Cortex-M 移植不一定采用相同的中断屏蔽、栈帧和编译器写法。以下代码块有注明“简化”或“示意”的，用于说明逻辑，不是逐字源码。

复读入口：[portmacro.h](Z:/FreeRTOS-Kernel/port/portmacro.h) → [port.c](Z:/FreeRTOS-Kernel/port/port.c) → [tasks.c](Z:/FreeRTOS-Kernel/src/tasks.c)。应用通常包含 `FreeRTOS.h` 和组件头文件，由内核头文件包含移植层；普通应用开发者使用现成的 port 接口，移植者负责为新平台提供实现。

当前配置：Tick 为 32 位，`configTICK_RATE_HZ = 1000`，任务优先级为 0～4，启用位图选优，关闭 Tickless。配置来源：[FreeRTOSConfig.h](Z:/FreeRTOS-Kernel/config/FreeRTOSConfig.h)。


## 一、基础数据类型定义（Type definitions）

为了隔离平台差异，FreeRTOS 使用移植层定义的内核类型。这里 `uint32_t` 明确为 32 位；`long` 的宽度则依赖目标 ABI，在本 ARM 32 位目标中为 32 位，不能推广为所有 C 平台的保证。

```c
#define portCHAR          char
#define portFLOAT         float
#define portDOUBLE        double
#define portLONG          long
#define portSHORT         short
#define portSTACK_TYPE    uint32_t    // 栈单元宽度为32位
#define portBASE_TYPE     long

typedef portSTACK_TYPE   StackType_t; // 栈类型，用于任务栈空间分配
typedef long             BaseType_t;  // 有符号基础类型，通常用于返回值（如 pdPASS）
typedef unsigned long    UBaseType_t; // 无符号基础类型，用于计数、标志位等
```

### TickType_t（系统节拍计数类型）

`TickType_t` 用于定义系统时钟节拍计数变量（如 `xTickCount` 和延时参数）。其位宽由 `configTICK_TYPE_WIDTH_IN_BITS` 控制，权衡存储开销与计时范围，与 CPU 位宽并非一一对应。本移植只支持 16 位或 32 位 Tick；其他配置会触发 `#error`。Tick 是时间基准，时间片轮转只是使用 Tick 的功能之一。

```c
#if ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS )
    typedef uint16_t     TickType_t;
    #define portMAX_DELAY              ( TickType_t ) 0xffff
#elif ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS )
    typedef uint32_t     TickType_t;
    #define portMAX_DELAY              ( TickType_t ) 0xffffffffUL
    /* 32位架构下读取32位tick值是原子操作，无需临界区保护 */
    #define portTICK_TYPE_IS_ATOMIC    1
#endif
```

> **原子性说明**：`portTICK_TYPE_IS_ATOMIC = 1` 表示此移植可以原子读取 Tick 值，所以读取 Tick 的专用临界区宏可展开为空。它不保证 `xTickCount++` 等“读取—计算—写回”操作原子，也不保证读取 Tick 后执行的一系列操作整体原子。参见 `FreeRTOS.h` 的 `portTICK_TYPE_ENTER_CRITICAL` 与 `tasks.c` 的 `xTaskGetTickCount()`。

`portMAX_DELAY` 首先是 Tick 类型的最大值。对于支持无限等待的 API，只有满足 `INCLUDE_vTaskSuspend == 1` 等内部条件时，传入它才表示无限等待；不能认为所有延时参数都把它解释为“永远等待”，例如 `vTaskDelay(portMAX_DELAY)` 仍然是有限延时。可追踪 `prvAddCurrentTaskToDelayedList()` 的 `xCanBlockIndefinitely` 参数。

当前 1 kHz、32 位 Tick 约每 49.71 天回绕一次。回绕由内核处理，不等于系统只能运行这么久。


## 二、架构特定参数（Architecture specifics）

这些宏定义了该硬件平台下 RTOS 运行的基础行为特征。

```c
#define portSTACK_GROWTH      ( -1 )     // 栈增长方向：-1 表示向下增长（高地址→低地址）
#define portTICK_PERIOD_MS    ( ( TickType_t ) 1000 / configTICK_RATE_HZ ) // 系统节拍周期（毫秒）
#define portBYTE_ALIGNMENT    8          // 栈与关键数据结构按 8 字节对齐

#ifdef __CC_ARM
    #define portDONT_DISCARD  __attribute__((used)) // 要求编译器保留定义
#else
    #define portDONT_DISCARD  __attribute__( ( used ) )
#endif
```

- **portSTACK_GROWTH**：Cortex-M 架构的栈指针（SP）向低地址移动，压栈时地址递减。
- **portBYTE_ALIGNMENT**：供堆分配和初始任务栈顶对齐使用，满足该移植及 ARM ABI 的对齐要求；不是“8 字节对齐就能保证 64 位原子操作”，也不是所有结构体都会自动被此宏对齐。
- **栈深度单位**：创建任务时栈深度按 `StackType_t` 单元计数。本移植每单元 4 字节，128 个单元就是 512 字节。
- **portTICK_PERIOD_MS**：使用整数除法，会截断；Tick 频率大于 1000 Hz 时甚至得到 0。毫秒转 Tick 通常使用 `pdMS_TO_TICKS(ms)`，但也要注意整数舍入、短延时可能转为 0，以及 Tick 边界对实际等待时间的影响。
- **portDONT_DISCARD**：`used` 主要要求编译器生成看似未引用的定义；链接阶段的节垃圾回收还涉及工具链和链接脚本，不能把它当成任何情况下都不会丢弃的保证。


## 三、调度器工具（Scheduler utilities）

### 任务切换触发 —— portYIELD()

本移植使用 **PendSV（可挂起的系统异常）** 完成上下文切换。`portYIELD()` 向 ICSR 寄存器第 28 位写 1，请求 PendSV；它本身不选择任务、不保存寄存器。

**当前源码问题**：仓库的 `portYIELD()` 把 `#ifdef __CC_ARM` 放进了反斜杠续行的宏体中，后续语句也没有正确续行。这不是合法的条件宏定义方式。下面给出只展示 GCC 风格分支的修正示意；若要兼容不同编译器，应把 `#ifdef/#else/#endif` 放在完整宏定义外面，分别定义宏。本次只修订笔记，源码问题仍需单独处理。

```c
#define portYIELD()                                         \
    do                                                     \
    {                                                      \
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;     \
        __asm volatile ( "dsb" ::: "memory" );             \
        __asm volatile ( "isb" ::: "memory" );             \
    } while( 0 )

#define portNVIC_INT_CTRL_REG     ( *( ( volatile uint32_t * ) 0xe000ed04 ) ) // ICSR 寄存器地址
#define portNVIC_PENDSVSET_BIT    ( 1UL << 28UL )                             // PendSV 挂起位
```

**内存屏障指令**：

- **DSB（数据同步屏障）**：确保所有之前的内存访问（如写操作）完成后才执行后续指令。
- **ISB（指令同步屏障）**：使后续指令在同步后的执行上下文中重新取指；它不是普通数据内存访问的完成屏障。

`volatile` 使寄存器访问不会被当作普通无副作用访问消除；汇编的 `"memory"` clobber 限制编译器对内存访问的重排；DSB/ISB 是 CPU 执行的指令。三者的作用层次不同。

### 中断服务例程中的切换

当在 ISR 中需要切换任务时，需调用以下宏，根据 `xSwitchRequired` 标志决定是否触发 PendSV。

```c
#define portEND_SWITCHING_ISR( xSwitchRequired ) \
    do                                           \
    {                                            \
        if( xSwitchRequired != pdFALSE )         \
        {                                        \
            traceISR_EXIT_TO_SCHEDULER();        \
            portYIELD();                         \
        }                                        \
        else                                     \
        {                                        \
            traceISR_EXIT();                     \
        }                                        \
    } while( 0 )
#define portYIELD_FROM_ISR( x )    portEND_SWITCHING_ISR( x )
```

这两个宏已经由移植层提供，应用用户通常只需调用，不需要另行实现。`traceISR_EXIT_TO_SCHEDULER()` 和 `traceISR_EXIT()` 是可选的跟踪插桩点，`FreeRTOS.h` 默认将它们定义为空，不需要用户补写函数。

典型用法（假定队列已经创建，外设中断优先级允许调用 FreeRTOS API）：

```c
void Peripheral_IRQHandler( void )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t ucData = ReadAndAcknowledgePeripheral(); /* 外设相关示意 */

    /* 队列满时发送会失败，实际应用应按需求处理返回值。 */
    xQueueSendFromISR( xQueue, &ucData, &xHigherPriorityTaskWoken );
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
```

`xQueueSendFromISR()` 的返回值表示发送是否成功，`xHigherPriorityTaskWoken` 则报告是否唤醒了比被中断任务优先级更高的任务，两者不能混用。该标志在每次 ISR 入口初始化为 `pdFALSE`，同一次 ISR 内多个 FromISR 调用可共享它，最后统一请求切换，中途不要重置。

执行链：FromISR API 把等待任务变为就绪 → 标志置真 → `portYIELD_FROM_ISR()` 挂起 PendSV → 当前 ISR 及其他优先级更高的异常处理完、屏蔽允许时 → PendSV 保存 R4～R11 和任务栈指针 → `vTaskSwitchContext()` 选择任务 → 恢复新任务的软件现场 → 异常返回恢复硬件保存的现场。

`portYIELD_FROM_ISR(pdFALSE)` 只表示本次不新增切换请求，不会清除已有的 PendSV 请求。即使请求了 PendSV，调度器挂起等情况下也可能暂不切换到另一个任务。因此跟踪宏表示切换请求路径，并不保证实际换了任务。

`do { ... } while (0)` 将多语句宏包装成一条语句，调用时可以自然加分号，并安全用于外层 `if/else`。循环体只执行一次。注意：多个 API 统一请求切换有利于减少重复操作和适配不同移植；本 Cortex-M3 上避免在外设 ISR 中途运行 PendSV，主要依靠的是异常优先级机制。


## 四、临界区管理（Critical section management）

本 Cortex-M3 移植的常规临界区通过 **BASEPRI（中断优先级屏蔽阈值）** 保护内核共享状态。优先级高于阈值的外设中断仍可响应，但不能调用 FreeRTOS API，包括 FromISR API。NMI、HardFault 是具有特殊优先级的异常，不能拿它们说明 BASEPRI 相比 PRIMASK 的区别，因为 PRIMASK 同样不屏蔽它们。

不要推广为 FreeRTOS 永远不用 PRIMASK：本仓库 `port.c` 的启动、Tickless 等路径也存在 `cpsid i` / `cpsie i`。

```c
extern void vPortEnterCritical( void ); // 嵌套临界区进入（计数+1）
extern void vPortExitCritical( void );  // 嵌套临界区退出（计数-1）

#define portSET_INTERRUPT_MASK_FROM_ISR()         ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )    vPortSetBASEPRI( x )
#define portDISABLE_INTERRUPTS()                  vPortRaiseBASEPRI()
#define portENABLE_INTERRUPTS()                   vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()                      vPortEnterCritical()
#define portEXIT_CRITICAL()                       vPortExitCritical()
```

### 关键实现函数解析

#### 1. vPortRaiseBASEPRI（屏蔽中断）

将 BASEPRI 设置为 `configMAX_SYSCALL_INTERRUPT_PRIORITY`，从而屏蔽所有优先级**数值大于或等于**该阈值的中断（注意：Cortex-M 中数值越大优先级越低）。

这里比较的是寄存器编码后的优先级，且需满足移植要求的优先级分组。假定实现 4 个优先级位，并均用于抢占优先级，当前配置的库层阈值为 5，写入 BASEPRI 的值是 `5 << (8 - 4) = 0x50`：

| 外设中断优先级数值 | BASEPRI = 0x50 时 | 是否允许调用 FreeRTOS FromISR API |
|---|---|---|
| 0～4 | 不受此阈值屏蔽 | 不允许 |
| 5～15 | 被屏蔽，等待解除后处理 | 允许 |

阈值不能配置为 0，因为 BASEPRI 为 0 表示不施加优先级屏蔽。注意任务优先级与 NVIC 中断优先级是两套体系：任务数值越大越优先，中断数值越小越优先。

下面仅展示本文件的 GCC 风格实现，避免混用不同编译器的内联汇编语法：

```c
portFORCE_INLINE static void vPortRaiseBASEPRI( void )
{
        uint32_t ulNewBASEPRI;
        __asm volatile ( "mov %0, %1; msr basepri, %0; isb; dsb;" 
                         : "=r" ( ulNewBASEPRI ) 
                         : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) 
                         : "memory" );
}
```

#### 2. ulPortRaiseBASEPRI（带返回值版本）

该函数返回**修改前的 BASEPRI 值**，用于在 ISR 中安全地暂存并恢复中断状态。

```c
portFORCE_INLINE static uint32_t ulPortRaiseBASEPRI( void )
{
    uint32_t ulOriginalBASEPRI, ulNewBASEPRI;
    __asm volatile ( "mrs %0, basepri; mov %1, %2; msr basepri, %1; isb; dsb;" 
                     : "=r" ( ulOriginalBASEPRI ), "=r" ( ulNewBASEPRI ) 
                     : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) 
                     : "memory" );
    return ulOriginalBASEPRI;
}
```

#### 3. vPortSetBASEPRI（恢复中断状态）

直接向 BASEPRI 写入指定值。写 0 只解除 BASEPRI 施加的屏蔽，不会清除 PRIMASK，也不会启用 NVIC 中原本禁用的中断。ISR 中应恢复原值，而不是总写 0。

```c
portFORCE_INLINE static void vPortSetBASEPRI( uint32_t ulNewMaskValue )
{
    __asm volatile ( "msr basepri, %0" ::"r" ( ulNewMaskValue ) : "memory" );
}
```

### 任务临界区和 ISR 临界区的区别

任务上下文的 `vPortEnterCritical()` 先设置 BASEPRI，再递增 `uxCriticalNesting`；`vPortExitCritical()` 递减计数，仅在计数变为 0 时清除 BASEPRI。当前移植的这个计数是 `port.c` 的静态变量，不是 TCB 中每任务各存一份。配对使用保证内层退出不会提前解除外层保护。

```text
进入外层：嵌套数 0 → 1，BASEPRI = 阈值
进入内层：嵌套数 1 → 2
退出内层：嵌套数 2 → 1，继续保护
退出外层：嵌套数 1 → 0，BASEPRI = 0
```

ISR 版本采用保存和恢复原屏蔽值的方式（逻辑示意）：

```c
UBaseType_t uxSavedMask = portSET_INTERRUPT_MASK_FROM_ISR();
/* 访问需要保护的共享状态。 */
portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedMask );
```

FromISR API 会在内部管理其需要的保护。普通应用需要自行保护数据时，优先使用相应的 `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` 或 ISR 版本接口，不要随意混搭低层宏。任务临界区不能用来执行可能阻塞的操作；ISR 不能调用任务版临界区。

`vTaskSuspendAll()` 仅挂起调度器，不等于屏蔽中断。裸用 `portDISABLE_INTERRUPTS()` / `portENABLE_INTERRUPTS()` 也不提供嵌套计数和原值恢复。另外，当前 `vPortRaiseBASEPRI()` 实际直接写固定阈值，并不是使用 `BASEPRI_MAX` 自动保留调用前更严格的阈值，不能把它当通用任意嵌套屏蔽工具。


## 五、任务函数辅助宏（Task function macros）

为了在示例代码中保持统一的原型风格，定义以下两个宏：

```c
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )    void vFunction( void * pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )          void vFunction( void * pvParameters )
```


## 六、Tickless 低功耗模式支持

当前配置 `configUSE_TICKLESS_IDLE = 0`，该低功耗路径未启用。启用后，空闲任务预测可空闲时间，通过该宏进入移植层：确认仍可睡眠 → 调整 SysTick 以减少周期性唤醒 → 执行睡眠等待 → 醒来后补偿经过的 Tick 并恢复周期节拍。

默认实现使用 WFI，但 WFI 不自动等于“深度睡眠”；具体睡眠深度还取决于芯片寄存器、时钟与应用低功耗钩子配置。`xExpectedIdleTime` 的单位是 Tick，可能被移植层计数器容量限制，也可能因其他中断提前醒来。

```c
#ifndef portSUPPRESS_TICKS_AND_SLEEP
    extern void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime );
    #define portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime )    vPortSuppressTicksAndSleep( xExpectedIdleTime )
#endif
```


## 七、架构优化——就绪任务快速查找

启用 `configUSE_PORT_OPTIMISED_TASK_SELECTION` 时，FreeRTOS 用位图记录哪些优先级存在就绪任务。`CLZ`（前导零计数）在常数时间内找出最高的就绪优先级，随后 `tasks.c` 从该优先级的就绪链表选择具体任务。O(1) 描述的是优先级查找，不代表整个 Tick 处理或所有调度相关逻辑都为 O(1)。

### 1. 前导零计算函数

```c
__attribute__( ( always_inline ) ) static inline uint8_t ucPortCountLeadingZeros( uint32_t ulBitmap )
{
    uint8_t ucReturn;
    __asm volatile ( "clz %0, %1" : "=r" ( ucReturn ) : "r" ( ulBitmap ) : "memory" );
    return ucReturn;
}
```

### 2. 配置检查与位图操作宏

该优化要求系统优先级数量不得超过 32 个（因为 `uxReadyPriorities` 变量为 32 位）。

```c
#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
    #define configUSE_PORT_OPTIMISED_TASK_SELECTION    1
#endif

#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 1 )

#if ( configMAX_PRIORITIES > 32 )
    #error configMAX_PRIORITIES must be <= 32 for optimized task selection.
#endif

#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities )    ( uxReadyPriorities ) |= ( 1UL << ( uxPriority ) )
#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities )     ( uxReadyPriorities ) &= ~( 1UL << ( uxPriority ) )
#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities )  uxTopPriority = ( 31UL - ( uint32_t ) ucPortCountLeadingZeros( ( uxReadyPriorities ) ) )
#endif
```

例如优先级 0、2、4 有就绪任务，位图是 `0x00000015`（低 5 位为 `10101`）。`CLZ` 得到 27，于是最高就绪优先级为 `31 - 27 = 4`。再通过 `listGET_OWNER_OF_NEXT_ENTRY()` 取出该链表中的下一个 TCB，实现同优先级轮转。

必须维持的不变量：某位为 1 当且仅当对应就绪链表非空；移除该链表最后一个任务时才能清位，不能每移除一个任务就清位。位图必须非零才能做有效的优先级索引，正常调度时空闲任务提供最低优先级的就绪候选。


## 八、运行时上下文检测

### xPortIsInsideInterrupt（中断内检测）

通过读取 **IPSR（中断程序状态寄存器）** 来判断当前代码是否运行在中断服务例程中。IPSR 值为 0 表示处于线程模式（任务或主程序），非 0 表示处于异常/中断中。

```c
portFORCE_INLINE static BaseType_t xPortIsInsideInterrupt( void )
{
    uint32_t ulCurrentInterrupt;
    __asm volatile ( "mrs %0, ipsr" : "=r" ( ulCurrentInterrupt )::"memory" );
    return ( ulCurrentInterrupt != 0 ) ? pdTRUE : pdFALSE;
}
```


## 九、辅助工具与内存屏障

### 内存屏障宏

当前文件两种编译器分支不完全相同：GCC 风格分支为空汇编加 `"memory"` clobber，只提供编译器屏障，不发出 CPU 屏障指令；`__CC_ARM` 分支显式执行 DSB，包含硬件层面的同步。不能统一描述成“只提供编译器屏障”，也不能以此推断共享数据访问变成了原子操作。

```c
#ifdef __CC_ARM
    #define portMEMORY_BARRIER()    __asm { dsb }
#else
    #define portMEMORY_BARRIER()    __asm volatile ( "" ::: "memory" ) // 编译器屏障
#endif
```

### 内联控制宏

`portINLINE` 表达内联意图；`portFORCE_INLINE` 结合工具链的 `always_inline` 属性提出更强的内联要求。这里只适用于支持相应语法的编译器，不能只靠一个 `__CC_ARM` 分支就认为整个移植已经兼容所有 ARM 编译器。

```c
#define portINLINE              __inline
#define portFORCE_INLINE        inline __attribute__( ( always_inline ) )
```

另一个容易误读的宏是 `portNOP()`：本文件把它定义为空，所以不会产生一条 NOP 机器指令，不能用来构造精确延时。文件首尾的 `extern "C"` 则在 C++ 编译时使用 C 链接约定。


## 十、调试辅助

当定义了 `configASSERT` 宏时，可启用中断优先级有效性验证功能，用于开发阶段捕获错误的优先级配置。

```c
#ifdef configASSERT
    void vPortValidateInterruptPriority( void );
    #define portASSERT_IF_INTERRUPT_PRIORITY_INVALID()    vPortValidateInterruptPriority()
#endif
```

对应 `port.c` 的 `vPortValidateInterruptPriority()` 主要检查两件事：调用 FromISR API 的外设中断优先级是否处于允许范围，以及 NVIC 优先级分组是否满足移植要求。它不会替用户修正优先级。`xPortIsInsideInterrupt()` 只识别异常上下文，返回真也不意味着当前 ISR 有资格调用内核 API。


## 十一、复读路线与值得摘录的设计

复读时按符号搜索比依赖固定行号更耐用：

| 本文入口 | 接下来追踪的实现 | 应当回答的问题 |
|---|---|---|
| `StackType_t`、`portSTACK_GROWTH` | `tasks.c: prvInitialiseNewTask`，`port.c: pxPortInitialiseStack` | 栈大小、对齐和初始异常帧如何形成？ |
| `portYIELD_FROM_ISR` | `queue.c: xQueueGenericSendFromISR`，`tasks.c: xTaskRemoveFromEventList` | 谁让任务就绪，谁决定需要切换？ |
| `portYIELD` | `port.c: xPortPendSVHandler`，`tasks.c: vTaskSwitchContext` | 谁保存现场，谁选择任务？ |
| 临界区宏 | `port.c: vPortEnterCritical / vPortExitCritical` | 嵌套数和 BASEPRI 如何配合？ |
| `portGET_HIGHEST_PRIORITY` | `tasks.c: taskSELECT_HIGHEST_PRIORITY_TASK` | 位图和就绪链表如何保持一致？ |
| `portSUPPRESS_TICKS_AND_SLEEP` | `port.c: vPortSuppressTicksAndSleep`，`tasks.c: vTaskStepTick` | 少产生 Tick 中断后如何补偿时间？ |

优秀代码摘录建议：

1. **请求与执行分离**：FromISR 操作报告唤醒结果，宏请求 PendSV，调度器选择 TCB，异常处理程序恢复现场，各层职责清晰。
2. **保存—恢复成对接口**：ISR 保护区返回旧 BASEPRI，退出时恢复，避免盲目写 0 破坏外层状态。
3. **位图与链表配合**：位图回答“哪个优先级”，链表回答“这个优先级中的哪个任务”。
4. **配置消除开销**：原子 Tick 读取对应的临界区、未配置的 trace 钩子可以在编译时消失。
5. **多语句宏的语法封装**：`do { ... } while (0)` 让宏可以像普通语句一样使用。

复读自测：为什么 32 位 Tick 可以原子读取但 `++` 不是原子操作？为什么临界区中仍可能进外设中断？为什么 `portYIELD_FROM_ISR(pdTRUE)` 不保证马上换任务？为什么 CLZ 位图不能为 0？这些问题能解释清楚，就掌握了本头文件的主要运行逻辑。
