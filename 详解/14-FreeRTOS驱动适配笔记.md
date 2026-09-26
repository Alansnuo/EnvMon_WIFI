# FreeRTOS 驱动适配笔记 —— 以 ESP8266 为例

> **结论：需要改。**
>
> 但不是"全部推倒重来"。驱动里有三类问题，严重程度差很多：
> 有的不改会**死机**，有的不改只是**白烧 CPU**，有的**多任务时才暴露**。
>
> 本文按"为什么要改 → 改哪里 → 怎么改"组织，ESP8266 是主体，DHT11 只占一节
> （但那一节里有个**最致命的问题**）。

---

## 一、先回答：需要改吗

| 级别 | 问题 | 不改的后果 | 涉及文件 |
|---|---|---|---|
| 🔴 **致命** | DHT11 和 FreeRTOS **抢 SysTick** | **DHT11 读取全部失败**（详见第四节） | `dht11.c` |
| 🟡 重要 | `HAL_Delay()` 忙等 | 低优先级任务被饿死 | `esp8266.c` / `dht11.c` |
| 🟡 重要 | `WaitResponse()` 轮询最长 15 秒 | 连 WiFi 时 CPU 100% 空转 | `esp8266.c` |
| 🟢 建议 | 共享状态无互斥保护 | **多任务**同时用 ESP8266 时数据错乱 | `esp8266.c` |
| 🟢 建议 | 任务栈可能不够 | 栈溢出，随机死机 | 建任务处（现在是 `APP/app_main.h`） |
| 🟢 建议 | `printf` 非线程安全 | 输出乱码 | `main.c` |

**一句话**：如果只跑**一个任务**，🟡 和 🟢 那几条"能跑但难看"；
但 🔴 那一条**一定会出问题**，而且现象很迷惑。

> 📌 **本文写于改造之前**，上面的表是**当时**的评估。后面各节里：
> **标 ✅ 的已经实施了**（DHT11 换 DWT、`BSP_DelayMs`、DMA 接收），
> **标"当时建议/未采用"的是走过又放弃的方案** —— 都按实际状态标出来了，
> 看的时候别把"当时的设计"当成"现在的代码"。

---

## 二、为什么阻塞式驱动在 FreeRTOS 下是问题

### 2.1 根本差别：从"卡住无所谓"到"卡住=别人全等着"

裸机程序只有一条执行流。函数里 `HAL_Delay(1000)` 卡 1 秒，
"卡"的就只有这唯一的执行流 —— **没有别人被影响，所以无所谓**。

FreeRTOS 下有多个执行流（任务）。**一个任务占着 CPU 不放手，
同优先级和更低优先级的任务全都得不到执行。**

```
裸机:       只有我一条路 ──────────────►  卡住 = 慢一点, 但没人受影响

FreeRTOS:   任务A ████████████░░░░░░░░    任务A 忙等 15 秒, 占满 CPU
            任务B ░░░░░░░░░░░░(饿死)      任务B 一直没机会跑
            任务C ░░░░░░░░░░░░(饿死)
```

**关键**：这不是"变慢了"，是**任务 B、C 完全停摆**。
如果你的设计里有个"喂狗任务"或"按键响应任务"，它俩一停就是事故。

### 2.2 `HAL_Delay()` 到底干了什么

查 HAL 源码：

```c
void HAL_Delay(uint32_t Delay)
{
    uint32_t tickstart = HAL_GetTick();
    uint32_t wait = Delay;
    if (wait < HAL_MAX_DELAY) { wait += (uint32_t)(uwTickFreq); }

    while ((HAL_GetTick() - tickstart) < wait)   // ← 就这一句, 空转
    {
    }
}
```

**它就是一个空转的 `while` 循环**，反复读 `uwTick` 变量的值，直到时间到。

在任务里调用它，等于告诉调度器："我要空转 N 毫秒，别管我" ——
**CPU 全程被占着，什么正事都不干。**

### 2.3 ⚠️ 但不是所有 `HAL_Delay` 都能简单换成 `vTaskDelay`

这是最容易踩的坑。**`vTaskDelay` 只能在调度器启动之后调用。**

```c
HAL_Delay(1000);       // 裸机 / 调度器启动前  → 正确
vTaskDelay(...);       // 调度器启动前调用     → 出错 / 断言失败
```

因为 `vTaskDelay` 要把当前任务挂起、让调度器去跑别人 ——
**调度器都没启动，没有"当前任务"这个概念**，调用它属于未定义行为。

**本工程正好卡在这个边界上**。**当时**（还没有任务）`main.c` 的结构是这样，
初始化全在调度器之前跑：

```c
int main(void)
{
  ...
  DHT11_Init();            // ← 调度器启动前
  ESP8266_Init();          // ← 调度器启动前, 里面有 2 秒的 HAL_Delay
  ESP8266_ConnectWiFi(...) // ← 调度器启动前, 里面有最长 15 秒的轮询

  xTaskCreate(...);        // ← 将来在这里建任务
  vTaskStartScheduler();   // ← 调度器在这里才启动
  while (1);
}
```

**现在（改造之后）变成了这样** —— 联网搬进了任务，启动前的活儿只剩传感器初始化：

```c
  ...
  App_Init();              /* Core/Src/main.c:102 */

  vTaskStartScheduler();   /* Core/Src/main.c:104 */

  /* 只有 heap 不足、连空闲任务都建不起来时, vTaskStartScheduler() 才会返回 */
  printf("ERROR: vTaskStartScheduler() returned!\r\n");
  Error_Handler();
```

- `DHT11_Init()` 仍然在**调度器启动前**跑（`App_Init()` 的第一件事，
  `APP/app_main.c:54`）—— **这条边界还是踩着的**
- `ESP8266_Init()` / `ESP8266_ConnectWiFi()` 改到了 **`TaskWifi` 任务里**
  （`APP/task_wifi.c:131-196` 的 `WifiConnect()`），是**调度器启动之后**

> ⚠️ 顺手提一句**源码注释里的残留**：`Core/Src/main.c:97` 还写着
> `App_Init()` 内部是"DHT11 初始化 → 建数据队列 → **建启动任务**"。
> 那个 `StartTask`（"先联网、再建采集任务"）早就拆掉了 ——
> 见 `APP/app_main.c:30-33` 的文件头说明，拆它的原因就是"DMA 收字节之后，
> 采集和联网可以同时跑，不再需要先联网再建采集任务"。
> 现在 `App_Init()` 建的是 `TaskWifi` / `TaskDht11` 两个任务。
> **读注释时以代码为准**（这行注释在 `main.c` 里，一直没跟着改）。

**结论不变**：`vTaskDelay` 不能在调度器启动前用。
`DHT11_Init()` 里那句 1 秒延时**不能**直接换成 `vTaskDelay` —— 会卡在半路出不来。

**解法见 3.1：一个"自适应延时"包装函数** —— 最终实现成了弱符号/强符号两套。

---

## 三、ESP8266 需要改什么

### 3.0 修改点一览

