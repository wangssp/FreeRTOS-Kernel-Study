# FreeRTOS 流缓冲区与消息缓冲区阅读笔记——stream_buffer.c

## 代码阅读顺序

1. `StreamBuffer_t`：理解环形缓冲区、等待任务和触发级别。
2. `xStreamBufferGenericCreate()`、`prvInitialiseNewStreamBuffer()`：看两种缓冲区如何共用结构。
3. `xStreamBufferSend()`、`prvWriteMessageToBuffer()`：阅读发送与阻塞。
4. `prvWriteBytesToBuffer()`：理解环形区跨尾部写入。
5. `xStreamBufferReceive()`、`prvReadMessageFromBuffer()`：阅读接收与唤醒。
6. `prvReadBytesFromBuffer()`：理解环形区跨尾部读取。
7. `xStreamBufferSendFromISR()`、`xStreamBufferReceiveFromISR()`：比较 ISR 路径。
8. 最后对照 [message_buffer.h](Z:/FreeRTOS-Kernel/include/message_buffer.h) 中的包装宏。

源码：[stream_buffer.c](Z:/FreeRTOS-Kernel/src/stream_buffer.c)、[stream_buffer.h](Z:/FreeRTOS-Kernel/include/stream_buffer.h)。

## 一、核心结构

| 成员 | 作用 |
|---|---|
| `xHead` | 下一写入位置 |
| `xTail` | 下一读取位置 |
| `xLength` | 环形存储区长度 |
| `xTriggerLevelBytes` | 达到多少字节时唤醒接收任务 |
| `xTaskWaitingToReceive` | 唯一的等待接收任务 |
| `xTaskWaitingToSend` | 唯一的等待发送任务 |
| `pucBuffer` | 字节存储区 |
| `ucFlags` | 消息模式、静态分配等标志 |

内部会保留一个空字节，用 `head == tail` 表示空，从而区分满和空。

## 二、流缓冲区与消息缓冲区

两者使用同一个 `StreamBuffer_t` 和同一份实现：

- 流缓冲区保存连续字节流，发送和接收可以只完成一部分。
- 消息缓冲区在每条消息前保存长度字段，发送和接收以整条消息为单位。

`message_buffer.h` 主要是把消息缓冲区 API 映射到流缓冲区通用接口，并传入消息模式标志。

## 三、发送流程

`xStreamBufferSend()`：

1. 计算所需空间；消息模式还要加长度字段。
2. 空间不足且允许等待时，用任务通知进入阻塞。
3. 醒来后重新计算空间。
4. 将数据写入环形缓冲区。
5. 数据量达到触发级别时，通知等待接收的任务。

流模式会尽量写入可用空间；消息模式只有空间容纳完整的“长度 + 数据”时才写入。

## 四、接收流程

`xStreamBufferReceive()`：

1. 检查当前可读字节数。
2. 数据不足且允许等待时，用任务通知进入阻塞。
3. 流模式读取尽可能多的数据。
4. 消息模式先读长度，再检查用户缓冲区是否能容纳整条消息。
5. 读取后通知可能正在等待空间的发送任务。

若消息接收缓冲区太小，该消息不会被截断取走，调用者需要换用更大的接收区。

## 五、原子提交消息

消息长度和消息数据需要分两次复制。底层读写函数先使用局部 `head` 或 `tail` 操作，不立即更新对象中的正式位置。

只有整条消息复制成功后才一次性提交 `xHead` 或 `xTail`。这样另一端不会看到只有长度或只有部分数据的半条消息。

## 六、阻塞和唤醒

流缓冲区不维护事件等待链表，而是直接保存一个发送等待任务和一个接收等待任务，并使用任务通知唤醒。

因此它默认面向单生产者、单消费者：

- 同一时刻只能有一个写入者。
- 同一时刻只能有一个读取者。
- 多写入者或多读取者必须由应用额外串行化，并使用零阻塞时间完成操作。

触发级别只决定何时唤醒阻塞的接收任务，不表示缓冲区少于该值时绝对不能读取。

## 七、ISR 接口

ISR 版本不阻塞，直接根据当前空间或数据量完成操作，并通过 `pxHigherPriorityTaskWoken` 报告是否唤醒任务。

它们与任务版本共享实际的环形区读写函数，差别主要在等待、临界区和通知方式。

## 八、值得记录的设计

- 同一环形缓冲区内核同时实现字节流和离散消息。
- 用任务通知代替两条事件链表，换取更小的对象和更短的路径。
- 延迟提交 `head/tail`，低成本保证消息元数据与数据的整体可见性。
- 通过单生产者、单消费者约束简化并发控制。
