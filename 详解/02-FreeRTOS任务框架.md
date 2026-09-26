# ② FreeRTOS 任务框架 —— 两个任务、一个队列、谁来调度谁

> 适用人群：没接触过 RTOS / 只写过裸机程序 / 不知道"任务"是什么
>
> 读完本篇你会知道：为什么需要 RTOS、两个任务是怎么同时跑的、
> 数据怎么从采集任务传到通信任务的。

---

## 2.1 为什么不用"裸机"（一个大循环）

如果你只写过 Arduino 或 51 单片机，程序大概长这样：

```c
void main()
{
    while (1)
    {
        读传感器();     // 2 秒一次
        发数据();       // 可能要等十几秒
    }
}
```

这个写法的问题是：**发数据的时候（十几秒），传感器读不了**。
反过来，如果传感器读数要关 4ms 的中断，WiFi 模块在这 4ms 里收到的字节就丢了。

裸机下你要么：
- 用"定时器中断 + 标志"来错开时间 —— 逻辑一复杂就变成意大利面条
- 或者忍受"联网时采不了、采集时连不了网"

**FreeRTOS 解决这个问题的办法**：把"读传感器"和"发数据"写成两个**独立的任务**，
调度器（内核里的一个小时钟）每隔 1ms 踢一脚，决定谁该跑。

```
时间轴 →
TaskDht11: 读→睡→读→睡→读→睡→读→睡→读→睡→读→睡→...
TaskWifi:  睡→睡→联网(十几秒)→上报→上报→上报→上报→...
```

联网那十几秒里，调度器只要发现 TaskDht11 在该醒的时间，就会切过去跑一次，
再回来继续联网。两边都不耽误。

## 2.2 任务（Task）是什么

**任务 = 一个永远不会 return 的函数。**

```c
static void TaskDht11(void *argument)
{
    for (;;)            // 永远循环，不能退出
    {
        DHT11_Read(&dht);                          // 干活
        xQueueSend(g_xDht11Queue, &dht, 0);         // 把数据丢进队列
        vTaskDelay(pdMS_TO_TICKS(2000));             // 睡 2 秒
    }
}
```

关键点：
- 每个任务都是 `for (;;)` —— 一旦 `return` 任务就结束了，相当于死了
- `vTaskDelay` 让任务**睡下去** —— 睡了就不占 CPU，调度器会去跑别的任务
- 参数 `argument` 是建任务时传的，没用就 `(void)argument` 假装用了

## 2.3 本项目的两个任务

| 任务名（代码里的名字） | 优先级 | 栈大小 | 干什么 |
|---|---|---|---|
| **TaskDht11** | 2（中等） | 256 字 = 1024 字节 | 每 2 秒读一次 DHT11，把数据丢进队列 |
| **TaskWifi** | 3（较高） | 512 字 = 2048 字节 | 先联网，再从队列取数据发到云平台 |

**优先级数字越大越优先。** WiFi 比采集高，是因为网络有超时约束 ——
如果没及时回复服务器的 PING，服务器会断开连接。

但 WiFi 任务**绝大多数时间是阻塞的**（等队列数据、等网络应答），
阻塞态的任务调度器根本不看它，所以高优先级不会饿死采集。

## 2.4 队列（Queue）—— 任务之间的"信箱"

两个任务不能直接调对方的函数（那样就成了一个任务在跑）。
它们通过**队列**来传递数据：

```
TaskDht11（采集）              TaskWifi（上报）
     │                              │
     │  xQueueSend()                 │
     ├──────────────────────────────>│  xQueueReceive()
     │     队列 g_xDht11Queue        │
     │     最多存 4 次采样           │
     │     满了就丢                   │
```

关键设计：

```c
// 采集任务 —— 投递
xQueueSend(g_xDht11Queue, &dht, 0);    // 最后一个参数 0 = 不等待
                                        // 队列满了直接丢掉本次数据

// 通信任务 —— 接收
xQueueReceive(g_xDht11Queue, &dht,
              pdMS_TO_TICKS(30000));    // 超时 30 秒
                                        // 30 秒没数据就醒过来发心跳
```

重要区别：
- `xQueueSend` 的超时给 `0` —— 满了就丢。采集任务是生产者，
  被消费者拖住就本末倒置了
- `xQueueReceive` 的超时给 `30000` —— 不能永久等（`portMAX_DELAY`），
  因为服务器那边 90 秒没收到任何报文就会断开，必须隔一阵子醒过来发心跳

## 2.5 启动顺序

程序从 `main()` 开始，顺序是这样的：