| # | 位置 | 现在 | 改成 | 状态 |
|---|---|---|---|---|
| 1 | `Reset()` 的 100/1000ms<br>`Init()` 的 1000ms | `HAL_Delay` | **`BSP_DelayMs()`**（弱/强符号两套实现，见 3.1） | ✅ 已做 |
| 2 | `WaitResponse()` | 逐字节忙等轮询 | **DMA 环形缓冲 + 轮询 CNDTR**（**不是**当时设想的 IDLE + 信号量，见 3.3） | ✅ 已做（换了方案） |
| 3 | 所有公开 API | 无保护 | 递归互斥量 | ⏳ **未做** —— 只有一个任务用 ESP8266，见 3.4 |
| 4 | 任务创建处 | `128` words | **实际取 512 words**（`APP_STACK_WIFI`，见 3.5） | ✅ 已做 |
| 5 | `printf` / `fputc` | 裸调用 | 加互斥 或 少用 | ⏳ **未做**，仍是 `HAL_MAX_DELAY`，见 3.6 |

### 3.1 修改点 1：自适应延时 ✅ 已实施（实现方式换了）

**问题**：驱动里的延时既要能在"调度器启动前"用，又要能在"任务里"用。

#### 当时的方案（未被采用）

写一个包装函数，运行时判断调度器状态：

```c
/* 建议放在 BSP 下, 比如 bsp_delay.h / bsp_delay.c, 供所有驱动共用 */
#include "FreeRTOS.h"
#include "task.h"

void BSP_DelayMs(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));   // 任务里: 让出 CPU, 别人能跑
    }
    else
    {
        HAL_Delay(ms);                   // 调度器启动前: 只能忙等
    }
}
```

⚠️ **这段代码本身没错，但放在 `BSP/` 下就错了**：文件里要
`#include "FreeRTOS.h"`，**BSP 层从此依赖 FreeRTOS**，这些驱动就再也没法
拿到裸机工程里用。而"驱动不认识 RTOS"是这个项目一直守着的规矩 ——
`BSP/bsp_delay/bsp_delay.h` 的注释写得很直白：

> ⚠️ 本头文件故意只 include <stdint.h>, 不 include "main.h".
>    这样驱动带着它就能移植到别的芯片, 不用把 STM32 的头文件一起搬。

#### 最终实现：弱符号 / 强符号，由链接器挑一个

同一个函数名，两份实现，放在两个目录下：

| 文件 | 符号强度 | 内容 | 什么时候被链接进去 |
|---|---|---|---|
| `BSP/bsp_delay/bsp_delay.c` | `__weak`（弱） | `HAL_Delay(ms)` 忙等 | 裸机 / 兜底 |
| `APP/app_delay.c` | 强 | `vTaskDelay(pdMS_TO_TICKS(ms))` | 上了 FreeRTOS |

`BSP/bsp_delay/bsp_delay.h` **只 `<stdint.h>`**，一个 FreeRTOS 的头都不引：

```c
void BSP_DelayMs(uint32_t ms);
```

BSP 里的弱定义（`BSP/bsp_delay/bsp_delay.c`，逐字）：

```c
/* __weak 是 AC5 的编译器关键字, 不是 C 标准的东西.
 * ST 的 HAL 自己也是这么干的 —— 见 stm32f1xx_hal.c:371
 *     __weak void HAL_Delay(uint32_t Delay)
 * 链接器看到同名强定义时优先用强的, 既不报错也不警告. */
__weak void BSP_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}
```

APP 里的强定义（`APP/app_delay.c:29-52`；`xTaskGetSchedulerState()` 就藏在这儿，
**FreeRTOS 的头也只在这个文件里出现**）：

```c
void BSP_DelayMs(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        /* 调度器启动前(或裸机): 没有任务可让, 只能忙等 */
        HAL_Delay(ms);
    }
}
```

**链接器规则：强符号覆盖弱符号。** 两个都加进工程 → 用 APP 那份；
只带 BSP 那份去裸机工程 → 用 `HAL_Delay`，驱动一个字都不用改。

⚠️ **代价是"忘了加文件不会报错"**：上了 FreeRTOS 却忘了把 `APP/app_delay.c`
加进工程，走弱定义照样能链接通过，只是延时悄悄变回忙等 ——
这是个**要记在心里的**配置项，编译器拦不住。

**`xTaskGetSchedulerState()` 有前提**：`FreeRTOSConfig.h` 里必须打开
`INCLUDE_xTaskGetSchedulerState`。**本工程已经开了**
（`freertos/inc/FreeRTOSConfig.h:685`，值 1），所以上面这段代码**可以直接用，不用改配置**。

（如果没开，编译会报未定义符号，加上 `#define INCLUDE_xTaskGetSchedulerState 1` 即可。）

**然后驱动里三处改动**：

```c
/* esp8266.c  ESP8266_Reset() */
BSP_DelayMs(100);      /* 原来是 HAL_Delay(100)  */
BSP_DelayMs(1000);     /* 原来是 HAL_Delay(1000) */

/* esp8266.c  ESP8266_Init() */
BSP_DelayMs(1000);     /* 原来是 HAL_Delay(1000) */
```

> 💡 **当时还想顺手修一个遗留问题 —— 结果没改**：`Reset()` 里已经等了 1000ms，
> `Init()` 里又等 1000ms（详见《ESP8266_代码思路笔记》第七章 #1）。
> 当时建议"既然要改，不如把 `Init()` 里那次删掉，开机快 1 秒"，
> **实际实现时保留了**（`esp8266.c:195` 那句 `BSP_DelayMs(1000);` 还在，
> 注释写的是"等待ESP8266启动"）。开机多 1 秒，换来复位后更稳，就这么留着了。

**为什么包装函数比"直接改成 vTaskDelay"好？**

因为驱动的**正确性不再依赖调用顺序**。以后有人把 `ESP8266_Init()`
挪到任务里调用，代码照样正确 —— 不用回来改驱动。
**驱动的职责是"能工作"，不是"要求调用者按某种顺序调我"。**

### 3.2 修改点 2 是重点，单独放下一节讲

### 3.3 修改点 2：接收 —— 这里加延时**不管用**

> ⚠️ **这是本文最重要的一节。** 很多教程说"阻塞改非阻塞，加个 `vTaskDelay` 就行"
> —— 对串口轮询接收**不成立**。

#### 3.3.1 先看一个"想当然"的改法

> 下面这段是 **DMA 改造之前**的 `WaitResponse()`（简化），当时的问题就出在它身上。
> **现在它已经不长这样了** —— 见 3.3.4 末尾的"实际落地"。

改造前的 `WaitResponse()` 长这样（简化）：

```c
while ((HAL_GetTick() - startTick) < timeout)
{
    if (HAL_UART_Receive(&ESP8266_UART, &ESP8266_RxBuf[ESP8266_RxLen], 1, 10) == HAL_OK)
    {
        ESP8266_RxLen++;
        ... 匹配响应 ...
    }
    /* ← 有人会想在这里加一句 vTaskDelay(1) 让出 CPU */
}
```

**看着很合理**：加个延时让出 CPU，不就 FreeRTOS 友好了吗？

