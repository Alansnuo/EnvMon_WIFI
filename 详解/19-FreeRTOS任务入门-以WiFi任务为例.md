# 详解 19 · FreeRTOS 任务入门 —— 以 WiFi 任务为例

> **覆盖代码**：`APP/app_main.c`、`APP/app_main.h`、`APP/task_wifi.c`、
> `APP/app_delay.c`、`Core/Src/main.c`
> 配套配置：`freertos/inc/FreeRTOSConfig.h`、`APP/app_config.h`
>
> **读者**：第一次碰 RTOS，想知道"任务到底是什么、`xTaskCreate()` 那六个参数在说什么、
> 一个任务怎么调驱动把数据发出去"的人。
>
> **阅读前提**：会写裸机的 `while(1)` 程序；知道中断、栈、指针是什么。
> 不需要事先懂任何 RTOS 概念。
>
> **本文不讲什么**：DHT11 的时序（只在"数据从哪来"处带过一句）、
> AT 指令与 ESP8266 驱动的内部（那是 [详解 20](20-ESP8266驱动详解.md) 的事）。
>
> **行号说明**：本文**只写函数名，不写行号**。代码一改行号就漂，函数名不会。

---

## 19.1 这块代码要解决什么问题

### 一句话

**让"每隔 2 秒采一次数据"和"联网、上报"这两件事同时进行，谁也不等谁。**

### 问题：两件节奏完全不同的事

本项目要同时干两件节奏差很远的事：

| 事 | 节奏 | 特点 |
| --- | --- | --- |
| 读温湿度 | 每 2 秒一次 | 快、周期固定 |
| 联网 + MQTT 上报 | 一次上线要十几秒，之后每 2 秒发一包 | 中间大量时间在**等**（等模块回 OK、等 DHCP、等 broker） |

裸机写法（一个大 `while(1)` 里轮流调）的问题：

- 联网那十几秒里，MCU 全耗在等模块上，传感器那 2 秒的节拍被拖得七零八落；
- 反过来，传感器读一次要关中断、占用几毫秒，也会影响串口收数据。

**RTOS 的解法**：把两件事各写成一个"自己的死循环函数"，这两个函数就叫**任务**。谁来跑由调度器决定；一个任务在等（延时、等队列、等串口）的时候会自动**让出 CPU**，另一个任务立刻接上。于是"等十几秒"只影响 WiFi 任务自己。

### 约束

| 约束 | 出处 | 后果 |
| --- | --- | --- |
| 堆只有 10240 字节 | `configTOTAL_HEAP_SIZE` | 任务栈、队列、任务控制块全从这里出，建任务失败要当致命错误处理 |
| 优先级只有 1~3 可用 | `configMAX_PRIORITIES = 5` | 4 被定时器服务任务占了，0 是空闲任务 |
| 栈的单位是**字**，不是字节 | `xTaskCreate()` 的参数定义 | 512 不是 512 字节，是 2 KB |
| 调度器起来之前不能调 `vTaskDelay` | FreeRTOS 的规定 | `App_Init()` 里的初始化只能忙等，解法见 19.2 |
| tick 1000 Hz | `configTICK_RATE_HZ` | 时间的分辨率是 1 ms，`pdMS_TO_TICKS(2000) == 2000` |

### 本文不讲什么

- **DHT11 怎么读**：采集任务只在 19.3.4 里作为"数据从哪来"出现一次。
- **AT 指令怎么写、驱动怎么收发**：见 [详解 20 · ESP8266 驱动详解](20-ESP8266驱动详解.md)。
  本文只需要知道一件事：`ESP8266_MQTT_Publish()` 是**阻塞**的，返回时事情已经有了结果。

---

## 19.2 整体结构

### 一个任务 = 一个不会返回的函数 + 自己的一个栈

```c
static void TaskWifi(void *argument)     /* 参数：建任务时传给它的东西 */
{
    (void)argument;                      /* 本项目不用，显式忽略 */

    /* 这里放这个任务的局部变量 —— 它们占的是"这个任务自己的栈" */
    DHT11_Data dht;
    char       payload[192];

    for (;;)                             /* ★ 必须自己死循环，函数返回 = 任务结束 */
    {
        /* 这个任务要干的活 */
    }
}
```

