# FreeRTOS Cortex-M3 移植层阅读笔记——port.c

## 代码阅读顺序

1. `pxPortInitialiseStack()`：构造任务首次运行时的初始栈帧。
2. `xPortStartScheduler()`：配置异常优先级和 Tick，启动第一个任务。
3. `prvPortStartFirstTask()`、`vPortSVCHandler()`：通过 SVC 恢复第一个任务现场。
4. `xPortPendSVHandler()`：保存旧任务现场、选择新任务、恢复新任务现场。
5. `xPortSysTickHandler()`：推进系统 Tick，并在需要时挂起 PendSV。
6. `vPortEnterCritical()`、`vPortExitCritical()`：理解临界区和 `BASEPRI`。
7. `vPortSetupTimerInterrupt()`、`vPortValidateInterruptPriority()`：最后看硬件配置与中断优先级检查。

源码：[port.c](Z:/FreeRTOS-Kernel/port/port.c)、[portmacro.h](Z:/FreeRTOS-Kernel/port/portmacro.h)。

## 一、移植层负责什么

`tasks.c` 决定“下一个运行谁”，`port.c` 负责“怎样在当前 CPU 上真正切换过去”。

主要工作：

- 初始化任务栈。
- 启动第一个任务。
- 保存和恢复寄存器现场。
- 提供 Tick 中断、临界区和让出 CPU 的底层实现。
- 检查可调用 `FromISR` API 的中断优先级是否合法。

## 二、任务栈初始化

`pxPortInitialiseStack()` 在任务还没运行前，人工构造一份异常返回所需的栈帧：

- `PC` 指向任务入口函数。
- `R0` 保存任务参数。
- `xPSR` 设置 Thumb 状态。
- `LR` 指向任务意外返回后的错误处理函数。
- 为 `R4-R11` 和硬件自动恢复的寄存器预留位置。

因此第一次恢复任务，与恢复一个被中断过的任务使用相同流程。

## 三、启动第一个任务

主线如下：

```text
vTaskStartScheduler()
    -> xPortStartScheduler()
        -> 配置 PendSV、SysTick、SVC 优先级
        -> vPortSetupTimerInterrupt()
        -> prvPortStartFirstTask()
            -> 触发 SVC
                -> vPortSVCHandler()
                    -> 从 pxCurrentTCB 指向的栈恢复现场
```

普通任务切换由 PendSV 完成，但启动时没有“旧任务现场”可保存，所以单独使用 SVC 恢复第一个任务。

## 四、PendSV 上下文切换

`xPortPendSVHandler()` 的核心过程：

1. 从 `PSP` 取得当前任务栈顶。
2. 将 `R4-R11` 压入当前任务栈。
3. 把新栈顶保存到当前 TCB 的第一个成员 `pxTopOfStack`。
4. 用 `BASEPRI` 保护调度器数据，调用 `vTaskSwitchContext()` 更新 `pxCurrentTCB`。
5. 从新任务的栈恢复 `R4-R11`，更新 `PSP`。
6. 异常返回时，由硬件自动恢复其余寄存器。

Cortex-M 异常入口会自动保存 `R0-R3、R12、LR、PC、xPSR`，移植层只需手动保存 `R4-R11`。

## 五、Tick 如何触发切换

`xPortSysTickHandler()` 调用 `xTaskIncrementTick()`：

- 返回 `pdFALSE`：没有必要立即切换任务。
- 返回非零：向 NVIC 写入 PendSV 挂起位。

真正产生 PendSV 请求的是对硬件寄存器设置 `PENDSVSET`，不是 `xYieldPendings[]`。后者只是调度器内部的“需要切换”状态。

PendSV 被设为最低优先级，因此会等当前中断处理完，再安全地执行上下文切换。

## 六、临界区和中断优先级

任务临界区支持嵌套：

- `vPortEnterCritical()` 提高中断屏蔽级别并增加嵌套计数。
- `vPortExitCritical()` 减少计数，归零时解除屏蔽。

Cortex-M3 端口使用 `BASEPRI`，只屏蔽能够调用 FreeRTOS API 的一部分中断。更高紧急程度的中断仍可运行，但不能调用 FreeRTOS API。

ISR 中只能调用以 `FromISR` 结尾的接口，并且其中断优先级必须满足 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 的限制。

## 七、值得记录的设计

- TCB 的第一个成员固定为栈顶指针，使汇编能直接保存和恢复任务栈。
- 人工构造初始栈帧，让任务首次启动复用异常返回机制。
- 调度决策留在通用内核，寄存器操作留在移植层，硬件相关代码被集中隔离。
- 用最低优先级 PendSV 延迟切换，避免在高优先级业务中断中直接更换任务现场。