**不行。这样会丢数据。**

#### 3.3.2 算一笔账

| 项 | 值 |
|---|---|
| 波特率 | 115200 |
| 1 字节耗时 | 10 bit ÷ 115200 ≈ **87µs** |
| `vTaskDelay(1)` 的实际效果 | **1ms**（tick 是 1ms） |
| 1ms 内能来多少字节 | 1000 ÷ 87 ≈ **11.5 字节** |

**问题在于：USART 在硬件层面只有一个字节的接收缓冲。**

```
USART 接收数据寄存器 (DR) —— 只有 1 个字节的空间！

如果 CPU 没及时把它读走, 下一个字节来了 → 硬件只能丢弃旧的
                                     → 置位 ORE (溢出错误)
```

**所以：如果每收 1 字节就让出 1ms，中间来的 11 个字节有 10 个会被硬件丢掉。**

> 这就是《ESP8266_代码思路笔记》8.5 说的 ORE 机制。
> 在那里它是"空档期丢数据"的原因，在这里它变成了
> **"轮询接收架构无法 FreeRTOS 化"的根本障碍**。

#### 3.3.3 结论：必须换架构，不能打补丁

| 方案 | 可行性 | 原因 |
|---|---|---|
| 轮询 + `vTaskDelay` 让出 | ❌ | 每让出 1ms 丢 ~11 字节 |
| 轮询 + 减小让出时间 | ❌ | tick 最小就是 1ms，没法再小 |
| 轮询 + `taskYIELD()` | ⚠️ | 不延时，只是让同优先级任务轮转，**CPU 还是满的** |
| **DMA + IDLE + 信号量** | ✅ | **当时认为的"唯一正解"** —— 方向对（换 DMA），落点不同（最终没用 IDLE、没用信号量，见 3.3.4） |
| **DMA 环形 + 轮询 CNDTR** | ✅ | **最终采用的**，空档期让出 CPU 靠 `BSP_DelayMs(1)` |

**结论没变**：必须在接收架构上换 DMA，在轮询上打补丁（加延时 / 让出）救不了。
变的是"**怎么知道收了多少字节**"——预想用 IDLE 中断通知，实际用轮询 DMA 的
剩余计数 `CNDTR`。

**`taskYIELD()` 那一行值得说明**：它确实能让同级任务轮转（CPU 不那么"独占"了），
但**没有任何一个时刻是空闲的** —— 总有人在空转。
这解决不了"CPU 白烧"的问题，只是把白烧的时间分给了别人。

#### 3.3.4 当时设想的正解：DMA + IDLE + 信号量（**未采用**）

> ⚠️ 这一节记的是**当时的设计**。它没被采用 —— 实际落地的是 3.3.4.1 那套。
> 保留它是因为"为什么当时这么想、后来为什么不这么干"本身就是笔记的价值。

**完整方案已经写在《ESP8266_代码思路笔记》第八节**，
包括四步改造骨架、`USART2_IRQn` 没使能的坑、以及为什么 Normal 模式必须重新武装。

**这里只补充 FreeRTOS 相关的那一步** —— 把标志换成信号量：

```c
/* ---- esp8266.c 新增 ---- */
static SemaphoreHandle_t xEspRxSem = NULL;   /* 接收完成信号量 */

/* 初始化时创建 (在 ESP8266_Init 里, 调度器启动前后都能建) */
xEspRxSem = xSemaphoreCreateBinary();
```

```c
/* ---- 中断回调里: 用 FromISR 版本 ---- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != ESP8266_UART.Instance) return;

    BaseType_t woken = pdFALSE;

    if (ESP8266_RxLen + Size < ESP8266_RX_BUF_SIZE)
    {
        memcpy(&ESP8266_RxBuf[ESP8266_RxLen], ESP8266_DmaBuf, Size);
        ESP8266_RxLen += Size;
    }
    xSemaphoreGiveFromISR(xEspRxSem, &woken);      /* ← 唤醒等待的任务 */

    HAL_UARTEx_ReceiveToIdle_DMA(&ESP8266_UART, ESP8266_DmaBuf, ESP8266_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);

    portYIELD_FROM_ISR(woken);     /* ← 如果唤醒了更高优先级任务, 立刻切 */
}
```

```c
/* ---- 任务里: 等待期间任务真正睡下, CPU 0 占用 ---- */
ESP8266_Status ESP8266_WaitResponse(const char *expected, uint32_t timeout)
{
    while (xSemaphoreTake(xEspRxSem, pdMS_TO_TICKS(timeout)) == pdTRUE)
    {
        if (strstr((char *)ESP8266_RxBuf, expected) != NULL) return ESP8266_OK;
        if (strstr((char *)ESP8266_RxBuf, "ERROR") != NULL ||
            strstr((char *)ESP8266_RxBuf, "FAIL")  != NULL) return ESP8266_ERR_AT;
        if (ESP8266_RxLen >= ESP8266_RX_BUF_SIZE - 1)       return ESP8266_ERR_BUF_FULL;
        /* 没匹配上就继续等下一批数据, 总超时由调用方控制 */
    }
    return ESP8266_ERR_TIMEOUT;
}
```

> ⚠️ `xSemaphoreTake` 的 timeout 是**每次**的，不是总的。
> 上面这个写法每次循环都会重置超时，**实际总超时会超过传入的 timeout**。
> 严格做法是自己用 `xTaskGetTickCount()` 记账（见 3.3.5）。

**收益**（最终实现也拿到了这些）：

| 项目 | 轮询 | DMA 接收 |
|---|---|---|
| 连 WiFi 那 15 秒 | CPU **~100% 空转** | CPU 大部分时间在睡（每 1ms 醒一次看一眼） |
| 其他任务 | **全被饿死** | 正常运行 |
| `+IPD` 主动上报 | 丢 | 能收 |

#### 3.3.4.1 实际落地的方案：DMA 环形 + 轮询 CNDTR（无 IDLE、无信号量）

**最后走的是另一条路**：DMA 开环形模式自己收，**用轮询 DMA 剩余计数的方式
知道收了多少**，一个 USART2 中断都不开 —— 也就没有 `FromISR`、没有信号量。

| | 3.3.4 设想的 | 实际落地 |
|---|---|---|
| DMA 模式 | 环形 | **环形**（`DMA_CIRCULAR`，`Core/Src/usart.c:152`）✅ 一致 |
| 怎么知道"收了多少" | IDLE 空闲中断 | **轮询 `CNDTR`**（`esp8266.c:79-107`）|
| USART2 中断 | 必须使能（优先级 5） | **不开** |
| 与 RTOS 交互 | `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR` | **无信号量、无 `FromISR`** |
| 任务怎么等 | `xSemaphoreTake(..., timeout)` 睡下 | `BSP_DelayMs(1)` 睡一个 tick 再看一眼 |

**为什么不走 IDLE 中断**（`BSP/ESP8266/esp8266.c:26-30` 的原文解释）：