三条硬规矩：

1. **函数签名固定**：`void 函数名(void *参数)`，返回值必须 `void`。
2. **必须自己死循环**：`for (;;)`。任务函数一旦 `return`，FreeRTOS 会把它当"任务自己要求删除"处理（没开 `INCLUDE_vTaskDelete` 就直接进断言）。
3. **栈是独立的**：任务里的局部变量、函数调用、`printf` 用的缓冲，全从这个任务的栈里出。所以栈开小了会"莫名其妙跑飞"，开大了浪费 RAM —— 见 19.4 决策二。

### 任务从哪来：启动链路

任务的诞生分三步，顺序不能乱：

```
main()                                          Core/Src/main.c
 ├─ HAL_Init()                                   HAL 库初始化
 ├─ SystemClock_Config()                         时钟配置
 ├─ MX_GPIO_Init() / MX_DMA_Init() / MX_USART*_Init()
 ├─ App_Init()                                   ← 应用层全在这里起步
 │    ├─ DHT11_Init()                            板级驱动（此时还没有别的任务）
 │    ├─ g_xDht11Queue = xQueueCreate(4, sizeof(DHT11_Data))   数据通道
 │    ├─ TaskWifiCreate()  ─┐                    只是"登记"任务
 │    └─ TaskDht11Create() ─┘
 └─ vTaskStartScheduler()                        ← 到这一句，任务才真正开始跑
```

两个必须记住的点：

1. **建任务 ≠ 任务在跑**。`xTaskCreate()` 只是把任务挂到就绪表上；真正的切换发生在 `vTaskStartScheduler()` 之后。所以 `App_Init()` 里两个任务函数的代码一行都还没执行。
2. **调度器启动之前不能调 FreeRTOS 的延时/等待 API**。`vTaskDelay()` 在调度器没跑时调用属于非法操作，会掉进 `configASSERT` 死循环。本项目正好踩在这个边上：`App_Init()` 里的 `DHT11_Init()` 内部要延时 1 秒，而那时调度器还没起来 —— 解法是弱/强符号：

   ```c
   /* BSP/bsp_delay/bsp_delay.c  弱定义：裸机 / 调度器启动前走这条 */
   __weak void BSP_DelayMs(uint32_t ms) { HAL_Delay(ms); }

   /* APP/app_delay.c  强定义：链接器优先选它 */
   void BSP_DelayMs(uint32_t ms)
   {
       if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
       {
           vTaskDelay(pdMS_TO_TICKS(ms));   /* 任务里：挂起自己，让出 CPU */
       }
       else
       {
           HAL_Delay(ms);                   /* 调度器没起来：只能忙等 */
       }
   }
   ```

   驱动里一律调 `BSP_DelayMs()`，于是同一份驱动"上 RTOS 会让出 CPU、裸机变忙等"，一个字都不用改。

**顺序上还有一条约定**：谁先建不影响谁先跑 —— 先跑的是优先级高的那个。

### 任务表与关键常量

本项目实际的任务表（`APP/app_main.h`）：

| 任务 | 优先级 | 栈 | 入口函数 | 干什么 |
| --- | --- | --- | --- | --- |
| 定时器服务任务 | 4 | 128 字 | FreeRTOS 内部 | 软件定时器用，本项目没用到定时器，但任务照样占着 |
| **WiFi 任务** | **3** | **512 字** | `TaskWifi()` | 联网 + 从队列取数据发到 OneNET |
| DHT11 任务 | 2 | 256 字 | `TaskDht11()` | 每 2 秒采一次，放进队列 |
| 空闲任务 | 0 | 128 字 | FreeRTOS 内部 | 所有任务都阻塞时它跑，顺便回收被删任务的内存 |

### 调度规则（够用版）