```
main()
  ├─ HAL_Init()                  ← ST 官方库初始化
  ├─ SystemClock_Config()        ← 时钟 72MHz 配置
  ├─ MX_GPIO_Init()              ← GPIO 初始化
  ├─ MX_DMA_Init()               ← 开 DMA1 时钟 + 配中断优先级
  ├─ MX_USART1_UART_Init()       ← 串口 1（调试打印）
  ├─ MX_USART2_UART_Init()       ← 串口 2（ESP8266）
  │
  ├─ App_Init()                  ← 应用层初始化
  │    ├─ DHT11_Init()            ← 传感器初始化（放调度器之前）
  │    ├─ xQueueCreate()          ← 建队列
  │    ├─ TaskWifiCreate()        ← 建 WiFi 任务（还没开始跑）
  │    └─ TaskDht11Create()       ← 建 DHT11 任务（还没开始跑）
  │
  └─ vTaskStartScheduler()       ← 启动调度器！从此不再返回
                                  ← 两个任务开始轮流跑
```

**关键理解**：
- `TaskWifiCreate()` 和 `TaskDht11Create()` 只是把任务注册到 FreeRTOS 的
  就绪表上，**任务并没有开始跑**
- `vTaskStartScheduler()` 那一行才是"发令枪" —— 调度器启动后，
  两个任务才开始你一下我一下地执行
- `App_Init()` 里之所以可以做 DHT11_Init()（里面有 1 秒延时），
  是因为**调度器还没启动**，没有别的任务在跑，阻塞 1 秒没关系
- `MX_DMA_Init()` **必须排在 `MX_USART2_UART_Init()` 前面**。它自己只做两件事：
  开 DMA1 的时钟、配 `DMA1_Channel6` 的中断优先级 —— **DMA 寄存器其实是
  `usart.c` 里的 `HAL_UART_MspInit()` 配的**（`usart.c:144-159`）。串口 2 收数据
  靠 DMA，而配 DMA 之前时钟必须已经开着，所以硬约束是"先 DMA 后 USART2" 

## 2.6 任务怎么"睡"和"醒"

两个 API 理解就够了：

| 函数 | 对调用者做什么 | 对其他任务的影响 |
|---|---|---|
| `vTaskDelay(毫秒)` | 挂起本任务至少 N 毫秒 | 别的任务可以跑 |
| `xQueueReceive(队列, 缓冲区, 超时)` | 有数据立刻返回；没数据挂起到超时 | 别的任务可以跑 |

**特别注意**：`vTaskDelay` 不是 `HAL_Delay`。`HAL_Delay` 是**忙等**（原地转圈），
2 秒里 CPU 做不了任何别的事。`vTaskDelay` 是把本任务挂起，CPU 让给别人。

## 2.7 栈（Stack）—— 每个任务的私人空间

每个任务有自己独立的栈，不是共用。栈大小在建任务时指定：

```c
xTaskCreate(TaskWifi, "WiFi", APP_STACK_WIFI, ...);
// APP_STACK_WIFI = 512 字 = 2048 字节
```

如果栈太小，任务会悄悄崩溃（现象很诡异：跑着跑着突然不走了，或者
串口打出乱码）。所以代码里会在联网成功后打印栈余量：

```c
printf("[WiFi] stack free = %lu words\r\n",
       (unsigned long)uxTaskGetStackHighWaterMark(NULL));
```

看到小于 50 就该调大栈大小。

## 2.8 堆（Heap）—— 所有任务分内存的地方

FreeRTOS 的 `heap_4.c` 管理一个 10240 字节的堆。任务栈、队列、定时器
都从这里分配。

**如果 `xTaskCreate` 返回失败（打印 `heap too small?`）**，说明堆不够大。
但本项目两个任务 + 一个队列，10240 字节实测够用。

## 2.9 Freertos 相关文件

你不需要去看这些文件的细节，知道在那就行：

```
t2/freertos/
├── src/              ← 内核代码（7 个 .c 文件）
│   ├── tasks.c       ← 任务管理（优先级、调度）
│   ├── queue.c       ← 队列
│   ├── timers.c      ← 软件定时器
│   └── ...
├── inc/              ← 头文件
│   ├── FreeRTOS.h
│   └── FreeRTOSConfig.h  ← 项目配置（优先级数量、堆大小等）
└── port/             ← 端口层（STM32F103 适配）
    ├── port.c
    └── portmacro.h
```

**本项目的特殊配置**：
- `configTICK_RATE_HZ = 1000` —— 调度器每 1ms 踢一次
- `configTOTAL_HEAP_SIZE = 10240` —— 堆总共 10KB
- `configMAX_PRIORITIES = 5` —— 优先级 0~4，数字越大越优先
- 时基用 TIM4，不是 SysTick（SysTick 给 FreeRTOS 自己用了）

---

**继续阅读：** [03-ESP8266 WiFi 模块驱动](03-ESP8266-WiFi模块驱动.md)