> 为什么不用 IDLE 中断判"一帧收完了"?
>   那要多开一个 USART2 中断, 还得纠结"中断里能不能调 FromISR 的 RTOS API"
>   —— 一碰 RTOS, BSP 就破了"不认识 FreeRTOS"这条规矩(见 bsp_delay.h).
>   轮询的代价只是最多 1ms 的发现延迟, 而 AT 响应本身都是几十毫秒级的,
>   完全够用. 少一个中断, 少一堆麻烦.

**这一条很关键**：中断方案要往 BSP 里塞 `FreeRTOS.h`（`FromISR` 系列），
而 BSP 层"不认识 FreeRTOS"是这个项目的底线 ——
跟 3.1 最后选弱/强符号是同一个取向。

**现在的 `WaitResponse()` 长这样**（`esp8266.c:241-288`，节选）：

```c
ESP8266_Status ESP8266_WaitResponse(const char *expected, uint32_t timeout)
{
    uint32_t startTick = HAL_GetTick();

    while ((HAL_GetTick() - startTick) < timeout)
    {
        /* 把 DMA 收上来的字节搬进来. 一次搬一批, 比原来"一个字节判一次
         * strstr"高效得多, 语义没变 —— 凑齐 expected 就返回. */
        if (ESP8266_RxDrain() > 0)
        {
            // 先判响应, 再判缓冲满。顺序不能反: 若最后一个字节恰好凑齐响应,
            // 先判满就会把成功误报成失败。
            if (strstr((char *)ESP8266_RxBuf, expected) != NULL)
            {
                return ESP8266_OK;
            }
            ...
        }
        else
        {
            /* 没有新字节: 让出 CPU. ... */
            BSP_DelayMs(1);
        }
    }

    return ESP8266_ERR_TIMEOUT;
}
```

**改动前后对照**：

| | 改造前 | 改造后 |
|---|---|---|
| 怎么收字节 | `HAL_UART_Receive(..., 1, 10)` 逐字节，**CPU 全程忙等** | DMA 硬件搬进环形缓冲区，**CPU 不参与** |
| 空档期（没数据） | 每次都等满 10ms 的轮询窗口 | `BSP_DelayMs(1)` —— 任务里就是 `vTaskDelay`，**让出 CPU** |
| 关中断 4ms（DHT11）期间 | 字节没人搬就丢 | DMA 照搬，**一个不丢** |
| 缓冲区满 | 报 `ERR_TIMEOUT`（明明是收满了） | 报 `ERR_BUF_FULL` |

> 逐行原理见《ESP8266_代码思路笔记》和 `esp8266.c` 文件头的长注释。

#### 3.3.5 一个容易忽略的细节：超时记账

> 这一节是给**信号量方案**写的。最终实现没用信号量，所以 `xSemaphoreTake`
> 那套不适用 —— 但**"总超时要自己记账、别让每次等待各自计时"这个道理仍然成立**，
> 只是记账的工具换成了 `HAL_GetTick()`：现在的 `ESP8266_WaitResponse()` 和
> `ESP8266_TCPRecvRaw()` 都是进函数先取 `startTick`，循环条件里做无符号相减。

信号量版本的 `xSemaphoreTake` 每次调用的超时是**独立**的。
上面那个循环如果一直收不到数据、每次都等满 `timeout`，总耗时会远超预期。

**正确做法**：自己用 tick 记账。

```c
TickType_t startTick = xTaskGetTickCount();
TickType_t deadline  = pdMS_TO_TICKS(timeout);

while ((xTaskGetTickCount() - startTick) < deadline)
{
    TickType_t remain = deadline - (xTaskGetTickCount() - startTick);
    if (xSemaphoreTake(xEspRxSem, remain) != pdTRUE) break;   /* 剩余时间等完都没来 */
    ... 匹配 ...
}
```

**注意 `(xTaskGetTickCount() - startTick)` 的写法** ——
和《代码思路笔记》2.② 里 `HAL_GetTick()` 那处是**同一个道理**：
无符号相减天然处理回绕，写成 `now < start + timeout` 就会在回绕时出错。

### 3.4 修改点 3：互斥量保护共享状态（**未实施**）

> ⚠️ **这一节整节都还是"建议"，代码里没有互斥量** —— `esp8266.c` / `mqtt.c` 里
> 一个 `xSemaphore*` 都没有。**原因不是"忘了"，是暂时不需要**：
> 现在**只有一个任务碰 ESP8266**（`TaskWifi`；`mqtt.c` 也是被它调的），
> 下面 3.4.1 说的那种"两个任务同时用"的场景还不存在。
> 哪天再开一个任务去用 ESP8266（比如"服务器下发指令"），
> **回来把这一节当成待办清单**：递归互斥量 + 公开 API 入口统一加解锁。

#### 3.4.1 问题在哪

ESP8266 驱动的状态是**模块级共享变量**：

```c
static uint8_t  ESP8266_RxBuf[ESP8266_RX_BUF_SIZE];
static uint16_t ESP8266_RxLen = 0;
```

裸机下这没问题 —— 只有一个执行流，不会有人"同时"用。

**FreeRTOS 下就不一样了**。假设你有两个任务：

```
任务A (传感器上报, 优先级 3)          任务B (服务器指令处理, 优先级 2)
   │                                      │
   ├─ ESP8266_TCPSend("temp=25")          │
   │    ├─ ClearRxBuf()   ← 清空           │
   │    ├─ 发 "AT+CIPSEND=7"              │
   │    │        ↑ 切到这里, A 被抢占      │
   │    │                                 ├─ ESP8266_TCPSend("led=on")
   │    │                                 │    ├─ ClearRxBuf()  ← 又清空!
   │    │                                 │    ├─ 发命令
   │    ▼                                 │    └─ 收到自己的响应, 返回 OK
   │  继续等 ">" ...                      │
   │  ← 缓冲区已被 B 清空/污染             │
   │  ← A 的响应可能被 B 抢占消费掉        │
   └─ 结果: 两个任务都拿到错乱的响应       │
```

**这类 bug 的特点**：偶发、难复现、现象随机（有时 A 错、有时 B 错）。
比逻辑错误难查十倍。

#### 3.4.2 用互斥量，但必须是**递归**的

```c
/* esp8266.c 新增 */
static SemaphoreHandle_t xEspMutex = NULL;

/* ESP8266_Init() 里创建 */
xEspMutex = xSemaphoreCreateRecursiveMutex();
```

**为什么必须是递归互斥量？**

因为 ESP8266 的公开 API 是**嵌套调用**的：

```
ESP8266_TCPSend()          ← 公开 API
   ├─ ESP8266_SendCmd()    ← 也是公开 API
   └─ ESP8266_WaitResponse() ← 也是公开 API
```

如果两边都加锁：

| 互斥量类型 | 结果 |
|---|---|
| 普通互斥量 | **自己把自己锁死（死锁）** ❌ |
| **递归互斥量** | 同一任务重复获取，计数 +1，正常通过 ✅ |

```c
/* 公开 API 的入口统一加锁/解锁 */
ESP8266_Status ESP8266_TCPSend(const uint8_t *data, uint16_t len)
{
    if (xSemaphoreTakeRecursive(xEspMutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP8266_ERR_AT;
    }

    ... 原有逻辑 ...

    xSemaphoreGiveRecursive(xEspMutex);
    return status;
}
```