- **优先级**：数字越大越优先。只要有高优先级任务处于"就绪"，低优先级就一定不跑。
- **同级轮转**：`configUSE_TIME_SLICING = 1`，同优先级的任务按 tick 轮流跑。
- **阻塞就是让出**：调 `vTaskDelay()`、`xQueueReceive()` 等待时，任务进入阻塞态，CPU 立刻交给其他任务。**RTOS 里所有"等"都应该是阻塞式的等待，而不是空转**。

---

## 19.3 逐段详解

### 19.3.1 `xTaskCreate()` —— 六个参数一个一个说

建任务就调这一个函数（本项目用的是动态创建版，栈从 FreeRTOS 的堆里分配，`heap_4`，`configTOTAL_HEAP_SIZE = 10240` 字节）：

```c
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,      /* 1. 任务函数（就是上面那个死循环函数的名字） */
    const char    *pcName,          /* 2. 任务名，纯给人看/调试用 */
    configSTACK_DEPTH_TYPE usStackDepth, /* 3. 栈大小 —— 单位是"字", 不是字节！ */
    void          *pvParameters,    /* 4. 传给任务函数的那个 void* 参数 */
    UBaseType_t    uxPriority,      /* 5. 优先级，数字大的优先 */
    TaskHandle_t  *pxCreatedTask    /* 6. 输出参数：任务句柄，不需要就传 NULL */
);
```

对应到本项目的真实取值：

| 参数 | 本项目给的值 | 说明 |
| --- | --- | --- |
| `pxTaskCode` | `TaskWifi` | 函数名，不带括号 |
| `pcName` | `"WiFi"` | 调试区看到的任务名 |
| `usStackDepth` | `APP_STACK_WIFI` = **512** | **单位是字**。STM32F103 一个字 4 字节 → 实占 2 KB |
| `pvParameters` | `NULL` | 本项目任务不需要外部传参（要用的东西直接从 `app_config.h` 的宏和全局队列拿） |
| `uxPriority` | `APP_PRIO_WIFI` = **3** | 见 19.4 决策一 |
| `pxCreatedTask` | `NULL` | 不保存句柄 —— 本项目不需要 `vTaskSuspend` 之类的操作 |

**优先级取值的边界**：`configMAX_PRIORITIES = 5`，所以合法值是 `0 ~ 4`；其中 `4` 被 FreeRTOS 的定时器服务任务占了（`configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1`）、`0` 是空闲任务。**应用任务实际能用的只有 1~3**，本项目 WiFi 用 3、DHT11 用 2。

