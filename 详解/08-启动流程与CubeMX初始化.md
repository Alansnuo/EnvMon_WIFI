# 详解 01 · 启动流程与 CubeMX 初始化

> **覆盖代码**：`Core/Src/` 六个文件约 970 行 —— `main.c`(229) / `usart.c`(213) /
> `dma.c`(62) / `gpio.c`(74) / `stm32f1xx_it.c`(253) / `stm32f1xx_hal_timebase_tim.c`(139)
> 外加 `Core/Inc/main.h`(70)
>
> **读者**：要改时钟、改引脚、加外设、或者被 CubeMX"重新生成"坑过的人。
>
> **阅读前提**：
> 1. **什么是时钟树**（HSE / PLL / AHB / APB 的关系）—— 见过 CubeMX 的时钟图即可
> 2. **中断向量表是什么** —— 知道"中断来了跳到某个函数"就行
> 3. 读一遍 [详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.1，知道 DMA 在这里存在的理由
>
> **与既有笔记的关系**：
> - 本文**不重复**《从零入门 02》讲的任务 / 队列概念，只讲"调度器启动之前发生了什么"
> - 《FreeRTOS 移植笔记》讲的是**移植过程**（为什么改 `FreeRTOSConfig.h`、
>   `port.c` 怎么接进来的）；本文讲的是**这份代码现在长什么样、为什么这么写**
> - 《FreeRTOS 驱动适配笔记》§3.1 的 `BSP_DelayMs` 设计已被弱/强符号方案取代，
>   实际实现在 `BSP/bsp_delay/` + `APP/app_delay.c`，本文 §1.8 会讲
>
> **行号说明**：本文行号对应当前提交。代码改动后行号会漂，**以函数名为准**。
> HAL 驱动的行号会随 ST 版本变化，只作定位参考。

---

## 1.1 这块代码要解决什么问题

`Core/` 里的东西有个共同特点：**它们全是 CubeMX 生成的**，
而且**只在开机那几十毫秒里跑一次**。

"跑一次"意味着两件事：

1. **调试成本极高。** 跑完就再也不执行了，没法"再跑一次看看"。
   时钟配错了，芯片根本起不来——连 `printf` 都打不出来。所以这块代码
   必须**一次写对**，而"一次写对"的前提是**知道每一行在做什么**。
2. **重新生成时会被覆盖。** 点一次 CubeMX 的 "Generate Code"，
   凡是写在 `/* USER CODE BEGIN */ ... /* USER CODE END */` 之外的东西
   **全部丢失**。这个工程有三处刻意的"反 CubeMX"改动，见 §1.10 的 checklist。

### 这份代码要建立的四件事

| 要建立的 | 靠谁 | 错了两会怎么样 |
|---|---|---|
| **时钟** | `SystemClock_Config()` | 主频不对 → 串口波特率全错 → 满屏乱码 |
| **时基** | `HAL_Init` + `HAL_InitTick`（TIM4） | `HAL_GetTick()` 不走 → **所有超时判断失效** |
| **中断能进来** | `MX_DMA_Init` + `MX_GPIO_Init` 的 NVIC | DMA 收到数据但没人处理、FreeRTOS 永远不切换任务 |
| **外设引脚** | `MX_GPIO_Init` + `HAL_UART_MspInit` | 引脚配错 → 模块不响应，而且**不会有任何报错** |

第三条要展开说一句：**"串口配好了"和"串口能收到数据"是两件事**。
`MX_USART2_UART_Init()` 配完波特率、字长、停止位之后，USART2 就已经能
收发字节了——但接收到的字节要靠 **DMA 搬走**，而 DMA 的搬运是
`MX_DMA_Init()` + `HAL_UART_MspInit()` 里那几行配的。
**这两处分在两个文件、两个函数里，而且顺序不能反**（§1.8 决策一）。

### 本文不讲什么

- FreeRTOS 的任务、队列、调度 —— 《从零入门 02》
- `App_Init()` 里建的三个东西（DHT11、队列、任务）—— [详解 04](11-任务层-上报循环.md)
- ESP8266 收到数据之后怎么解析 —— [详解 02](09-ESP8266接收架构与+IPD剥离.md)
- DHT11 的单总线时序（那是 `dht11.c` 的事，不是 `gpio.c` 的事）

---

## 1.2 整体结构

### 启动顺序全景

从复位到"任务开始跑"，一共 8 步。下面这张图是本文的目录：

```
复位
 │
 ├─① Reset_Handler → SystemInit()          ← 启动文件里, 不在 Core/ 里
 │      把时钟切回 HSI 8MHz, 关中断
 │
 ├─② main()
 │      │
 │      ├─③ HAL_Init()                      main.c:77
 │      │     ├─ FLASH 预取使能
 │      │     ├─ NVIC 分组 = PRIORITYGROUP_4
 │      │     ├─ HAL_InitTick(15) ★第一次!  此时 PCLK1=8MHz → 预分频=7
 │      │     └─ HAL_MspInit()              → AFIO/PWR 时钟 + SWJ_NOJTAG
 │      │
 │      ├─④ SystemClock_Config()            main.c:84
 │      │     ├─ HAL_RCC_OscConfig()        HSE 8M 起振, PLL ×9 = 72MHz
 │      │     └─ HAL_RCC_ClockConfig(..., FLASH_LATENCY_2)
 │      │           └─ HAL_InitTick(15) ★第二次! 此时 PCLK1=36MHz → 预分频=71
 │      │
 │      ├─⑤ MX_GPIO_Init()                  PB1(ESP RST) / PB12(DHT11)
 │      ├─⑥ MX_DMA_Init()                   DMA1 时钟 + NVIC  ← 顺序要紧
 │      ├─⑦ MX_USART1_UART_Init()           printf 出口
 │      │  MX_USART2_UART_Init()            ESP8266 出口
 │      │                                   └─ HAL_UART_MspInit() 里配 DMA 通道
 │      │
 │      ├─⑧ App_Init()                      DHT11 + 队列 + 建任务
 │      │
 │      └─⑨ vTaskStartScheduler()           从此 main() 不再返回
 │
 └─ 任务开始跑: TIM4 每 1ms 中断一次喂 HAL_GetTick,
                SysTick 每 1ms 中断一次喂 FreeRTOS
```

**★ 标出来的两次 `HAL_InitTick` 是本文最重要的一处细节**，
它是"TIM4 的预分频为什么是 71 而不是 35"的答案。见 §1.3.2。

### 文件职责

| 文件 | 行数 | 职责 | 本文哪节 |
|---|---|---|---|
| `Core/Src/main.c` | 229 | 主流程 + 时钟配置 + `fputc` + TIM4 回调 | §1.3 §1.7 §1.5 |
| `Core/Src/usart.c` | 213 | USART1/2 参数 + **DMA 通道寄存器真正配置的地方** | §1.3.5 |
| `Core/Src/dma.c` | 62 | 只有三行有内容：开时钟 + NVIC | §1.3.4 |
| `Core/Src/gpio.c` | 74 | PB1 / PB12 两个引脚 | §1.3.3 |
| `Core/Src/stm32f1xx_it.c` | 253 | 中断向量 + 三个 `#if 0` | §1.5 |
| `Core/Src/stm32f1xx_hal_timebase_tim.c` | 139 | TIM4 顶替 SysTick 做 HAL 时基 | §1.4 |
| `Core/Src/stm32f1xx_hal_msp.c` | 87 | 全局 MSP：AFIO/PWR 时钟、关 JTAG | §1.6 |

### 关键常量

| 名字 | 值 | 定义在 | 改了会影响 |
|---|---|---|---|
| `TICK_INT_PRIORITY` | **15** | `stm32f1xx_hal_conf.h:132` | TIM4 的中断优先级。**不能小于 5**（§1.8 决策四） |
| `HSE_VALUE` | 8000000 | `stm32f1xx_hal_conf.h` | 板子上的晶振频率。写错 → 波特率全错 |
| 芯片 RAM | 20 KB | `project.uvprojx:21` `IRAM(0x20000000-0x20004FFF)` | §1.9 的账本 |
| 芯片 Flash | 64 KB | `project.uvprojx:21` `IROM(0x8000000-0x800FFFF)` | 同上 |
| `Stack_Size` | `0x400` = 1024 B | `startup_stm32f103xb.s:32` | **主栈**（MSP）。和任务栈是两码事 |
| `Heap_Size` | `0x200` = 512 B | `startup_stm32f103xb.s:43` | C 库 `malloc` 的堆。项目里没人调 `malloc`，白占 |
| `configTOTAL_HEAP_SIZE` | 10240 | `FreeRTOSConfig.h:290` | FreeRTOS 自己的堆（`heap_4.c`），**和上面那个不是一回事** |
| `configPRIO_BITS` | 4 | `FreeRTOSConfig.h:324` | STM32F1 的 NVIC 只实现高 4 位优先级 |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | **5** | `FreeRTOSConfig.h:326` | 优先级数值 **≥5** 的中断才准调 `...FromISR` |

> ⚠️ **两个"堆"、两个"栈"，别混。**
> `startup_*.s` 里的 `Heap_Size`/`Stack_Size` 是 **C 运行时**的（给 `malloc` 和
> 中断/调度器前的主流程用）；`configTOTAL_HEAP_SIZE` 是 **FreeRTOS 内核**的
> （给 `xTaskCreate`/`xQueueCreate` 用）。任务跑起来之后用的是**任务自己的栈**，
> 是从 FreeRTOS 堆里切出来的，不占 `Stack_Size`。

---

## 1.3 逐段详解

### 1.3.1 `main()` 的主流程

`main.c:67-122`：

```c
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  App_Init();
  vTaskStartScheduler();

  printf("ERROR: vTaskStartScheduler() returned!\r\n");
  Error_Handler();
```

**这八行的顺序没有一行是可以挪的**，理由分散在下面五节。

最后那个 `printf` 值得注意：`vTaskStartScheduler()` 正常情况**永远不返回**
（调度器一启动，`main()` 的栈和上下文就被丢弃了）。
它只在**连空闲任务都建不起来**（堆不够）时才返回——代码里这一句是这个
错误唯一的表现形式。**看到它 = `configTOTAL_HEAP_SIZE` 不够了。**

> 这个工程的 `App_Init()` 是**同步**的：它只建对象，不做事。
> 联网的 15 秒超时、DHT11 的 1 秒初始化延时，**都不在调度器启动之前**——
> 这是刻意的，见 `app_main.c:26-33` 的注释解释。

### 1.3.2 `HAL_Init()` 与两次 `HAL_InitTick` —— 本文的核心

`HAL_Init()`（`stm32f1xx_hal.c`）做四件事，第三件是关键：

```c
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
  HAL_InitTick(TICK_INT_PRIORITY);      /* ← 15 */
  HAL_MspInit();
```

`TICK_INT_PRIORITY = 15`（`stm32f1xx_hal_conf.h:132`），
而这份工程的 `HAL_InitTick` **不是** HAL 里那个空壳弱函数——
`Core/Src/stm32f1xx_hal_timebase_tim.c` 提供了强定义（§1.4 讲它干了什么）。

#### 第一次调用：在 HSI 上

`HAL_Init()` 在 `SystemClock_Config()` **之前**执行，所以此刻的时钟还是
**复位默认值**：

```
SYSCLK = HSI = 8 MHz
AHB  ÷1  → HCLK  = 8 MHz
APB1 ÷1  → PCLK1 = 8 MHz      ← 注意是 ÷1（复位值）
APB2 ÷1  → PCLK2 = 8 MHz
```

`HAL_InitTick` 里那句分支（`stm32f1xx_hal_timebase_tim.c:61-68`）：

```c
  if (uwAPB1Prescaler == RCC_HCLK_DIV1)
  {
    uwTimclock = HAL_RCC_GetPCLK1Freq();
  }
  else
  {
    uwTimclock = 2UL * HAL_RCC_GetPCLK1Freq();
  }
```

走的是**前一个分支**：`uwTimclock = 8 MHz` → 预分频 = 8−1 = **7**。

#### 第二次调用：在 72MHz 上

`SystemClock_Config()` 里的 `HAL_RCC_ClockConfig(...)` 在**它自己的函数体最后一行**
又调了一次（`stm32f1xx_hal_rcc.c:945`）：

```c
  /* Configure the source of time base considering new system clocks settings*/
  HAL_InitTick(uwTickPrio);
```

> `uwTickPrio` 是 `HAL_InitTick` 上次调用时存进去的（`uwTickPrio = TickPriority`），
> 所以**第二次的优先级参数还是 15**，不是重新传的。这个变量存在的唯一目的
> 就是"让时钟变好之后能用同样的优先级重配一次"。

这次时钟已经全是新值了：

```
SYSCLK = HSE 8MHz × PLL9 = 72 MHz
AHB  ÷1  → HCLK  = 72 MHz
APB1 ÷2  → PCLK1 = 36 MHz     ← 变了！现在是 ÷2
APB2 ÷1  → PCLK2 = 72 MHz
```

于是走的是 `else` 分支：`uwTimclock = 2 × 36 = 72 MHz` → 预分频 = 72−1 = **71**。

```c
  htim4.Init.Period = (1000000U / 1000U) - 1U;   /* = 999 */
  htim4.Init.Prescaler = uwPrescalerValue;       /* = 71  */
```

最终 TIM4 计数时钟 = 72 MHz ÷ (71+1) = **1 MHz**，
每计满 1000 次溢出一次 = **每 1ms 一次中断**。✅

#### 那个 `×2` 是哪来的

这是 STM32 时钟树上一个很容易被忽略的规则：

> **当 APB 预分频器 ≠ 1 时，挂在该 APB 上的定时器时钟 = PCLK × 2。**

所以 TIM4（在 APB1 上）拿到的不是 36MHz，而是 72MHz。
**如果照抄"预分频 = 36−1 = 35"这个直觉算，TIM4 就会 2ms 才中断一次——
`HAL_GetTick()` 直接走慢一倍。**

这就是那行看起来莫名其妙的 `2UL *` 存在的全部理由。它不是什么优化，
是在**修正时钟树规则带来的偏差**。

> **反过来说**：`uwAPB1Prescaler == RCC_HCLK_DIV1` 那个分支之所以存在，
> 是因为 ÷1 时**没有**这个 ×2 规则，定时器时钟就等于 PCLK1。
> 两个分支合起来才覆盖全。

### 1.3.3 `SystemClock_Config()` —— 8MHz 到 72MHz

`main.c:128-161`。分两步，对应 HAL 的两个 API：

**第一步 `HAL_RCC_OscConfig()`（`main.c:136-146`）—— 让 HSE 起振、PLL 锁定：**

```c
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
```

| 字段 | 值 | 含义 |
|---|---|---|
| `HSEState` | `RCC_HSE_ON` | 用板子上那颗 **8MHz 晶振**（不是内部 RC） |
| `HSEPredivValue` | `DIV1` | HSE 进 PLL 前不分频 → PLL 输入 = 8MHz |
| `PLLMUL` | `MUL9` | **8 × 9 = 72 MHz** |
| `HSIState` | `RCC_HSI_ON` | ⚠️ 内部 RC 也开着，**虽然没在用** |

> **`HSIState = RCC_HSI_ON` 有点浪费**：HSI 一直耗电，但系统时钟源是 PLL(HSE)，
> 用不到它。CubeMX 生成代码时如果"RCC 配置里手动设了 HSI"，就会留下这一行。
> 改成 `RCC_HSI_OFF` 能省一点电——但**注意** HSI 是 HSE 起振失败时的硬件兜底
> （HSE 断了 CPU 会自动切到 HSI 继续跑），关掉就连这个兜底都没了。
> 电池供电的场景才值得动它。

**第二步 `HAL_RCC_ClockConfig(..., FLASH_LATENCY_2)`（`main.c:150-160`）—— 切时钟源：**

```c
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
```

| 总线 | 分频 | 频率 | 挂了谁 |
|---|---|---|---|
| AHB (HCLK) | ÷1 | 72 MHz | 内核、DMA、SRAM、Flash 接口 |
| APB1 (PCLK1) | **÷2** | **36 MHz** | **USART2**、TIM2~7、I2C、SPI2 |
| APB2 (PCLK2) | ÷1 | 72 MHz | **USART1**、GPIO、AFIO |

**APB1 为什么必须 ÷2？** 因为 STM32F103 规定 **APB1 最高只能跑 36MHz**
（APB2 才能到 72MHz）。÷2 不是性能取舍，是硬性上限。

**这也解释了 `MX_USART2_UART_Init` 里那个 115200 是怎么算对的**：
USART2 挂在 PCLK1 = 36MHz 上，HAL 用它去算波特率分频值。
**如果哪天把 APB1 改成 ÷1（36→72MHz），`huart2.Init.BaudRate` 那一行
一个字都不用改——HAL 会自己重算。** 但改成 ÷1 已经违反芯片上限了，别改。

#### `FLASH_LATENCY_2` 是什么

Flash 的读取速度跟不上 CPU。72MHz 下必须**插 2 个等待周期**，这是 STM32F1 的硬规定：

| SYSCLK | 等待周期 |
|---|---|
| ≤ 24 MHz | 0 |
| 24 ~ 48 MHz | 1 |
| **48 ~ 72 MHz** | **2** |

**填少了会怎么样？** 取指出错，表现为**随机 HardFault / 程序跑飞**，
而且往往在编译优化等级变化、代码布局变化之后才出现——非常难查。
CubeMX 会自动算这个值，**手改时钟频率的时候记得一起改**。

配上 `HAL_Init()` 里那句 `__HAL_FLASH_PREFETCH_BUFFER_ENABLE()`（预取缓冲），
才能让 72MHz 跑得动。

### 1.3.4 `MX_DMA_Init()` —— 只有三行

`dma.c:39-56`，去掉注释就三行：

```c
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
```

**DMA 通道本身（方向、地址、长度、模式）这里一个字都没配。**
那些在 `usart.c` 的 `HAL_UART_MspInit()` 里（§1.3.5）。

所以 `MX_DMA_Init()` 的职责只有两件事：
**① 给 DMA1 供上时钟；② 让 DMA1_CH6 的中断能进 NVIC。**

#### 为什么"供时钟"这件事决定了调用顺序

`dma.c` 的文件头注释说它是"memory to memory DMA transfers"——这是 CubeMX 的
套话，实际这里只开了个时钟。但**这个时钟是硬约束**：

```
MX_DMA_Init()          → __HAL_RCC_DMA1_CLK_ENABLE()
MX_USART2_UART_Init()  → HAL_UART_Init() → HAL_UART_MspInit()
                                             → HAL_DMA_Init(&hdma_usart2_rx)
                                                  → 往 DMA1_Channel6 的寄存器写配置
```

**顺序反了会怎样？** 在 STM32F1 上，往一个**时钟没开**的外设寄存器写值，
**写操作会被直接丢弃，不报错、不产生 HardFault**。

结果就是：`HAL_DMA_Init()` 返回 `HAL_OK`（它只是写寄存器，写完就返回成功），
USART2 也初始化成功了，程序**照常往下跑** —— 直到第一次 `HAL_UART_Receive_DMA()`
之后你发现**一个字节都收不到**，而且**没有任何错误信息**。

这就是 §1.8 决策一要单独拎出来讲的原因：**这是一个静默失败**。

#### 为什么 NVIC 优先级是 5

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`（`FreeRTOSConfig.h:326`），
意思是：**优先级数值 ≥ 5 的中断才允许调用 `...FromISR` 系列 API**。
优先级数值 0~4 的中断**禁止**碰任何 FreeRTOS API（因为它们的优先级比内核
自己的临界区还高，会破坏内核的互斥保护）。

设成 5 就是"刚刚好跨过那条线"。

> ⚠️ **但 `dma.c:47-52` 那段注释里的理由已经过时了。**
> 它说"set to 5 so the USART2 DMA receive completion can safely give a
> semaphore to a task"——**全工程没有任何一处调用 `...FromISR`**
> （`grep -rn "FromISR" APP/ BSP/ Core/` 只匹配到注释）。
> DMA 中断现在只做一件事：让 `HAL_DMA_IRQHandler` 清个标志（[详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.3.1）。
>
> 优先级 5 **本身没问题**（留着以后真要发信号量时不用改），
> 只是**理由**该更新了。而且那段注释末尾的警告
> "otherwise Generate Code reverts it to 0"**也已经不成立**——
> `.ioc:48` 里已经写死了 `NVIC.DMA1_Channel6_IRQn=true\:5\:0\:...`，
> 重新生成不会丢。

### 1.3.5 `MX_USART1/2_UART_Init()` + `HAL_UART_MspInit()`

`usart.c` 里有两个 Init 函数和一个 MspInit 回调，分工是 **HAL 的固定套路**：

| 函数 | 配什么 |
|---|---|
| `MX_USARTx_UART_Init()` | **业务参数**：波特率、字长、停止位、校验、模式 |
| `HAL_UART_MspInit()` | **硬件相关**：时钟、引脚、DMA、NVIC（MSP = MCU Support Package） |

`HAL_UART_Init()` 内部先填自己的寄存器，然后**回调** `HAL_UART_MspInit()`。
所以引脚和时钟是在 `HAL_UART_Init` 执行到一半的时候配好的。

两个口的参数**完全一样**（`usart.c:44-50` 和 `73-79` 逐字相同）：

```c
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
```

**115200-8-N-1**，和串口助手的设置对应（《从零入门 06》6.2）。

> ⚠️ **`huart1.Init.Mode = UART_MODE_TX_RX` 里的 RX 是多余的。**
> USART1 只用来 `printf` 输出，代码里**没有任何地方读 `huart1`**。
> 留着无害（引脚 PA10 会被配成输入，不占资源），但别以为串口助手
> 能往板子发命令——那是 USART2 的事，而且 USART2 被 ESP8266 占着。

#### 引脚

```
USART1:  PA9  → TX (AF_PP, 高速)      PA10 → RX (INPUT, 无上下拉)
USART2:  PA2  → TX (AF_PP, 高速)      PA3  → RX (INPUT, 无上下拉)
```

配 RX 引脚时 `GPIO_InitStruct.Pull` 是**沿用上一次设置**的——
`HAL_UART_MspInit` 里 `GPIO_InitStruct` 是个局部变量，`{0}` 初始化后
`Pull = 0 = GPIO_NOPULL`，所以 RX 引脚是**浮空输入**。

浮空输入在**空闲（无数据）时电平不定**，理论上会收到随机噪声。
实际上 USART 的起始位检测（下降沿）会滤掉大部分，而且
**ESP8266 的 TX 引脚在模块上电后一直驱动着**，不是真的悬空，所以没事。
但如果哪天上电顺序不对、模块还没起来，浮空可能产生一个假的起始位——
表现为**偶尔收到一个 `0x00`**。要根治就把 RX 引脚改成 `GPIO_PULLUP`。

#### DMA 通道的真正配置

`usart.c:146-159`，在 `HAL_UART_MspInit` 的 USART2 分支里：

```c
    hdma_usart2_rx.Instance = DMA1_Channel6;
    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
```

逐条翻译：

| 字段 | 值 | 意思 |
|---|---|---|
| `Direction` | PERIPH_TO_MEMORY | 从串口搬到内存（接收方向） |
| `PeriphInc` | DISABLE | **外设地址不动** —— 永远是 `USART2->DR` 这一个寄存器 |
| `MemInc` | ENABLE | **内存地址递增** —— 一个字节一个字节往后填 |
| `PeriphDataAlignment` | BYTE | 外设侧按字节搬 |
| `MemDataAlignment` | BYTE | 内存侧也按字节 |
| `Mode` | **CIRCULAR** | **循环模式** —— 搬完 256 个自动回绕，永不停 |
| `Priority` | LOW | 通道仲裁优先级（这里只有一个通道在用，无所谓） |

**`DMA_CIRCULAR` 是整个接收架构的地基**，`Priority` 反而是无关紧要的一项。
循环模式的含义：DMA 的计数器（CNDTR）减到 0 时**自动重装成初值**、
地址回到起点，**不需要 CPU 干预，也永远不会"结束"**。

于是驱动才能靠"读当前 CNDTR 反推写到哪了"来判断数据（[详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.3.2）。
**改成 `DMA_NORMAL` 模式，整个 `esp8266.c` 的接收层就废了** —— 跑满 256 字节
DMA 就停下，之后一个字节都进不来，而且同样**不报错**。

最后一行是 HAL 的关联动作：

```c
    __HAL_LINKDMA(uartHandle, hdmarx, hdma_usart2_rx);
```

它把 `hdma_usart2_rx` 挂到 `huart2.hdmarx` 上。**没有这一句，
`HAL_UART_Receive_DMA()` 会直接返回错误**（`huart->hdmarx == NULL`）。

### 1.3.6 `MX_GPIO_Init()`

`gpio.c:42-69`。三件事：

```c
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  ...
  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
```

#### 先写电平，再配模式 —— 顺序是刻意的

注意 `HAL_GPIO_WritePin(...RESET)` 出现在 `HAL_GPIO_Init()` **之前**。

这是 CubeMX 的标准写法，目的是**避免切换成输出的瞬间产生一个毛刺**
（输出寄存器 ODR 复位值是 0，但"先配成输出"那一刻引脚会立刻反映 ODR 的旧值；
先把 ODR 写成期望值再切输出，就不会有中间态）。

代价是：**上电到 `App_Init()` 之间，这两个引脚都是低电平。**

| 引脚 | 低电平意味着 |
|---|---|
| **PB1**（ESP8266 RST） | **模块被按住复位** —— 从开机到 `ESP8266_Reset()` 释放它为止 |
| **PB12**（DHT11 DATA） | 数据线被拉低（开漏输出 = 主动拉低） |

PB1 这个副作用**正好是想要的**：模块上电后被"按住"几百毫秒，
等到 `ESP8266_Init()` 里做一次完整的硬复位，时序干净。
**反过来说**：如果你把这两行改成 `GPIO_PIN_SET`，`ESP8266_Reset()` 的行为
会变（模块可能已经自己启动了）——**别改**。

#### PB1 推挽，PB12 开漏 —— 为什么不一样

| 引脚 | 模式 | 为什么 |
|---|---|---|
| PB1 | `OUTPUT_PP`（推挽） | 复位脚是**纯输出**，永远不需要读回来。推挽驱动能力强，边沿干净 |
| PB12 | `OUTPUT_OD`（开漏） | DHT11 是**单总线双向**：主机要能"拉低"也要能"放开"。开漏输出 + 外部上拉 = 拉低时输出 0，写 1 时靠上拉拉高 |

**DHT11 那条线的实现方式是：**
`OUTPUT_OD` 输出 0 = 拉低；输出 1 = 高阻（被外部上拉电阻拉高）。
驱动读数据时**要把引脚切成输入模式**才能读到从机的电平——
这个切换在 `dht11.c` 里做，不在 `gpio.c` 里。

> ⚠️ **`GPIO_InitStruct.Pull = GPIO_NOPULL` —— 没有开内部上拉。**
> DHT11 必须靠**外部** 4.7k~10k 上拉电阻。
> 买"**DHT11 模块**"（三脚小板）的话，板子上已经带了；
> 买"**DHT11 裸传感器**"（四脚、蓝色网格壳）的话**必须自己接一个**，
> 否则读出来全是超时。
>
> 另一种改法是给 PB12 配 `GPIO_PULLUP`（F1 的内部上拉约 40kΩ）——
> **太弱**，DHT11 的时序要求边沿足够陡，40k 拉不起来。**还是得接外部电阻。**

#### `GPIOD` 的时钟是白开的

`gpio.c:48` 打开了 **GPIOD** 的时钟，但整个函数里**没有一个 PD 引脚的配置**，
`.ioc` 里也搜不到 GPIOD。这是 CubeMX 早期选过又删掉的引脚留下的残留。

代价：一点点功耗。**没有任何功能影响。** 想清理可以直接删那一行
（CubeMX 不会再生成它，因为 `.ioc` 里没有）。

---

## 1.4 时基：TIM4 是怎么顶掉 SysTick 的

这是 `Core/` 里最"绕"的一块，因为它在做一件看起来矛盾的事：
**把 HAL 的时基从 SysTick 挪到 TIM4，好让 SysTick 空出来给 FreeRTOS。**

### 为什么必须挪

Cortex-M3 有且只有一个 SysTick。HAL 默认用它做 `HAL_GetTick()` 的 1ms 心跳，
FreeRTOS 也要用它做任务调度的 tick。**两者抢同一个中断**，会互相打断
（HAL 的 tick 计数和 RTOS 的 tick 计数混在一起，调度时序全乱）。

解法就是把其中一个挪走。CubeMX 的 "Timebase Source" 选项就是干这个的——
`.ioc:59` 里写着：

```
NVIC.TimeBase=TIM4_IRQn
```

### 挪过去之后的分工

```
TIM4 ──每1ms──▶ TIM4_IRQHandler       (stm32f1xx_it.c:239)
                  └─ HAL_TIM_IRQHandler(&htim4)
                       └─ HAL_TIM_PeriodElapsedCallback(&htim4)   ← main.c:185
                            └─ HAL_IncTick()     → uwTick++
                                                      ↑
                                            HAL_GetTick() 读的就是它
                                            所有 HAL 超时判断都靠它

SysTick ──每1ms──▶ SysTick_Handler = xPortSysTickHandler  (port.c)
                     └─ 任务调度、vTaskDelay 的时间基准
```

**两条时间线彻底分开了**：`HAL_GetTick()` 走 TIM4（喂给 `esp8266.c` 里那一堆
`HAL_GetTick()` 超时判断），FreeRTOS 的 tick 走 SysTick（喂给 `vTaskDelay`）。

> ⚠️ **两条线虽然都是 1ms，但它们是独立的、相位不对齐的。**
> 一次 `BSP_DelayMs(1)` 在任务里实际可能睡 1.0~2.0ms
> （`vTaskDelay(pdMS_TO_TICKS(1))` 至少一个 tick，最坏等下一个 tick 边缘）。
> 这在 `app_delay.c:41-44` 的注释里写明了，也是合理的——
> 驱动要的都是"至少 N 毫秒"。

### 那个回调函数的写法

`main.c:185-197`：

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
}
```

`HAL_TIM_PeriodElapsedCallback` 在 HAL 里是**弱函数**（`stm32f1xx_hal_tim.c:5656`），
main.c 这份是强定义，链接器会选它。

**`if (htim->Instance == TIM4)` 这个判断不能删。**
这个回调是**所有定时器共用的**：以后你加一个 TIM2 做 PWM 或 TIM3 做输入捕获，
它的溢出中断也会走到这里来。不加判断的话，
**别的定时器溢出一次就会让 `HAL_GetTick()` 多走 1ms** —— 时间线直接加速。

**这个回调跑在中断上下文里**（TIM4 优先级 15）。
以后往这里加代码要注意：TIM4 的优先级数值 15 ≥ 5，
所以**可以**调 `...FromISR` 系列；但不能调非 ISR 版本（`vTaskDelay` 之类）。

### 优先级 15 的取舍

| | 值 | 说明 |
|---|---|---|
| `TICK_INT_PRIORITY` | 15 | HAL 时基想要的优先级 |
| `configKERNEL_INTERRUPT_PRIORITY` | 15 | FreeRTOS 内核（SysTick/PendSV）的优先级 |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | 5 | 能调 `FromISR` 的"门槛" |

15 是**最低优先级**。TIM4 和 SysTick 撞在一起时谁也打断不了谁，
但两者都只做一件极小的事（`uwTick++` / 调度标记），**1ms 内都远跑得完**，
不需要抢。

**为什么不设高一点？** 因为没必要。TIM4 只喂一个计数器，
设成高优先级只会让它在中断里插队，反而增加抖动。

> `.ioc:58` 里 `NVIC.TIM4_IRQn=true\:15\:0\:...` —— 这个值也是持久化的，
> 重新生成代码不会丢。

---

## 1.5 `stm32f1xx_it.c` —— 中断向量与三处 `#if 0`

### 1.5.1 三个被屏蔽的 handler

`stm32f1xx_it.c:153` / `:183` / `:202` 有三处 `#if 0 ... #endif`，
分别包住 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler`。

**为什么屏蔽**：FreeRTOS 的 CM3 移植层（`freertos/port/port.c`）里已经定义了
这三个中断处理函数，名字是 `vPortSVCHandler` / `xPortPendSVHandler` /
`xPortSysTickHandler`。而 `FreeRTOSConfig.h:348-350` 用宏把这两个名字对上了：

```c
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
```

宏替换之后，**port.c 里定义的就是 `SVC_Handler` / `PendSV_Handler` / `SysTick_Handler`
这三个符号**。如果 `it.c` 里再定义一遍 → **重复符号，链接报错**（`L6200E: Symbol
multiply defined`）。所以必须屏蔽。

**这三个中断各自负责什么：**

| 中断 | 归属 | 干什么 |
|---|---|---|
| `SVC_Handler` | FreeRTOS | 调度器启动时第一次切入任务（`vPortStartFirstTask` 里的 `svc 0`） |
| `PendSV_Handler` | FreeRTOS | **任务切换的实际发生地**。它会挂起自己、等所有中断处理完再切换，避免在中断里切换上下文 |
| `SysTick_Handler` | FreeRTOS | 每 1ms 一次，累加 tick、检查是否有任务该被唤醒、决定要不要触发 PendSV |

> **这三个 `#if 0` 是"反 CubeMX"的改动之一。**
> `it.c:151-152` 和 `:181-182`、`:198-201` 的注释都写了同一件事：
> **CubeMX 重新生成代码时会把 `#if 0` 剥掉**，重新生成之后必须手动补回来。
> 忘了补 → 编译报重复符号 → 而这个报错信息（`SVC_Handler` 重复定义）
> 对不熟悉 FreeRTOS 移植的人来说很难直接联想到"去 it.c 加 if 0"。

### 1.5.2 两个真正在跑的中断

`it.c:225-234` 和 `:239-248`，两个函数都只有一行有效代码：

```c
void DMA1_Channel6_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}
```

- **`DMA1_Channel6_IRQHandler`** —— 这个名字来自 `startup_stm32f103xb.s` 里的
  向量表，不能改。`.ioc:50` 的 `NVIC.ForceEnableDMAVector=true` 保证了
  CubeMX 会生成这个函数。
  **它现在确实会被触发**（半传输中断没关干净，[详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.3.1），
  但 `HAL_DMA_IRQHandler` 在里面只会清个标志、调一个空回调，然后就返回。

- **`TIM4_IRQHandler`** —— 走 `HAL_TIM_IRQHandler` → 判断是不是更新事件 →
  调 `HAL_TIM_PeriodElapsedCallback`（§1.4）。

**`extern` 声明在第一行**（`it.c:58-59`）：

```c
extern DMA_HandleTypeDef hdma_usart2_rx;
extern TIM_HandleTypeDef htim4;
```

这两个句柄分别定义在 `usart.c:29` 和 `stm32f1xx_hal_timebase_tim.c:28`。

### 1.5.3 那一堆 `while(1)` 的异常处理

`NMI_Handler` / `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` /
`UsageFault_Handler` —— **全部是空的死循环**，没有一句提示。

这是 CubeMX 的默认模板，也是**调试时最难受的地方**：
程序跑飞之后停在这些函数里，串口上一片安静，你不知道是哪个异常、
更不知道从哪来的。

**排查办法**（不用改 `it.c`）：

```
1. 进调试会话, 让程序停在 while(1)
2. 看调用栈 (Call Stack + Locals 窗口)
3. 看 SCB->CFSR 寄存器的值 → 查 Cortex-M3 手册的 fault 位定义
   HardFault 通常是: 空指针解引用 / 数组越界 / 栈溢出 / 非对齐访问
4. 如果是 HardFault 且 CFSR 全是 0 → 大概率是 **任务栈溢出**
   (把 uxTaskGetStackHighWaterMark 打出来看, 见 详解 04)
```

> 这五个 handler 都**没有** `USER CODE BEGIN/END` 之外的保护需求，
> CubeMX 重新生成会保留它们原样。想加日志的话直接在里面加就行，
> 但要注意此时系统状态是坏的，`printf` 不一定还能用
> （可能是栈坏了，或者卡在某个外设上）。

---

## 1.6 `stm32f1xx_hal_msp.c` —— 全局 MSP

`HAL_MspInit()`（`stm32f1xx_hal_msp.c:63-82`）由 `HAL_Init()` 回调，
在**时钟配置之前**执行。它做三件事：

```c
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
```

前两个是 CubeMX 的固定套话——**AFIO 时钟**后面要被 `SWJ_NOJTAG` 用到
（AFIO 寄存器就在这个时钟下），**PWR 时钟**本项目其实没用到
（没进低功耗模式），开着是模板习惯。

### 第三行才是重点：为什么只能用 SWD

Cortex-M3 默认同时开放 **JTAG** 和 **SWD** 两套调试接口，占掉 **5 个引脚**：

| 引脚 | JTAG 用途 | SWD 用途 |
|---|---|---|
| PA13 | JTMS | **SWDIO** |
| PA14 | JTCK | **SWCLK** |
| **PA15** | **JTDI** | 空出来了 ✅ |
| **PB3** | **JTDO** | 空出来了 ✅ |
| **PB4** | **NJTRST** | 空出来了 ✅ |

`__HAL_AFIO_REMAP_SWJ_NOJTAG()` = **只关 JTAG，保留 SW-DP**，
把 PA15 / PB3 / PB4 三个引脚解放出来当普通 GPIO。

**本项目其实没用到这三个脚**（只用 PB1、PB12、PA2/3、PA9/10）。
所以这一行**不是功能需要，是提前占位**——
哪天要从 PB3/PB4 引一根线出来，不会突然发现"这个引脚怎么不受控"。

> ⚠️ **千万别改成 `__HAL_AFIO_REMAP_SWJ_DISABLE()`。**
> 那个会把 **SWD 也一起关掉**，后果是**调试器再也连不上芯片**。
> 恢复办法只有"按住复位键 → 点下载 → 松手"（connect under reset），
> 让芯片在复位期间还没执行到这一行时被抓住。
>
> `NOJTAG` 是安全的中间档：**JTAG 没了，SWD 还在**。
> 本工程用的就是 SWD（4 线：3V3 / GND / SWDIO / SWCLK）。

---

## 1.7 `fputc` / MicroLIB / `Error_Handler`

### 1.7.1 `printf` 的出口

`main.c:170-174`：

```c
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
```

C 库的 `printf` 在内部**每输出一个字符就调一次 `fputc`**。
把这个函数重定向到 USART1，`printf` 的输出去向就变成了串口。

在 Keil 上，**只写 `fputc` 就够了的唯一前提是用 MicroLIB**。
本工程 `project.uvprojx:193` 写着：

```
<useUlib>1</useUlib>
```

用标准 C 库（不用 MicroLIB）的话，除了 `fputc` 还得处理
半主机（semihosting）——`printf` 会试图通过调试器把字符发给 PC，
没有调试器时程序**卡死在第一个 printf**。标准库要额外写
`__use_no_semihosting` 和 `_sys_exit` / `_ttywrch` 之类的一堆桩函数。

**MicroLIB 就是为了省掉这些**：代码小、不依赖半主机、只要一个 `fputc`。

### 1.7.2 MicroLIB 的代价：**没有浮点格式化**

MicroLIB 的 `printf` **不带浮点格式化**。意味着：

```c
printf("%f", 27.5);        /* ← 不可用 */
printf("%d.0", 27);        /* ← 本项目的做法 */
```

这就是 `task_wifi.c` 里 JSON 拼装写 `%d.0` 而**不是** `%f` 的**真正原因**
（`从零入门 05` §5.7 提到了这个写法，但没说清楚为什么）。

> **"MicroLIB 省 flash" 只是次要理由**——这个工程 Flash 才用了 18.7KB / 64KB，
> 根本不缺。**主因是浮点不可用**（顺便也就避免了把浮点库链进来）。
>
> **验证方法**：写一句 `printf("%f\r\n", 1.5f);` 编译烧录，
> 看串口输出——要么是空的，要么是乱码，反正不是 `1.500000`。

**这也解释了一个设计约束**：整个工程的 `printf` / `snprintf` 里
**没有一个 `%f`**。以后加浮点传感器（比如 BH1750 光照），
要么把值乘 10 变成整数打（`%d.%d`），要么换成 sprintf 手动拆整数和小数部分。

### 1.7.3 `HAL_MAX_DELAY` 是个隐患

```c
  HAL_UART_Transmit(&huart1, ..., HAL_MAX_DELAY);
```

`HAL_MAX_DELAY = 0xFFFFFFFF`。意思是"**等到发出去为止，不管多久**"。

如果串口助手**没打开**、或者 USB-TTL 的 TX 线**没接**，会怎样？
`HAL_UART_Transmit` 是**轮询式**的：它等 `USART1->SR` 的 `TXE` 位。
**没接线的 USART 的 TXE 位依然是正常的**（这是发送寄存器空的标志，
与外部无关），所以实际上**没接线也能"发成功"**（数据丢在空中）。

真正会卡死的情况是：**USART1 的时钟被关掉了**（那 TXE 永远不置位）。
正常流程里不会发生。所以这个 `HAL_MAX_DELAY` 在实践中是**安全的**，
但**理论上是个无界阻塞**——一旦触发就是"任务里死等，整个系统卡住"。

`main.c:167-168` 的注释里写了正确做法：改成有限超时。
（《FreeRTOS 驱动适配笔记》§3.6 有讨论。）

### 1.7.4 `fputc` 不可重入 —— 本工程**确实存在**的隐患

`HAL_UART_Transmit(&huart1, ...)` 操作的是**全局共享的 `huart1`**，
而它内部**没有任何互斥保护**（纯轮询，不是 DMA，也不会进临界区）。

本工程有**两个任务**都在调 `printf`：

| 任务 | 优先级 | 打印什么 |
|---|---|---|
| `TaskWifi` | **3**（高） | `[WiFi] ...` / `[MQTT] ...` |
| `TaskDht11` | **2**（低） | `[DHT11] #N Temp: ...` |

**`TaskWifi` 会抢占 `TaskDht11`。** 如果 `TaskDht11` 打到一半
（比如刚输出 `[DHT11] #3  Tem`），`TaskWifi` 抢占进来打了一整行
`[WiFi] uploaded: ...`，然后 `TaskDht11` 才把剩下的 `p: 27 C` 补上，
串口上看到的就是：

```
[DHT11] #3  Tem[WiFi] uploaded: Temp 27 C, Humi 98%
p: 27 C  Humi 98%
```

**不是数据出错，是日志被插花了。** 但调试的时候会让人怀疑"是不是串口丢了字节"
或者"是不是内存坏了"——浪费很多时间。

> **为什么会这样：** `printf` → `fputc` 每个字符一次，
> 中间有足够的窗口被抢占。而且 `HAL_UART_Transmit` 会阻塞等 TXE，
> 高优先级任务一进来就插到中间了。
>
> **要不要修？** 这不是 bug（数据没错），但确实影响调试效率。
> 三种修法，从简到繁：
> ① **接受它**——真的很难读时把两个任务的打印错开时间
> ② **加临界区**——`fputc` 里包 `taskENTER_CRITICAL()`，
>    但 `fputc` 是 C 库回调，在这里 include FreeRTOS 会破坏"BSP 不依赖 RTOS"
>    的分层（虽然 `main.c` 已经不在 BSP 层了，勉强能接受）
> ③ **改成专用输出任务**——两个任务往队列里发字符串，一个任务统一打
>
> 本次只记录，未修改代码。

### 1.7.5 `Error_Handler()` —— 静默挂死

`main.c:203-212`：

```c
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
```

**关掉所有中断，然后死循环。** 后果：

- **没有输出。** 函数里一句 `printf` 都没有，所以串口日志停在**出错前的最后一行**。
- **没有看门狗。** 本工程没开 IWDG/WWDG（`grep IWDG` 全工程无匹配），
  所以它会**永远**循环下去，不会自动复位。
- **时间停止。** `__disable_irq()` 之后 TIM4 中断进不来，
  `HAL_GetTick()` 也停了——所以**不能靠"看 tick 有没有走"来判断**
  （它一定不走）。
- **调试器能抓。** 这是唯一的好消息：`while(1)` 里的 PC 值好认，
  挂上调试器看调用栈就知道是谁调进来的
  （调用栈里能看到 `Error_Handler` 的上一个栈帧）。

**谁会调它？** 全部是初始化路径上的"配置失败"：

| 调用点 | 触发条件 |
|---|---|
| `main.c:145` | HSE 起振失败 / PLL 锁定失败（晶振虚焊、负载电容不对） |
| `main.c:159` | 切换时钟源失败 |
| `usart.c:53` / `:81` | `HAL_UART_Init` 失败（**参数非法**，正常不会） |
| `usart.c:156` | `HAL_DMA_Init` 失败 |
| `app_main.c:62` | `xQueueCreate` 返回 NULL（**FreeRTOS 堆不够**） |
| `task_dht11.c` / `task_wifi.c` | `xTaskCreate` 失败的兜底 |

> **最可能真实发生的是最后两条**——`configTOTAL_HEAP_SIZE` 不够。
> 那时候串口会停在 `DHT11 Sensor Ready!` 之后
> （因为队列是在那句 printf 之后建的），**看到这个位置就知道是堆的问题**。

---

## 1.8 关键设计决策

### 决策一：`MX_DMA_Init()` 必须在 `MX_USART2_UART_Init()` 之前

| | |
|---|---|
| **决策** | `main.c:92-94` 的顺序：GPIO → DMA → USART1 → USART2 |
| **理由** | DMA 通道的寄存器配置（`usart.c:146-159`）发生在 `HAL_UART_Init` 内部回调的 `HAL_UART_MspInit` 里。**配置 DMA 寄存器需要 DMA1 的时钟**，而时钟是 `MX_DMA_Init()` 开的 |
| **不这么做** | 写寄存器**被静默丢弃**，`HAL_DMA_Init()` 照样返回 `HAL_OK`，程序照常跑 —— 直到发现"一个字节都收不到"且**没有任何报错**。这是最难查的一类故障 |

`.ioc:117` 里的 function list 也固化了这个顺序
（`3-MX_DMA_Init` 在 `5-MX_USART2_UART_Init` 之前），
所以 **CubeMX 重新生成不会打乱它**。

### 决策二：接收用 `DMA_CIRCULAR` + 轮询 CNDTR，而不是 IDLE 中断

| | |
|---|---|
| **决策** | `usart.c:152` `Mode = DMA_CIRCULAR`；`esp8266.c` 里**刻意不使能 IDLE 中断** |
| **理由** | ① 循环模式让 DMA 永不停止，CPU 参不参与都不丢字节，**扛得住 DHT11 那 4ms 关中断**；② 轮询 CNDTR 不需要任何中断，**BSP 层就不用 include FreeRTOS**，保住驱动可以脱离 RTOS 移植 |
| **不这么做** | 用 IDLE 中断判帧的话，中断里要唤醒任务就得调 `xSemaphoreGiveFromISR` → `esp8266.c` 必须依赖 FreeRTOS → 驱动不能再单独移植到裸机工程 |

完整论证见 [详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.1 和 §2.9。

### 决策三：TIM4 顶替 SysTick 做 HAL 时基

| | |
|---|---|
| **决策** | `.ioc:59` `NVIC.TimeBase=TIM4_IRQn` |
| **理由** | FreeRTOS 需要 SysTick 做任务调度，HAL 需要 1ms 心跳——**Cortex-M3 只有一个 SysTick**。把 HAL 的挪到 TIM4，两者彻底分开 |
| **不这么做** | 两个 tick 混在一个中断里互相打断，`HAL_GetTick()` 和 RTOS tick 都会不准；`vTaskDelay` 的时间会漂 |

代价：多占一个 TIM4 和它的中断向量。**F103 有 4 个通用定时器，完全够用。**

### 决策四：TIM4 优先级 = 15（最低）

| | |
|---|---|
| **决策** | `stm32f1xx_hal_conf.h:132` `TICK_INT_PRIORITY = 15U` |
| **理由** | TIM4 中断里只做 `uwTick++`，是**最不需要及时响应**的事。设最低优先级让它永远排在所有业务中断后面，减少对时序敏感代码（DHT11 位流、DMA）的干扰 |
| **不这么做** | 设成高优先级，它会在 DHT11 读位流时插进来，让本来就只有几十微秒余量的电平宽度测量更紧张 |

数值 15 ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`(5)，
所以**理论上它能调 `FromISR` API**（虽然现在没调）。

### 决策五：DMA1_CH6 优先级 = 5

| | |
|---|---|
| **决策** | `dma.c:53` `HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0)` |
| **理由** | **原注释的理由已过时**（说是为了发信号量，但工程里没有 `FromISR` 调用）。现在保留 5 的实际意义是"以后要发信号量时不用改"——它刚好是 FreeRTOS 允许调 API 的最低门槛 |
| **不这么做** | 设成 0~4：万一以后真在 DMA 中断里调了 `...FromISR`，**会破坏内核的临界区保护**，表现为随机死锁或任务卡死，极难查 |

> ⚠️ `dma.c:47-52` 的注释需要更新（理由过时，且"CubeMX 会打回 0"的警告
> 已不成立，`.ioc:48` 已持久化为 5）。见 [详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.3.1。

### 决策六：用 `#if 0` 屏蔽 `it.c` 里的三个 handler

| | |
|---|---|
| **决策** | `it.c:153/183/202` |
| **理由** | 这三个符号由 FreeRTOS 的 `port.c` 提供，`FreeRTOSConfig.h:348-350` 把名字映射到了 CMSIS 标准名。两份定义会链接冲突 |
| **不这么做** | **链接报 `L6200E: Symbol multiply defined`**。反过来，如果不用宏映射而选择改 `port.c` 里的函数名，那移植层就和 CMSIS 绑死了，换芯片时要改 `port.c` |

### 决策七：MicroLIB + `fputc`

| | |
|---|---|
| **决策** | `project.uvprojx:193` `useUlib=1`，只重定向 `fputc` |
| **理由** | 不依赖半主机、不用写一堆桩函数、代码小。代价是**没有浮点格式化** |
| **不这么做** | 用标准库要处理 semihosting：没连调试器时 `printf` **卡死在第一个字符**（更糟的静默失败） |

### 决策八：PB1/PB12 先写电平再配模式，且 PB1 推挽、PB12 开漏

见 §1.3.6。核心是**用"上电即为低"这个副作用**让 ESP8266 保持复位，
直到驱动主动释放它。

---

## 1.9 已知局限与注意事项

| 局限 | 触发条件 | 现象 | 能自愈 | 修法 |
|---|---|---|---|---|
| **`fputc` 不可重入** | TaskWifi 抢占 TaskDht11 的 printf | 串口日志**插花**（不是数据错） | 是（下一行就正常） | §1.7.4 的三种修法 |
| **`HAL_MAX_DELAY` 无界阻塞** | USART1 时钟被关 | 任务永久卡在 `printf` | 否 | 改有限超时 |
| **`Error_Handler` 静默挂死** | 初始化失败 / 堆不足 | CPU 空转，**无输出、无复位** | 否（没看门狗） | 里面加 `printf` + 开 IWDG |
| **MicroLIB 无浮点格式化** | `printf("%f")` | 输出为空或乱码 | — | 用 `%d.%d` 手动拆 |
| **异常 handler 全是空循环** | HardFault 等 | 串口静默，不知从哪来的 | 否 | 见 §1.5.3 的排查办法 |
| **APB1 上限 36MHz** | 改时钟树把 APB1 设成 ÷1 | 芯片行为不可预期 | 否 | 别改，36 是硬上限 |
| **GPIOD 时钟白开** | — | 只多一点功耗 | — | 删 `gpio.c:48` |
| **HSI 一直开着** | — | 多一点功耗 | — | 电池场景才关（会失去 HSE 失效兜底） |
| **`dma.c:47-52` 注释过时** | 读注释的人被误导 | 以为有信号量机制 | — | 更新注释 |

### 展开说：RAM 账

**这是后面所有"缓冲区加大一点"要求的唯一依据**，务必记住这些数字。

`MDK-ARM/build.log`：

```
Program Size: Code=18720  RO-data=524  RW-data=164  ZI-data=12828
```

芯片资源（`project.uvprojx:21`）：

```
IRAM(0x20000000-0x20004FFF)   ← 0x5000 = 20480 = 20 KB
IROM(0x8000000-0x800FFFF)     ← 0x10000 = 65536 = 64 KB
```

| 项目 | 已用 | 占比 |
|---|---|---|
| **Flash** | 18720 + 524 = 19244 B | **29%**（宽裕） |
| **RAM（动态）** | RW-data 164 + ZI-data 12828 = **12992 B** | **63%** |

**RAM 那 12828 字节的 ZI-data 里，最大的一块是 FreeRTOS 的堆：10240 字节**
（`FreeRTOSConfig.h:290` 的 `configTOTAL_HEAP_SIZE`）。

```
12828 (ZI)  ≈  10240 (FreeRTOS 堆)
             +  1024 (主栈 Stack_Size, 0x400)
             +   512 (C 库堆 Heap_Size, 0x200)
             +   512 (ESP8266_RxBuf)
             +   256 (ESP8266_DmaBuf)
             +   256 (s_MqttTxBuf)
             +   ~28 (其余零散全局变量)
```

**剩下的自由 RAM 只有约 7.5 KB**（20480 − 12992），
而 FreeRTOS 堆里的 10240 还要装**两个任务的栈**
（`APP_STACK_WIFI = 512` 字 = 2048 B，`APP_STACK_DHT11 = 256` 字 = 1024 B）
加上队列和 TCB。**实际能动的余量不多。**

**结论：想加大 `ESP8266_RxBuf` 或 `ESP8266_DmaBuf`，得先想清楚从哪儿减。**
详见 [详解 02](09-ESP8266接收架构与+IPD剥离.md) §2.9。

---

## 1.10 动手改这里

### 改晶振频率

**改哪几行**：
1. `stm32f1xx_hal_conf.h` 的 `HSE_VALUE`（默认 8000000）
2. `.ioc` 里的 HSE 值，重新生成（或者手改 `RCC_OscInitStruct`）
3. **确认 `FLASH_LATENCY` 还是对的**（§1.3.3 的表）

**怎么验证**：串口助手打一行 `printf("ok\r\n")`，
**输出乱码 = 波特率算错了 = 频率没配对**。这是最快的验证方式。

### 改主频（比如降到 48MHz 省电）

**改哪几行**：`main.c:142` 的 `RCC_OscInitStruct.PLL.PLLMUL`，
以及 `main.c:157` 的 `FLASH_LATENCY_2` 按 §1.3.3 的表调整。

**连带要改的**：
- ⚠️ **APB1 的分频比要重新算**——48MHz 下 APB1 还是得 ≤36MHz，
  所以 `APB1CLKDivider` 可能要变成 `RCC_HCLK_DIV2`（48/2 = 24MHz，OK）。
- ⚠️ **`HAL_InitTick` 不用改**——它每次都重新读时钟配置算预分频。
  这正是把时钟计算写成"读寄存器"而不是"写死常量"的好处。

**怎么验证**：串口打印 `HAL_RCC_GetHCLKFreq()` 和 `HAL_RCC_GetPCLK1Freq()`，
和你算的对一下。**再看 `HAL_GetTick()` 走不走得准**——
拿秒表对着一句"每 10 秒打印一次"的日志看，1 分钟内误差应该在 1 秒内。

### 加一个新引脚

**改哪几行**：`.ioc` 里点出来，或者手改 `gpio.c` 在
`/* USER CODE BEGIN 2 */` 之后加自己的配置。

**如果引脚比较敏感（比如又一个复位脚）**：记得在
`HAL_GPIO_WritePin(GPIOB, ...)` 那个"先写电平"的列表里加上它。
`.ioc` 会自动处理；手改的话容易漏。

**怎么验证**：用万用表量电平，或者写一段
`HAL_GPIO_TogglePin` + `BSP_DelayMs(500)` 让它闪。

### ⚠️ CubeMX 重新生成代码之后的 checklist

这是本文**最实用的一节**。点完 "Generate Code" 之后**必须**逐条确认：

| # | 检查什么 | 不做会怎样 |
|---|---|---|
| 1 | `it.c` 里**三处 `#if 0` 补回来了吗**（`SVC_Handler` / `PendSV_Handler` / `SysTick_Handler`） | **链接报重复符号**，编不过 |
| 2 | `it.c:146-152`、`:181-182`、`:198-201` 的**注释**还在吗 | 下次再重新生成时又得重新想一遍为什么有 `#if 0` |
| 3 | `main.c` 的 `/* USER CODE BEGIN 2 */` 里 `App_Init()` + `vTaskStartScheduler()` 还在吗 | 编译过但**程序什么都不做** |
| 4 | `main.c` 的 `fputc`（`USER CODE BEGIN 4`）还在吗 | `printf` 没输出。**如果 `fputc` 没了还开着 MicroLIB，程序会卡在半主机调用** |
| 5 | `HAL_TIM_PeriodElapsedCallback`（在 `USER CODE BEGIN 4` **之外**！）还在吗 | **`HAL_GetTick()` 永远不走** → 所有超时判断失效 → ESP8266 所有命令都超时 |
| 6 | `dma.c` 里 `HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0)` 还是 5 吗 | 变 0 的话暂时没影响（没有 `FromISR`），但埋了雷 |
| 7 | `main.c:92-94` 的 `MX_DMA_Init()` 还在 `MX_USART2_UART_Init()` 之前吗 | **静默收不到数据** |
| 8 | `usart.c:152` 的 `Mode = DMA_CIRCULAR` 还在吗 | **整个接收层失效**，且不报错 |

> **第 5 条最危险，也最容易被忽略。** `HAL_TIM_PeriodElapsedCallback`
> 在 `main.c` 里**不在任何 `USER CODE` 块内**（CubeMX 把它生成在文件末尾，
> 但那是 "Callback" 样板区，重新生成时**不会保留**——具体行为取决于
> CubeMX 版本和 .ioc 配置）。如果它被抹掉，链接会**成功**
> （HAL 里有弱定义），但 `HAL_IncTick` 再也不会被调用，
> 于是 `HAL_GetTick()` 恒为 0 ——
> **表现是"所有 ESP8266 命令都超时"，但串口能打、任务能跑，
> 看起来完全不像时钟问题。**

**最省事的做法**：改完之后先**编译**，然后**烧录并看串口前 20 秒的日志**
（《从零入门 06》§6.3 有正常日志长什么样）。
日志能走完全链路就说明这八条都过了。

### 别的想改的

| 想做什么 | 改哪 | 注意 |
|---|---|---|
| 开启看门狗 | 加 IWDG 初始化 + 在任务里喂狗 | 喂狗要放在**所有任务都能走到**的地方，否则一个任务卡住就复位循环 |
| 让 `Error_Handler` 有输出 | `main.c:203` 里加 `printf` | 如果失败的是 USART1 本身就没用；可以改用 GPIO 闪灯 |
| 关掉 HSI 省电 | `main.c:139` 改 `RCC_HSI_OFF` | 失去 HSE 失效的兜底 |
| 关掉 GPIOD 时钟 | 删 `gpio.c:48` | 无副作用 |
| `printf` 变线程安全 | §1.7.4 的三种修法 | 别在 `fputc` 里直接 `#include "FreeRTOS.h"`，会破坏分层 |

---

## 1.11 一句话总结

**`Core/` 里的代码只干一件事：在 `vTaskStartScheduler()` 之前的几十毫秒里，
把时钟、时基、中断、引脚全部建立好，然后交给 FreeRTOS 再也不回来。**

三件最容易忘的事：

1. **`HAL_InitTick` 被调了两次** —— 第二次（`HAL_RCC_ClockConfig` 里）
   才是决定 TIM4 预分频的那次。APB1 预分频 ≠ 1 时定时器时钟要 **×2**，
   这个"×2"就是 `stm32f1xx_hal_timebase_tim.c` 里那行 `2UL *` 的全部理由。
2. **`MX_DMA_Init()` 的位置是硬约束** —— 它不开时钟，DMA 的寄存器配置
   就会被**静默丢弃**，程序照常跑但一个字节都收不到。
3. **点完 CubeMX "Generate Code" 要复查八条** ——
   尤其是 `it.c` 的三个 `#if 0` 和 `HAL_TIM_PeriodElapsedCallback`。

---

**相关笔记**

- 下一份：[详解 02 · ESP8266 接收架构与 +IPD 剥离](09-ESP8266接收架构与+IPD剥离.md)
- 任务与队列概念：《从零入门 02-FreeRTOS任务框架》
- 启动之后的流程：[详解 04 · 任务层上报循环](11-任务层-上报循环.md)
- 移植过程（为什么改 `FreeRTOSConfig.h`）：《FreeRTOS 移植笔记》
- 正常日志长什么样：《从零入门 06》§6.3