**确认配置已开**（我查过了，两项都是 1）：

```c
#define configUSE_MUTEXES                       1    /* FreeRTOSConfig.h:666 */
#define configUSE_RECURSIVE_MUTEXES             1    /* FreeRTOSConfig.h:667 */
```

> ⚠️ **加锁后有个副作用要注意**：`ESP8266_ConnectWiFi()` 最长阻塞 15 秒。
> 这 15 秒里**互斥量一直被占着** —— 其他任务想调 ESP8266 就得等。
> 这是**正确的**（串口本来就该串行使用），但要意识到
> "一个任务连 WiFi 时，别的任务用不了 ESP8266"。

#### 3.4.3 只读访问要不要加锁？

`ESP8266_GetRxBuf()` / `ESP8266_GetRxBufLen()` 是给调试用的只读接口
（**现在的调用者是 `APP/task_wifi.c`**，连 WiFi / 连 TCP 失败时把模块的原始应答
打出来：`task_wifi.c:156`、`:182`）。严格来说也应该加锁，
但调试代码对一致性要求低，**可以不加**，避免死锁风险。

**如果决定不加，至少加个注释说明**，否则后来者会以为是漏了。

### 3.5 修改点 4：任务栈大小 ✅ 已实施

**ESP8266 驱动的栈消耗不低**：

| 位置 | 栈上占用 |
|---|---|
| `ESP8266_ConnectWiFi()` | `char cmd[128]` = 128 字节 |
| `ESP8266_TCPConnect()` | `char cmd[128]` = 128 字节 |
| `snprintf` 内部 | 几十字节 |
| `printf` 内部 | 可观（格式解析） |

**`configMINIMAL_STACK_SIZE = 128` 是 words，不是字节。**
在 CM3 上 `StackType_t` = `uint32_t`，所以：

```
configMINIMAL_STACK_SIZE = 128  →  128 × 4 = 512 字节
```

**光 `cmd[128]` 就吃掉 1/4**，再算上函数调用链和中断嵌套，
**512 字节非常紧张。**

**建议**：256 words 起步（= 1024 字节）。**实际取的是 512 words**
（`APP/app_main.h:33`），因为联网阶段的真实用量比估算高 ——
按 `uxTaskGetStackHighWaterMark()` 打出来的余量往上加：

```c
/* APP/app_main.h:33 —— 实际用的值 */
#define APP_STACK_WIFI          512      /* words, = 2048 字节 */
```

```c
/* APP/task_wifi.c:201 */
xTaskCreate(TaskWifi, "WiFi", APP_STACK_WIFI, NULL,
            APP_PRIO_WIFI, NULL);
```

**怎么验证够不够？** 打开栈溢出检查：

```c
/* FreeRTOSConfig.h */
#define configCHECK_FOR_STACK_OVERFLOW   2    /* 当前是 0, 建议改成 2 */
```

然后实现回调，溢出时打印是哪个任务：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack overflow: %s\r\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;);
}
```

> 方法 2 会在每次任务切换时检查栈末尾的填充字节（`0xA5`）有没有被改写，
> **能抓出"已经溢出但还没造成破坏"的临界状态**，比方法 1 更早发现。

### 3.6 修改点 5：`printf` 不是线程安全的（**未实施**）

`Core/Src/main.c:170-174`（逐字）：

```c
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
```

**两个问题**：

**① 非线程安全**：`printf("Temp: %d\r\n", t)` 内部会分多次调 `fputc`。
两个任务同时 printf，字符会**交叉穿插**：

```
任务A 想说: Temp: 25 C
任务B 想说: Humi: 60 %
实际输出:   TeHmuip::  2650C %      ← 乱码
```

**② `HAL_MAX_DELAY` 是永不超时**：如果串口硬件故障（TX 脚短路、
或者打印量太大塞满了发送），**这个任务会永久卡死在这里**。
在 FreeRTOS 下，卡死一个任务比裸机更严重 —— 它占着 CPU。

**方案（按推荐度排序）**：

| 方案 | 做法 | 评价 |
|---|---|---|
| **少用 printf** | 只在初始化和错误路径用 | ✅ **最推荐** |
| 加互斥量 | `fputc` 里加锁 | 可以，但每次 1 字节加锁开销大 |
| 改成 DMA 发送 | 像接收那样上 DMA | 最彻底，但工作量大 |
| 换 SEGGER RTT | 用 J-Link 的 RTT 输出 | 不走串口，速度极快，**调试首选** |

**最实用的**：**把 `HAL_MAX_DELAY` 改成有限值**，至少不会永久卡死：

```c
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);   /* 100ms 超时 */
    return ch;
}
```

> ⚠️ **这一条最终没做** —— `Core/Src/main.c:172` 现在还是 `HAL_MAX_DELAY`。
> 代码里留着提醒（`main.c:167-168` 的注释指着本节）：
> *"正常带 FreeRTOS 的工程会改成有限超时, 见《FreeRTOS驱动适配笔记》3.6."*
> 也就是说：**"少用 printf"那条做到了**（输出量不大），
> 但"给 `fputc` 加超时"这个更彻底的保险还没上 —— 算个待办。

### 3.7 优先级怎么定

`configMAX_PRIORITIES = 5`，可用优先级 **0~4**（数字越大优先级越高）。

**当时的建议**（⚠️ **与实际采用的值正好相反**，别照抄这一版）：

| 任务 | 优先级 | 理由 |
|---|---|---|
| DHT11 采集 | **4**（最高） | 位流时序敏感，**必须不能被抢占**（见 4.4） |
| ESP8266 通信 | 2 | 大部分时间在等，不占 CPU |
| 其他业务逻辑 | 1~2 | — |
| 空闲任务 | 0 | FreeRTOS 自动创建 |

**实际采用的**（`APP/app_main.h:24-25`，逐字）：

```c
#define APP_PRIO_WIFI           3
#define APP_PRIO_DHT11          2
```

| 任务 | 优先级 | 理由 |
|---|---|---|
| 定时器服务任务 | 4 | FreeRTOS 占用（`configTIMER_TASK_PRIORITY`，见下） |
| **WiFi 通信** | **3**（`APP_PRIO_WIFI`） | **网络有超时约束**：AT 应答有超时、MQTT 有 Keep Alive，晚一步就重传/掉线 |
| **DHT11 采集** | **2**（`APP_PRIO_DHT11`） | 周期采样，晚一点无所谓 |
| 空闲任务 | 0 | FreeRTOS 自动创建 |

**为什么和当时的建议反过来了？**

- 当时只盯着"时序敏感"这一条：DHT11 的采样窗口是几十微秒级，怕被抢占打乱时序
- 但**DHT11 的驱动本来就抗抖动** —— 它测的是高电平宽度、门限 45µs，
  两侧各有约 17µs 余量（见 4.4），被抢占一下不至于判错位
- 而 **WiFi 那边是真有硬约束**：网络超时是实打实的，超了就重传、掉线
- **高优先级不会饿死低优先级**：WiFi 任务绝大多数时间阻塞在 `vTaskDelay` /
  `xQueueReceive` 上，处于**阻塞态**而不是就绪态，调度器根本不看它
  （`APP/task_wifi.c` 的文件头注释也是这么写的）

**两条原则（没变，只是"谁有约束"要按实际算）**：

1. **有硬时序 / 硬超时约束的任务优先级要高** —— 但别想当然地认定"谁最敏感"，
   要按实际跑出来的现象定
2. **长时间阻塞的任务优先级低一点没关系** —— 反正它大部分时间在等

> ⚠️ **注意 `configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1 = 4`**
> （`freertos/inc/FreeRTOSConfig.h:230`），而且 **`configUSE_TIMERS = 1`**
> （`freertos/inc/FreeRTOSConfig.h:223`）—— 也就是说**软件定时器任务是真实存在的，
> 它已经占了最高级 4。**
>
> 当时基于"DHT11 要占 4"提过一个建议：把 `configTIMER_TASK_PRIORITY` 降到
> `( configMAX_PRIORITIES - 4 )`（= 1），把 4 让出来。**现在不需要了** ——
> 实际是 WiFi 3 / DHT11 2，最高级 4 留给（目前没用上的）定时器任务，三边不打架。
>
> （顺手也可以把 `configUSE_TIMERS` 改成 0 省一点 RAM —— 但改动配置有风险，
> **接入 FreeRTOS 的第一轮先别动**，等系统跑起来再说。这条现在也还没做。）

---

## 四、DHT11 需要改什么

> DHT11 的代码整体写得很好（读时序那套"测高电平宽度"的方案很扎实），
> **但它和 FreeRTOS 有一个硬冲突。**

### 4.1 🔴 致命问题：DHT11 和 FreeRTOS 抢 SysTick

#### 4.1.1 两个"主人"对 SysTick 的要求完全不同

**FreeRTOS 的要求**（`freertos/port/port.c:717-718`）：

```c
portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG |
                              portNVIC_SYSTICK_INT_BIT |
                              portNVIC_SYSTICK_ENABLE_BIT );