**返回值**：成功是 `pdPASS`；常见失败是 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`（堆不够，任务的控制块或栈分配不出来）。

### 19.3.2 `TaskWifiCreate()` —— 建任务这一步的真实样子

`APP/task_wifi.c` 里那个"建任务"的函数，全项目建任务都是这个模子：

```c
void TaskWifiCreate(void)
{
    if (xTaskCreate(TaskWifi, "WiFi", APP_STACK_WIFI, NULL,
                    APP_PRIO_WIFI, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(WiFi) failed (heap too small?)\r\n");
        Error_Handler();
    }
}
```

写法上的三个点：

- **栈和优先级不写字面量**，用 `APP_STACK_WIFI` / `APP_PRIO_WIFI`（定义在 `APP/app_main.h`）。想调优先级只改头文件一处，不用翻代码。
- **失败就 `Error_Handler()` 停住**：堆不够意味着这个任务永远不会存在，系统已经是残废的了 —— 定死了的错误就早点死，别带着半条命往下跑。
- **函数名后缀 `Create`**，内部才是任务本体 `TaskWifi`。`App_Init()` 只调 `Create`，看不到任务函数的细节。

### 19.3.3 `TaskWifi()` —— 任务本体逐段拆

`APP/task_wifi.c` 的任务本体，删掉注释后就是下面这个骨架：

```c
static void TaskWifi(void *argument)
{
    DHT11_Data dht;
    char       payload[192];            /* OneNET 物模型的 JSON 报文 */
    static uint32_t msgId = 0;          /* 报文序号, 每次上报 +1 填进 "id" */
    uint8_t    failStreak;              /* 连续失败计数 */

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)                            /* ★ 任务的死循环 */
    {
        /* --- 阶段 1: 联网 --- */
        while (WifiConnect() == 0)      /* 连不上就一直重试, 不往下走 */
        {
            printf("[WiFi] retry in %d s\r\n", WIFI_RETRY_DELAY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));   /* 让出 CPU 5 秒 */
        }

        /* --- 阶段 2: 上报 --- */
        failStreak = 0;
        while (failStreak < WIFI_MAX_CONSEC_FAIL)
        {
            if (xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY) != pdPASS)
            {
                break;                  /* 队列异常, 退出去重连 */
            }

            (void)snprintf(payload, sizeof(payload),
                "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
                    "\"Temp\":{\"value\":%d.0},"
                    "\"Hum\":{\"value\":%d.0}"
                "}}",
                (unsigned long)(++msgId), dht.temperature, dht.humidity);

            if (ESP8266_MQTT_Publish(CFG_MQTT_PUB_TOPIC, payload) == ESP8266_OK)
            {
                failStreak = 0;         /* 成功: 计数清零 */
            }
            else
            {
                failStreak++;           /* 失败: 累加, 到 3 次退回重连 */
            }
        }

        printf("[WiFi] publish failed %u times in a row, reconnecting\r\n",
               (unsigned)failStreak);
    }
}
```

几个刻意的写法：

- **局部变量放在任务函数里**：`payload[192]` 用的是 WiFi 任务的栈（这也是它的栈要开到 512 字的原因之一）。用 `static` 的 `msgId` 是为了跨循环记住序号。
- **两个 `while` 就是状态机**：外层管"没连上就重连"，内层管"连上了就发，连续失败 3 次就跳回外层"。整段读下来就是一句人话：**连上 → 发 → 发不动了 → 回到连**。
- **失败重连走到 `WifiConnect()`**，第一步是 `ESP8266_Init()`，里面会拉 RST 把模块整个复位。所以不管模块是卡死、半连还是 WiFi 掉了，走一遍就是干净状态 —— 不用另写清理代码。
- **`WIFI_MAX_CONSEC_FAIL = 3`**：单次失败可能只是网络抖一下，而重连要复位模块 + 重关联 + 重连 broker，十几秒起步；3 次失败（约 6 秒）才认定链路真死了。

### 19.3.4 一次上报的全链路：任务 → 驱动 → 模块 → 平台

这是"任务怎么调用 WiFi 函数把数据传上去"的完整答案，把一次上报从头到尾串一遍：

```
DHT11 任务                       WiFi 任务                     ESP8266 模块           OneNET
    │                                │                              │                   │
    │ 每 2 秒采集一次                 │                              │                   │
    │ xQueueSend(g_xDht11Queue, …)   │                              │                   │
    ├───────────────────────────────►│                              │                   │
    │  (队列满了就丢, 绝不等)          │ xQueueReceive(…, portMAX_DELAY) 取到一条          │
    │                                │ 拼 JSON 报文                  │                   │
    │                                │ ESP8266_MQTT_Publish(topic, payload)              │
    │                                ├─────────────────────────────►│                   │
    │                                │   AT+MQTTPUB=0,"<主题>","<载荷>",0,0              │
    │                                │   驱动发完这条 AT 指令, 就等模块回 "OK"            │
    │                                │◄─────────────────────────────┤ OK                │
    │                                │  返回 ESP8266_OK ────────────┴──────────────────►│  数据点刷新