```

代进本工程的值（都在 `FreeRTOSConfig.h`）：

| 项 | 值 |
|---|---|
| `configCPU_CLOCK_HZ` | 72000000（第 56 行） |
| `configTICK_RATE_HZ` | 1000 |
| `configSYSTICK_CLOCK_HZ` | 未单独定义 → 默认等于 CPU 时钟 |
| **LOAD** | 72000000 / 1000 − 1 = **71999** |
| **INT_BIT** | **开**（必须有，否则调度器收不到 tick） |

**DHT11 当时的要求**（**改造前**的 `dht11.c`；这段代码已经换成 DWT 了，见 4.1.4）：

```c
SysTick->LOAD = DHT11_STK_MASK;    /* 0x00FFFFFF = 16777215 */
SysTick->VAL  = 0UL;
SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;  /* 无 INT */
```

| 项 | 值 |
|---|---|
| **LOAD** | **16777215** |
| **INT_BIT** | **关**（故意的，只当计数器用） |

**对比一下：**

| 项 | FreeRTOS 要 | DHT11 要 | 能共存吗 |
|---|---|---|---|
| `LOAD` | 71999 | 16777215 | ❌ **冲突** |
| `TICKINT` | 开 | 关 | ❌ **冲突** |

**SysTick 只有一个，两个主人要的配置完全不同。**

#### 4.1.2 为什么 LOAD 的值不能将就

可能有人想："都用 71999 不就行了，慢一点无所谓" —— **不行。**

看**改造前** `dht11.c` 里的核心公式（现在也换成 DWT 版了，见 4.1.4）：

```c
__STATIC_INLINE uint32_t DHT11_Elapsed(uint32_t start)
{
    return (start - DHT11_Ticks()) & DHT11_STK_MASK;   /* 掩码 24 位 */
}
```

**这个公式成立的前提是：计数器在 `0xFFFFFF` 处回绕。**
SysTick 是**向下计数**的，回绕点是 `LOAD`，不是固定的 2²⁴。

**算个例子**（实际测一次应答的 200µs 窗口）：

```
真实情况:  start = 70000, 现在 current = 69900
           实际经过 = 100 个计数
```

| LOAD | `(start − current) & 0xFFFFFF` | 对不对 |
|---|---|---|
| **16777215**（DHT11 要的） | `70000 − 69900` = **100** | ✅ |
| **71999**（FreeRTOS 要的） | 如果中间跨过一次回绕：<br>`(100 − 71900) & 0xFFFFFF` = **16705416** | ❌ **差 16 万倍** |

**LOAD 一变，`DHT11_Elapsed()` 就废了** —— 所有超时判断全部失效，
`DHT11_Read()` 会一直读失败。

> 这不是"精度差一点"，是**公式前提被破坏，结果完全错误**。

#### 4.1.3 那当时为什么"看起来没事"？

因为**还没有 FreeRTOS 的时候**，`main.c` 的调用顺序是：

```c
DHT11_Init();            /* ← 设 LOAD = 0xFFFFFF, 关 TICKINT */
...
/* 还没有 FreeRTOS, 没人碰 SysTick */
while (1) { DHT11_Read(); ... }     /* 此时 SysTick 是 DHT11 的 */
```

（这段 `while(1){ DHT11_Read(); }` 现在已经没有了 —— 采集搬进了
`APP/task_dht11.c` 的 `TaskDht11`，跑在调度器之后。）

**接入 FreeRTOS 后**，`vTaskStartScheduler()` 会调 `vPortSetupTimerInterrupt()`，
**把 LOAD 改成 71999、打开 TICKINT**：

```
DHT11_Init() 设好 → ... → vTaskStartScheduler() 覆盖掉 → DHT11_Read() 全失败
                              ↑
                        从这一刻起, DHT11 就废了