```

逐步说清"谁在等谁"：

1. **取数据**：`xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY)`。队列空的时候，WiFi 任务就**睡着**（阻塞态），一点 CPU 都不占；队列里一有数据，调度器立刻把它叫醒。
2. **拼报文**：`snprintf()` 拼 OneNET 物模型的 JSON。属性名 `Temp` / `Hum` 必须和控制台"功能定义"里大小写完全一致，否则平台不收（而且不一定报错）。
3. **调驱动发**：`ESP8266_MQTT_Publish(CFG_MQTT_PUB_TOPIC, payload)`。**注意这是阻塞式调用** —— 函数返回时，要么数据已经交给模块（模块回了 OK），要么已经判定失败（超时 / 模块回 ERROR）。等的时候驱动内部调 `BSP_DelayMs(1)` → `vTaskDelay` 让出 CPU，所以采集任务、灯这些照跑，只是 **WiFi 任务自己停在函数里**。
4. **判定与恢复**：返回 `ESP8266_OK` 就继续等下一包；否则失败计数 +1，连续 3 次就跳出内层循环，回到阶段 1 重连。

**驱动和任务的分工**（本项目的一条设计原则）：驱动（`BSP/ESP8266/`）只管"把 AT 指令发出去、把应答收回来"，它不知道什么叫"重试"；策略（重连、失败计数、退避时间）全在任务层。所以调驱动的那几行看起来特别朴素 —— 就是一句 `if (… != ESP8266_OK)`。

`WifiConnect()` 同理，四步一路检查下来：

```c
ESP8266_Init()                                          /* 复位模块 + AT 探活 */
ESP8266_ConnectAP(CFG_WIFI_SSID, CFG_WIFI_PASSWORD)     /* 关联热点 */
ESP8266_MQTT_SetParam(设备名, 产品ID, Token, keepalive)   /* 配 MQTT 鉴权 */
ESP8266_MQTT_ConnectBroker(CFG_MQTT_HOST, CFG_MQTT_PORT) /* 连 broker */
```

### 19.3.5 队列 —— 两个任务之间唯一的通道

本项目只有一条数据通道：`g_xDht11Queue`（生产者在采集任务，消费者在 WiFi 任务）。

```c
/* 建：在 App_Init() 里，长度 4，每个元素是一条温湿度数据 */
g_xDht11Queue = xQueueCreate(APP_DHT11_QUEUE_LEN, sizeof(DHT11_Data));

/* 放：采集任务，超时 0 = 队列满了立刻返回失败，绝不在这里等 */
xQueueSend(g_xDht11Queue, &dht, 0);