```

**而且反过来也成立**：如果 `DHT11_Init()` 在调度器之后调用，
它会把 `TICKINT` 关掉 → **SysTick 中断再也不会来 → 调度器彻底停摆**
（`vTaskDelay` 永不返回、所有超时失效、系统假死）。

**两个顺序都不行。这是硬冲突，不是顺序问题。**

> 💡 **代码作者早就预见到这一点了。** 当时 `dht11.c` 里留着这么一句注释：
>
> *"注意: 如果以后把 HAL 时基改回 SysTick 或**上 FreeRTOS**, 这里要换成 TIM."*
>
> （**这条注释已经随着 4.1.4 的 DWT 改造删掉了** —— 现在 `dht11.c:32-35`
> 那个位置换成了"历史"说明，记的正是这次 SysTick 冲突。）
>
> 现在正是"上 FreeRTOS"的时候。
> （作者当时想到的方案是换 TIM；实际实施时用了更省事的 DWT，
> 不用动 CubeMX —— 见 4.1.4。）

#### 4.1.4 修法：把微秒时基换成 DWT ✅ 已实施

**思路**：SysTick 完全归 FreeRTOS，DHT11 改用 **Cortex-M3 内核自带的
DWT 周期计数器**做微秒时基。

**为什么选 DWT 而不是 TIM？**

| 方案 | 要动 CubeMX 吗 | 占硬件资源 | 分辨率 | 与 FreeRTOS 冲突 |
|---|---|---|---|---|
| **DWT**（已采用） | ❌ 不用 | ❌ 不占 | **约 13.9ns** | 无 |
| TIM2/TIM3 | ✅ 要加定时器 + 重新生成代码 | 占一个定时器 | 1µs | 无 |

DWT（Data Watchpoint and Trace）是 Cortex-M3 内核**自带的**调试组件，
里面有个 32 位周期计数器 `CYCCNT`，**每个 CPU 周期 +1**。
它不属于任何外设，所以在 CubeMX 里既配不了、也不用配 ——
**只改 `dht11.c` 就够了**，没有重新生成覆盖代码的风险。

**唯一要记住的前提**：先置 `DEMCR.TRCENA`，否则 `CYCCNT` 恒为 0。

**改动（`dht11.c` 共 5 处）：**

**① 时基变量** —— 24 位掩码不要了，32 位计数器不需要掩码：

```c
/* ---- 原来 ---- */
#define DHT11_STK_MASK        0x00FFFFFFUL          /* SysTick 24bit */
static uint32_t s_ticks_per_us = 72UL;

/* ---- 换成 ---- */
#define DHT11_CYC_PER_US_INIT 72UL                  /* 兜底值, Init 时重算 */
static uint32_t s_cycles_per_us = DHT11_CYC_PER_US_INIT;
```

**② `DHT11_TimebaseInit()`** —— 不再碰 SysTick：

```c
static void DHT11_TimebaseInit(void)
{
    s_cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000UL;

    /* TRCENA 是 DWT 的总开关, 不开的话 CYCCNT 恒为 0 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0UL;                          /* 计数清零 */
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;       /* 启动周期计数器 */
}
```

**③ `DHT11_Ticks()` / `DHT11_Elapsed()`** —— 计数方向从"向下"变"向上"：

```c
__STATIC_INLINE uint32_t DHT11_Ticks(void)
{
    return DWT->CYCCNT;                         /* 向上计数, 直接读 */
}

/* 自 start 起经过的周期数 (向上计数 + 无符号相减, 回绕安全) */
__STATIC_INLINE uint32_t DHT11_Elapsed(uint32_t start)
{
    return (DHT11_Ticks() - start);             /* 不再需要掩码 */
}
```

> ⚠️ **注意方向反了，减法顺序也要跟着反**：
> SysTick 是**向下**计数，公式是 `start - current`；
> DWT 是**向上**计数，公式是 `current - start`。
> 写反了不会报错，但算出来是错的 —— 而且往往只在某些时刻才出错。

**④ `DHT11_DelayUs()`** —— 去掉钳位：

```c
static void DHT11_DelayUs(uint32_t us)
{
    uint32_t wait  = us * s_cycles_per_us;
    uint32_t start = DHT11_Ticks();

    /* 32bit 计数器只在连续约 59s 后才回绕, 本驱动最长延时 30us, 无需钳位 */
    while (DHT11_Elapsed(start) < wait) { }
}
```

原来那句 `if (wait > DHT11_STK_MASK) { wait = DHT11_STK_MASK; }`
是因为 24 位计数器最多表示 16.7M 个计数（约 233ms）。
换成 32 位后，`us * 72` 要 `us > 5900 万`才溢出 —— **不可能发生**。

**⑤ `WaitLevel()` / `MeasureHighUs()` 里的变量改名**：

```c
uint32_t limit = timeout_us * s_cycles_per_us;   /* 原 s_ticks_per_us */
...
return DHT11_Elapsed(start) / s_cycles_per_us;   /* 同上 */
```

**回绕安全吗？**

| 项 | 值 |
|---|---|
| 计数器位宽 | 32 位 |
| 主频 | 72MHz |
| 回绕周期 | 2³² ÷ 72e6 ≈ **59.6 秒** |
| 本驱动最长窗口 | **200µs**（`DHT11_TIMEOUT_RESP`） |

**差 30 万倍，永远碰不到回绕。** 而且 `DHT11_Elapsed()` 用的是无符号相减，
就算真回绕了也能算对 —— 和《ESP8266_代码思路笔记》2.② 里
`HAL_GetTick()` 那处是同一个原理。

> **替代方案：换成 TIM2/TIM3**
> 如果以后 DWT 要挪作他用（比如接 SWO 跟踪），也可以改用 TIM：
> CubeMX 里加 TIM2，Prescaler = 72−1（1MHz），Counter Period = 0xFFFF，
> **不开中断**；然后 `DHT11_Ticks()` 改成读
> `__HAL_TIM_GET_COUNTER(&htim2) & 0xFFFF`，掩码从 `0xFFFFFF` 改成 `0xFFFF`，
> `s_cycles_per_us` 全部换成 `1`。
> **代价**：要动 CubeMX（有重新生成覆盖代码的风险），且多占一个硬件定时器。

> ⚠️ **调试注意**：`CYCCNT` 在内核被调试器暂停期间**通常停止计数**
> （具体行为随内核/调试器实现而异）。
> 这意味着**不适合用断点去调试 DHT11 的时序** —— 时间会跟着"冻结"。
> 要看真实时序请用示波器或逻辑分析仪。

### 4.2 关中断 4ms 会丢 tick

`BSP/dht11/dht11.c:163` 的 `__disable_irq()` 到 `:183` 的 `__enable_irq()`，
**关中断约 4~5 毫秒**（40 bit × ~120µs + 应答段）。

**这会关掉 SysTick → FreeRTOS 丢掉这几个 tick。**

**后果分析**：

| 影响 | 程度 | 说明 |
|---|---|---|
| 时间基准漂移 | **小** | 每 2 秒读一次，每次丢 4ms → 累积误差 0.2% |
| `vTaskDelay` 时间变长 | 小 | 每次读 DHT11 后，延时多 4ms |
| 时间片轮转失序 | 小 | 关中断期间不切换，之后恢复 |

**结论：可以接受。** 2000ms 周期里丢 4ms，对温湿度采集这种慢变量完全无所谓。

**但有两个必须注意的点：**

**① 关中断期间绝对不能调用任何 FreeRTOS API**

`BSP/dht11/dht11.c:158` 的 `BSP_DelayMs(20)` 是在 `__disable_irq()`（`:163`）
**之前**调的 —— **作者已经做对了**，注释也写清楚了（`dht11.c:155-157`）：

```c
    /* 这里用 BSP_DelayMs 而不是 HAL_Delay: 在任务里它会挂起本任务 20ms,
     * 引脚电平由硬件保持, 不需要 CPU 守着 —— 这 20ms 别的任务照跑.
     * 必须在 __disable_irq() 之外调用: 关着中断是没法阻塞的. */
```

**但改成 `vTaskDelay` 后要格外小心**：
`vTaskDelay` 会触发任务切换，**在关中断上下文里是致命的**。
改的时候确认它在 `__disable_irq()` 外面：

```c
DHT11_PinOutput();
DHT11_LOW();
BSP_DelayMs(20);          /* ← 必须在关中断之前! */
DHT11_PinInput();
DHT11_DelayUs(30);

__disable_irq();          /* ← 从这里才开始关 */
...
exit:
__enable_irq();           /* ← 到这里必须开回来 */
```

**② 如果以后有别的任务对时间敏感，要把关中断窗口缩小**

现在的方案是"整个位流期间都关"。其实这个驱动的设计
（测宽度 + 45µs 门限 + 两侧 ~17µs 余量）**已经能容忍一定抖动**，
理论上可以只在"测高电平宽度"那一小段关中断。
但**改动风险大于收益**，建议先保持现状。

### 4.3 `HAL_Delay(1000)` 和 `HAL_Delay(20)` ✅ 已实施

和 ESP8266 一样的问题，同样用 `BSP_DelayMs()` 替换（代码里已经换好了）：

```c
/* BSP/dht11/dht11.c:139  DHT11_Init() */
BSP_DelayMs(1000);        /* 原来是 HAL_Delay(1000) */

/* BSP/dht11/dht11.c:158  DHT11_Read() */
BSP_DelayMs(20);          /* 原来是 HAL_Delay(20), 必须在 __disable_irq() 之前 */
```

**精度够吗？** DHT11 要求起始信号拉低 **≥18ms**。

| 项 | 值 |
|---|---|
| tick 周期 | 1ms（`configTICK_RATE_HZ = 1000`） |
| `pdMS_TO_TICKS(20)` | 20 ticks |
| 实际延时 | 20~21ms |
| 要求 | ≥18ms |

✅ **满足**。

> ⚠️ **但如果有人把 `configTICK_RATE_HZ` 改成 100 或 10**，
> tick 变成 10ms/100ms，`vTaskDelay` 的粒度就粗到不满足 DHT11 时序了。
> **改 tick 频率时记得回来检查这里。**

### 4.4 DHT11 任务优先级（当时的结论：应该最高 —— 实际没这么排）

位流总共约 4ms，采样的时间窗口是**几十微秒级**。
虽然驱动用"测宽度"降低了对精确采样的要求，
但**如果被高优先级任务抢占太久（>17µs 余量），位就可能判错**。

**当时的建议**：

```c
xTaskCreate(DhtTask, "DHT", 128, NULL, 4, NULL);   /* 优先级 4, 最高 */
```

**实际采用的是**：优先级 **2**、栈 **256 words**
（`APP/app_main.h:25` / `:34`，WiFi 通信排在它上面）：

```c
/* APP/task_dht11.c:29 */
xTaskCreate(TaskDht11, "DHT11", APP_STACK_DHT11, NULL,
            APP_PRIO_DHT11, NULL);
```

**为什么可以让步**（对应 3.7 那次反转）：驱动靠"测宽度 + 45µs 门限"
本来就有约 17µs 的两侧余量，被别的任务抢占一下不至于判错位；
而网络那边的超时是实打实的。**两条约束权衡下来，让 WiFi 排在上面。**

**如果真出现位判错**（比如以后加了更重的任务），**这条可以再翻回来** ——
判断依据是现象，不是这里的建议：

| 情况 | 做法 |
|---|---|
| 实测没出现判错 | 维持现状：WiFi 3 / DHT11 2 |
| 实测出现判错（读数频繁校验失败） | 把 DHT11 提到 4，让 WiFi 降到 3；注意定时器任务也是 4 |

---

## 五、改造顺序建议

**别一次全改。** 按下面的顺序，每步都能验证：

```
第 0 步  接入 FreeRTOS 骨架 (vTaskStartScheduler)  ← 先让系统能跑起来
   │
   │     此时先别建 ESP8266/DHT11 任务, 用一个空任务验证调度器正常
   │     验证方法: 空任务里 vTaskDelay + 翻转 LED/打印, 看是否正常
   ▼
第 1 步  🔴 修 DHT11 的 SysTick 冲突 (第四节)
   │     ← 必须最先做! 不然后面所有现象都被它污染
   │     验证: DHT11_Read() 能正常返回
   ▼
第 2 步  🟡 换 BSP_DelayMs (3.1 + 4.3)
   │     验证: ESP8266_Init() / DHT11_Init() 仍能成功
   ▼
第 3 步  🟢 加互斥量 + 建任务 + 调栈大小 (3.4 + 3.5 + 3.7)
   │     建任务 / 调栈 / 定优先级都做了; **互斥量没做** —— 串口只有 WiFi
   │     任务在用, 没有并发 (见 3.4 开头的说明)
   │     验证: 两个任务能同时工作不打架
   ▼
第 4 步  🟡 DMA 接收改造 (3.3)  ✅ 已做
   │     ← 最后做, 因为最复杂
   │     ⚠️ 这里当时写的前置是"先在 CubeMX 里使能 USART2 global interrupt (优先级 5)"
   │        —— 实际没开, 是**刻意**不开的: 最终方案是 DMA 环形 + 轮询 CNDTR,
   │        不需要任何 USART2 中断 (理由见 3.3.4.1 和 esp8266.c:26-30)
   │     验证: 连 WiFi 时 CPU 不再满载, 其他任务正常运行
   ▼
第 5 步  🟢 printf 处理 (3.6)  ← 没做, 还是 HAL_MAX_DELAY
```

**为什么 DHT11 要排在最前面（第 1 步）？**

因为它的故障现象是**"DHT11 一直读失败"**，
而 ESP8266 的问题现象是**"卡顿、任务不响应"**。
两个问题**混在一起会让排查变成噩梦** —— 你分不清是哪个引起的。

**先把确定性的硬伤修掉，再动可优化的部分。**

---

## 六、一句话总结

> **驱动代码需要改，但改的不是"逻辑"，是"等待方式"。**
>
> 全部改动可以归结为一句话：
> **把"占着 CPU 等"换成"让出 CPU 等"。**
> - `HAL_Delay` → `vTaskDelay`（靠 `BSP_DelayMs` 的弱/强符号两套实现，见 3.1）
> - 逐字节忙等收串口 → **DMA 环形缓冲 + 轮询 CNDTR**（见 3.3.4.1）
> - 无保护共享 → 互斥量（**这一条最终没做** —— 串口只有一个任务在用，见 3.4）
>
> **唯一的例外是 DHT11 的 SysTick** —— 那不是等待方式的问题，
> 是**两个模块要抢同一个硬件资源**，只能一方让路（改用 DWT）。
>
> 而且这个问题**代码作者早就写在注释里预警了**。
> 好的注释就是这样：它不解释代码在做什么，它告诉你**将来什么时候会坏**。
>
> 顺带一条教训：**设想和落地经常不是一回事**。这篇笔记里"IDLE 中断 + 信号量"
> 和"BSP 直接 include FreeRTOS"两个方案都没被采用，实际走的是更朴素、
> 但更守规矩的做法（轮询 CNDTR、弱/强符号）。设想别删 —— 但要标清楚状态。