/* 取：WiFi 任务，portMAX_DELAY = 一直等到有数据为止 */
xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY);
```

为什么用队列而不是一个全局变量：

| | 全局变量 | 队列 |
| --- | --- | --- |
| 并发安全 | 两边同时读写会撕裂（写一半被切走） | FreeRTOS 内部关中断/加锁，安全 |
| 缓冲区 | 自己想办法，只能存最新一条 | 自带长度 4 的缓冲，能囤 4 条 |
| 让出 CPU | 没有等待机制，只能轮询 | 消费者没数据就睡着，有数据立刻被叫醒 |

### 19.3.6 延时、让出与"时间"

- **`vTaskDelay(pdMS_TO_TICKS(ms))`**：把本任务挂起 ms 毫秒，CPU 让给别人。
- **`configTICK_RATE_HZ = 1000`**：系统节拍 1 ms 一次，所以 `pdMS_TO_TICKS(2000) == 2000`，写起来和毫秒数一样。
- **实际睡眠时间会比要求的长一点点**（最多一个 tick），只多不少 —— 驱动里"等模块启动"这类"至少等够"的场合刚好合适。
- **任务里绝对不要用 `HAL_Delay()`**：它是忙等，抱着 CPU 空转，这 2 秒里所有低优先级任务全被饿死。要用就调 `BSP_DelayMs()`（RTOS 下自动变 `vTaskDelay`）。
- **`portMAX_DELAY`**（等于 `0xFFFFFFFF`）：超长的超时，实际含义是"无限等"，等不到就一直阻塞着，不占 CPU。

---

## 19.4 关键设计决策

### 决策一：优先级按"谁耽误不起"排 —— WiFi(3) 高于 DHT11(2)

网络有超时约束（broker 有 keepalive、平台有数据点间隔），晚一点就要重连；采集只是周期采样，晚个几十毫秒无所谓 —— 所以 WiFi = 3 高于 DHT11 = 2。

同时要注意：优先级高低**不改变"阻塞会让出"**这件事，WiFi 任务等串口时照样让采集跑，不会把低优先级任务饿死。

### 决策二：栈按"最吃栈的那个阶段"定，单位是字

WiFi 任务给 512 字（2 KB），比采集的 256 字大一倍，原因是**联网阶段**而不是上报阶段：驱动里要拼 AT 命令字符串（`s_Cmd` 288 字节）、`printf` 自己的缓冲也在这里花掉一截。定栈大小时看的是最坏那一刻，不是平均水平。

判断栈够不够：

- **症状**：任务里某个局部变量莫名其妙被改、函数返回地址乱掉、进 `HardFault`。
- **量化**：`uxTaskGetStackHighWaterMark(句柄)` 返回"历史最低还剩多少**字**"，本工程 `INCLUDE_uxTaskGetStackHighWaterMark = 1` 已经打开，随时可以打印出来看。**本项目当前没有打印栈余量**，要量的话得先把任务句柄存下来（建任务时第 6 个参数别传 `NULL`）。

### 决策三：队列长度 4，生产者超时给 0，消费者给 `portMAX_DELAY`

两个超时值的取法是刻意的：

- **生产者（采集任务）给 0**：它是被服务的一方，绝不能被消费者拖住，队列满了就丢掉这次采样并打印一行。
- **消费者（WiFi 任务）给 `portMAX_DELAY`**：它本来就没别的事干，没数据时睡着最省 CPU。

队列长度 4 也是这个道理：WiFi 任务在联网那十几秒里取不了数据，采集任务照常往里投，最多囤 4 条；再满就丢 —— 宁可丢几条历史数据，也不让采集被拖慢。

### 决策四：所有"等"都走 `BSP_DelayMs()`，驱动不认识 RTOS

驱动（`BSP/ESP8266/`）里没有任何 `vTask*`、`xQueue*`、`pdMS_*` 的字样 —— 它只调 `BSP_DelayMs()`。RTOS 里这句变成 `vTaskDelay()`（让出 CPU），裸机里变成 `HAL_Delay()`（忙等），**同一份驱动一个字都不用改**。

代价是驱动里的等待时长是"整毫秒级"的，做不了精细的 tick 比较；好处是驱动可以在任何工程里直接拿来用。

### 决策五：碰 ESP8266 的代码全关在 WiFi 任务里

别的任务只通过队列和它打交道。两个任务同时操作一个外设（一个在发 AT、一个在拉 RST）是最难查的一类 bug，本项目从结构上就避开了。

### 决策六：建任务失败就 `Error_Handler()`，不带着半条命跑

堆不够时任务是不存在的，采集或上报会彻底停摆。这种"定死了的死"要早点死、死得明白，别让系统带病跑出更难查的现场。

---

## 19.5 已知局限与注意事项

新手最容易踩的坑（都在本项目里出现过）：

1. **调度器没起来就调 `vTaskDelay`** → 掉进 `configASSERT` 死循环。本项目的解法是 `app_delay.c` 里那句 `xTaskGetSchedulerState() == taskSCHEDULER_RUNNING` 判断，**别删**。
2. **栈大小当成字节填** → 以为 512 是 512 字节，其实是 512 字 = 2 KB；反过来填小了就是跑飞。
3. **任务函数忘写 `for(;;)`，或者在循环里 `return`** → 任务直接结束，FreeRTOS 断言/异常。
4. **高优先级任务里不放让出点**（比如用 `HAL_Delay` 忙等，或者死循环里不调任何阻塞 API）→ 低优先级任务永远得不到执行。
5. **`printf` 会阻塞**：`main.c` 里的 `fputc()` 用的是 `HAL_UART_Transmit(..., HAL_MAX_DELAY)`，串口发不出去（比如调试串口没接、波特率不对）就会一直等在那里，把当前任务卡死。打印用在调试阶段没问题，别在时间敏感的地方刷屏。
6. **重连期间数据是"丢"，不是"延迟"**：队列只有 4 个槽 = 8 秒缓冲，而一次失败的 `WifiConnect` 要十几秒 —— 每轮重连丢几条数据。想不丢就得等发布成功再取下一包，或者把队列加长（RAM 账见下）。

**RAM 账**：`configTOTAL_HEAP_SIZE = 10240` 字节，任务栈、队列、任务控制块都从这里出。两个任务栈 2 KB + 1 KB、空闲任务 512 B、定时器任务 512 B，加上队列 4×`sizeof(DHT11_Data)` 和各任务控制块 —— 这就是为什么 `xTaskCreate` 失败要当成致命错误处理，也是为什么不能随手把栈和队列加大。

**没有的东西**：栈余量没有打印；没有任务级的超时看门狗；WiFi 任务一旦死在某个 AT 等待里，没有"重启这个任务"的机制（只能靠模块自己超时返回）。

---

## 19.6 动手改这里

想再加一个任务，要改五处：

1. 新建 `APP/task_xxx.c` / `.h`，写 `TaskXxxCreate()`（里面 `xTaskCreate` + 失败打印 + `Error_Handler`）和 `static void TaskXxx(void *argument)`（`for(;;)` 不能少）。
2. 在 `APP/app_main.h` 加两个宏：`APP_PRIO_XXX`（在 1~3 里挑）、`APP_STACK_XXX`（单位字）。
3. 在 `App_Init()` 里加一句 `TaskXxxCreate()` —— 放在 `vTaskStartScheduler()` 之前就行，位置不影响谁先跑。
4. 数据要给别的任务，就在 `App_Init()` 里再建一条队列（`xQueueCreate`），别用全局变量。
5. 如果是新外设，初始化放 `App_Init()` 里、建任务之前（那时没有并发问题，延时可以用）。

**改上报周期**：改采集任务里的 `vTaskDelay`（`APP/app_main.h` 里的周期宏），别改 WiFi 任务的 —— WiFi 任务的节拍是被队列推着走的。

**改优先级之前先想清楚**：本项目的默认顺序是 WiFi > 采集，理由是网络有超时约束。反过来调的话，采集的关中断时序可能会切进 AT 指令的收发里。

---

## 19.7 一句话总结

**两个任务各写一个 `for(;;)`，靠队列传数据、靠优先级决定谁先跑、靠 `vTaskDelay` 让出 CPU —— 于是"联网十几秒"只耽误 WiFi 任务自己，采集的 2 秒节拍照旧。**

三件最容易被误解的事：

1. **`xTaskCreate()` 的栈单位是"字"**，512 就是 2 KB；填 512 字节的想法会让任务跑飞。
2. **建任务不等于任务在跑**，真正的起点是 `vTaskStartScheduler()`；在那之前连 `vTaskDelay` 都不能调（本项目用弱/强符号的 `BSP_DelayMs` 绕开）。
3. **任务调驱动那一刻，任务自己是停住的**：`ESP8266_MQTT_Publish()` 返回时事情已经有结果 —— 这是驱动"顺序阻塞"设计的直接后果，也正因为如此，策略（重试、重连、计数）才有地方可以写。

---

## 附：本笔记涉及的文件

| 文件 | 内容 |
| --- | --- |
| `Core/Src/main.c` | 启动流程、`App_Init()` 和 `vTaskStartScheduler()` 的调用位置、`fputc` 重定向 |
| `APP/app_main.c` | 应用层总入口：建队列 + 建任务 |
| `APP/app_main.h` | 优先级、栈、队列长度、任务周期的宏定义 |
| `APP/task_wifi.c` | WiFi 任务本体（联网 + 上报） |
| `APP/app_delay.c` | `BSP_DelayMs()` 的 RTOS 版（强符号，覆盖 BSP 里的弱符号） |
| `BSP/bsp_delay/bsp_delay.h` | 弱/强符号机制的原理，驱动"不依赖 RTOS 却会让人"的关键 |
| `freertos/inc/FreeRTOSConfig.h` | tick 1000 Hz、优先级数 5、堆 10240 字节、中断优先级门槛 5 |
| `APP/app_config.h` | 所有要换环境的参数（WiFi、MQTT、主题）集中在这里 |
