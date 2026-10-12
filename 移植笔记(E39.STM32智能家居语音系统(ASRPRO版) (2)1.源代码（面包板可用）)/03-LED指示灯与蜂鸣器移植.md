# 03 LED 指示灯与蜂鸣器移植

**定位**：参考工程的 LED 是 TIM1 CH2 的 PWM 调光（PB14），你的板子上是两个普通 GPIO 驱动的 LED（PC14/PC15），**整套 PWM 代码作废，改写成开关控制**。顺带把蜂鸣器（PA0 → PC13）也一起改了，因为两者都是"改两个宏就完事"的同类。

**读之前先补**：[00-总纲](00-总纲-PCB引脚表与移植决策.md)（第三节第 3、4 条）。

**参考文件**：`1.源代码（面包板可用）/Hardware/LED.h`、`LED.c`、`Buzzer.h`、`Buzzer.c`、`System/PWM.c`

---

## 一、作用原理

### LED 本身没有任何"原理"可讲，坑全在电网上

LED 是发光二极管，**阴极接低电平、阳极接高电平**且**电压超过正向压降**才亮。就这么简单。麻烦的是它和 MCU 的接法：

```
方式A（灌电流 / 阳极接电源）:  3V3 → 限流电阻 → LED → MCU 引脚
方式B（拉电流 / 阴极接地）:    MCU 引脚 → 限流电阻 → LED → GND
```

两者的软件语义**完全相反**：

| | 方式 A（灌电流） | 方式 B（拉电流） |
|---|---|---|
| 引脚输出**低** | **亮** | 灭 |
| 引脚输出**高** | 灭 | **亮** |
| 电流从哪来 | 从 3V3 经引脚流入芯片 | 从引脚流出到地 |
| 常见程度 | **最常见**（因为 F1 灌电流能力比拉电流强） | 少见 |

你的板子是**方式 A**。看网表：

```
'$1N75' ; D2.1 R11.2        ← LED 的一脚接电阻
3V3 ; OLED1.2 R1.1 R11.1 ... ← 电阻另一端接 3V3
'$1N74' ; D3.1 R1.2
PC14 ; D2.2 U1.23           ← LED 另一脚直接接 MCU
PC15 ; D3.2 U1.24
```

串起来就是 `3V3 → R11 → D2 → PC14`。

> ⚠️ **PC14 / PC15 输出低电平才亮。**

这一点和参考工程的语义正好相反（参考工程 `LED_Init()` 里最后一句是 `GPIO_ResetBits(LED_PROT, LED)`，那是"灭灯"）。**第一次调试如果点不亮，先别怀疑代码，先确认你把高电平当成"亮"了。**

### PWM 调光的原理（参考工程为什么要那么复杂）

参考工程想让 LED 能调亮度，用的是 PWM：**用很快的开关，靠占空比骗过眼睛**。如果开关频率够高（>100 Hz），人眼看到的不是"闪"，而是"平均亮度"——占空比 30% 就看着像 30% 亮。

参考工程 `PWM_Init(100 - 1, 720 - 1)` 的算法：

```
TIM1 时钟 = 72 MHz
预分频 PSC = 720 - 1 → 计数时钟 = 72 MHz / 720 = 100 kHz
自动重装 ARR = 100 - 1 → PWM 频率 = 100 kHz / 100 = 1 kHz
比较值 CCR 取 0~100 → 占空比直接就是百分比（0% ~ 100%）
```

所以 `TIM_SetCompare2(TIM1, 30)` 就是"30% 亮度"。这个设计很漂亮——**PWM 的寄存器值直接等于百分数**，不用换算。

**但你的板子上用不了硬件 PWM**：PWM 输出必须走**定时器的复用通道**（TIM1_CH2 只能出在 PA9/PB14 等固定引脚上）。你的 LED 在 PC14/PC15 上，那两根是**普通 GPIO，任何定时器都没有通道能输出到这两个脚**。所以硬件 PWM 这条路是断的。

> ⚠️ **更正（题目4 扩展功能2 需要 PWM 调光时看这里）**：断的只是**硬件 PWM**。软件 PWM——用定时中断自己翻转 GPIO——在 PC14/PC15 上完全可行，**零改板**；或者飞一根线到 PA8 用 TIM1_CH1 的真硬件 PWM。两种做法见 [06 篇第六节](06-题目4缺口补齐-按键时间播报PWM云端.md)。本篇下面的"改写成开关控制"是按**不需要调光**写的。

### 蜂鸣器：有源还是无源

参考工程 `Buzzer_Init()` 只是把引脚配成推挽输出，`Buzzer_ON()` 就是 `GPIO_SetBits()`——**高电平"响"，低电平"不响"**，中间的过程完全不管。这说明它默认用的是**有源蜂鸣器**（模块自带振荡电路，给电就响，MCU 只管开关）。

⚠️ 如果你换上的是**无源蜂鸣器**，`Buzzer_ON()` 只会"嗒"一声——因为无源的需要 MCU 给 **2~4 kHz 方波**才响。判断方法：直接给它 3.3 V 直流，有源的会响，无源的不会。你的板子上 `U4` 标的是"蜂鸣器模块"，模块一般都带振荡电路，**按有源处理即可**。

---

## 二、代码原理

### 参考工程 `LED.c` 的三个部分，一个都留不下

```c
void LED_Init(void)
{
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;   // ① 复用推挽
    GPIO_Init(LED_PROT, &GPIO_InitStructure);
    GPIO_ResetBits(LED_PROT, LED);
}

void LED_PWM_KEY(void)                                // ② 按键切档
{
    if (KeyNum == 2)
    {
        ledluminance.ledState++;                      // 关 / 30% / 60% / 100%
        ...
        TIM_SetCompare2(TIM1, ledluminance.ledDutyRatio);   // ③ 改占空比
    }
}
```

| 部分 | 为什么不能移植 |
|---|---|
| ① `GPIO_Mode_AF_PP` | 复用推挽是给"引脚交给定时器输出"用的。PC14/PC15 没有定时器通道，配成复用毫无意义。要改成 `GPIO_MODE_OUTPUT_PP`（普通推挽） |
| ② `KeyNum` 切档 | **你的板子上没有按键**。这个交互整个作废 |
| ③ `TIM_SetCompare2(TIM1, ...)` | TIM1 的通道配在 PB14/PB15 上，你的板子没有这两根线。改成 `HAL_GPIO_WritePin()`（要调光则见 [06 篇第六节](06-题目4缺口补齐-按键时间播报PWM云端.md)） |

**所以 `LED.c` / `LED.h` / `PWM.c` / `PWM.h` 四个文件一个都不用拷。** 参考工程唯一还有参考价值的是 `ledluminance` 那个状态机结构体——它演示了"用枚举表达档位"的写法，你在语音控制里想实现"灯光三档"的话可以照抄这个思路（见 04 篇）。

### 参考工程 `Buzzer.c` 可以照抄，只改两个宏

```c
#define Buzzer       GPIO_Pin_0      // → GPIO_PIN_13
#define Buzzer_PROT  GPIOA           // → GPIOC
```

函数体（`_Init` 推挽输出 + `_ON` 高 + `_OFF` 低）**逻辑完全正确，不用改**，只是把 `RCC_APB2PeriphClockCmd` 换成 HAL 宏、`GPIO_Init` 换成 `HAL_GPIO_Init`。

⚠️ 一个细节：参考工程里 `Buzzer_ON()` 会置全局 `Buzzer_Flag = 1`。这个标志位在参考工程里给自动模式用（`TIM2.c` 里根据烟雾阈值开关蜂鸣器）。你移植时**保留这个变量**，如果以后要做报警逻辑还用得上；不需要就删掉。

---

## 三、怎么移植到 PCB

### 第 1 步：LED 驱动

新建 `BSP/led/led.h`：

```c
#ifndef __LED_H
#define __LED_H

#include "main.h"

/* PCB: D2→PC14, D3→PC15, 均经 R1/R11 到 3V3
 * 灌电流接法: 输出低电平点亮, 高电平熄灭 (和参考工程反着) */
#define LED1_PORT  GPIOC
#define LED1_PIN   GPIO_PIN_14
#define LED2_PORT  GPIOC
#define LED2_PIN   GPIO_PIN_15

void LED_Init(void);
void LED1_On(void);   void LED1_Off(void);   void LED1_Toggle(void);
void LED2_On(void);   void LED2_Off(void);   void LED2_Toggle(void);

#endif
```

新建 `BSP/led/led.c`：

```c
/* ============================================================================
 * 板载 LED 驱动 (D2/D3)
 * 接法是灌电流: 3V3 → 电阻 → LED → MCU 引脚, 所以低电平点亮
 * PC13/PC14/PC15 是低驱动能力脚, 限流电阻要 >=1k (板上的 R1/R11 已满足)
 * ========================================================================== */

#include "led.h"

void LED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin   = LED1_PIN | LED2_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED1_PORT, &gpio);

    LED1_Off();
    LED2_Off();
}

void LED1_On(void)  { HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET); }
void LED1_Off(void) { HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);   }
void LED2_On(void)  { HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, GPIO_PIN_RESET); }
void LED2_Off(void) { HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, GPIO_PIN_SET);   }
void LED1_Toggle(void) { HAL_GPIO_TogglePin(LED1_PORT, LED1_PIN); }
void LED2_Toggle(void) { HAL_GPIO_TogglePin(LED2_PORT, LED2_PIN); }
```

**改哪几行**（和参考工程对照）：

| 参考工程 | 改成 | 为什么 |
|---|---|---|
| `#define LED GPIO_Pin_14` + `LED_PROT GPIOB` | `LED1_PIN GPIO_PIN_14` + `LED1_PORT GPIOC` | 换成 PC，并拆成两个灯 |
| （无） | 新增 `LED2_PIN GPIO_PIN_15` + `LED2_PORT GPIOC` | 板上有 D2/D3 两个灯 |
| `GPIO_Mode_AF_PP` | `GPIO_MODE_OUTPUT_PP` | 不走定时器，普通推挽 |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE)` | `__HAL_RCC_GPIOC_CLK_ENABLE()` | SPL → HAL，且换成 GPIOC |
| `GPIO_ResetBits(...)`（灭） | `HAL_GPIO_WritePin(..., GPIO_PIN_SET)`（灭） | 灌电流接法，高电平才是灭 |
| `LED_PWM_KEY()` / `TIM_SetCompare2` | **删掉**，用 `LED1_On/Off/Toggle` | 没有 PWM 通道 |

⚠️ **PC14/PC15 的两件事，必须知道**：

1. **它们是 LSE 晶振脚（OSC32_IN / OSC32_OUT）。** `EnvMon_WIFI` 的 `SystemClock_Config()` 用的是 HSE（PD0/PD1），`.ioc` 里也没开 LSE，所以拿来当 GPIO 是**安全的**。但如果你以后要上 RTC 或者外挂 32.768 kHz 晶振，**这两个脚要还回去**。同时把 `.ioc` 里 `RCC` 的 LSE 保持为 Disabled，别误开。
2. **它们属于备份域引脚组，驱动能力只有 3 mA**（普通脚是 20 mA）。所以限流电阻不能小：`(3.3 − 2.0) / 0.003 ≈ 430 Ω` 是理论下限，**取 1 kΩ** 才稳（电流约 1.3 mA，亮度也够看）。板上 R1/R11 如果是 220 Ω 就偏大了，把 LED 的亮度会很高但引脚会超标；**如果你自己选的电阻，选 1 kΩ**。

### 第 2 步：蜂鸣器驱动

新建 `BSP/buzzer/buzzer.h`：

```c
#ifndef __BUZZER_H
#define __BUZZER_H

#include "main.h"

/* PCB: U4.2 是信号脚接 PC13, U4.3 接 3V3, 高电平响 */
#define BUZZER_PORT  GPIOC
#define BUZZER_PIN   GPIO_PIN_13

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);

#endif
```

新建 `BSP/buzzer/buzzer.c`：

```c
/* ============================================================================
 * 蜂鸣器驱动 (有源, 给电即响)
 * PC13 也是备份域引脚, 只有 3mA 驱动能力; 模块信号脚是驱动三极管的基极,
 * 电流在 uA 级, 够用
 * ========================================================================== */

#include "buzzer.h"

void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin   = BUZZER_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);

    Buzzer_Off();
}

void Buzzer_On(void)  { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET); }
void Buzzer_Off(void) { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET); }
```

**改哪几行**：

| 参考工程 | 改成 |
|---|---|
| `#define Buzzer GPIO_Pin_0` | `GPIO_PIN_13` |
| `#define Buzzer_PROT GPIOA` | `GPIOC` |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)` | `__HAL_RCC_GPIOC_CLK_ENABLE()` |
| `Buzzer_ON()` 里的 `GPIO_SetBits` | `HAL_GPIO_WritePin(..., GPIO_PIN_SET)` — **逻辑不变，还是高电平开** |

⚠️ **PC13/PC14/PC15 是同一条 3 mA 的规矩**。蜂鸣器模块的信号脚是接到三极管基极的，串着电阻，电流很小，没问题；但如果你的模块是**直接驱动蜂鸣器本体**（不经过三极管），PC13 拉不动，会响得很轻或者根本不响——那种情况需要一个 NPN 三极管（S8050 之类）加基极电阻，或者把蜂鸣器改到别的脚（飞线到 PA0，那本来就是参考工程的脚）。

### 第 3 步：`App_Init()` 里初始化

```c
void App_Init(void)
{
    LED_Init();                    /* ← 新增 */
    Buzzer_Init();                 /* ← 新增 */
    DHT11_Init();
    printf("DHT11 Sensor Ready!\r\n");
    ...
}
```

⚠️ **不要**把 `LED_Init()` 放在 `TaskXxx` 里面。它是纯 GPIO 配置，没有延时，放 `App_Init()` 里最简单。而且放这里的好处是**可以在任何任务里用 LED 当调试指示**——比如在 `TaskWifi` 的 `WifiConnect()` 成功后 `LED1_On()`，一眼就能看出联网状态。

### 第 4 步：用起来——最常见的三个用法

**用法一：状态指示（推荐，零成本）**

`APP/task_wifi.c` 的 `TaskWifi()` 里：

```c
if (WifiConnect() == 0)
{
    LED1_Off();                       /* 没连上: 熄 */
    ...
}
else
{
    LED1_On();                        /* 连上了: 常亮 */
}
```

⚠️ 注意别做"每秒翻转一次"的心跳灯，除非你确定这个任务是周期性执行的。`TaskWifi()` 是阻塞在 `xQueueReceive(..., portMAX_DELAY)` 上的，只在有 DHT11 数据时才醒来（2 秒一次），所以翻转频率恰好等于采样频率，看着像呼吸灯——这算巧合，**别把它当设计依赖**。要真正的心跳要么用软件定时器，要么在 DHT11 任务里翻。

**用法二：报警鸣叫**

在 DHT11 任务里做阈值判断：

```c
if (dht.temperature > 35) { Buzzer_On(); }
else                      { Buzzer_Off(); }
```

⚠️ **有源蜂鸣器没有音量档位**，只有"响"和"不响"。想让报警更醒目只有两个办法：换无源蜂鸣器 + PWM 出不同频率的音调（但 PC13 没有定时器通道，得飞线），或者用 `BSP_DelayMs()` 断续响（`On → 200ms → Off → 800ms`）。

**用法三：语音控制开关（见 04 篇）**

天问模块收到指令后，MCU 直接 `LED1_On()` / `LED1_Off()`。**这是最简单的一条闭环**，适合作为 04 篇的验证用例。

### 第 5 步：验证

1. **最朴素的验证**：`App_Init()` 里 `LED_Init()` 之后加 `LED1_On(); LED2_On();`，上电后两个灯应该都亮。
   - **不亮**：99% 是"低电平点亮"这条记反了，改成 `LED1_Off()` 试试，亮了就说明接法是灌电流，把 `_On/_Off` 的宏对调即可。
   - **亮但很暗**：限流电阻太大，或者 LED 极性焊反了。
2. **蜂鸣器**：`Buzzer_On();` 上电后应该响一声。不响就先量 U4.2 有没有 3.3 V（响的时候），有电压而不响就是驱动能力不够（见上面那条警告）。
3. **配合 OLED**：按 [02-OLED显示屏移植.md](02-OLED显示屏移植.md) 把灯的状态显示到屏上，调试起来更方便。

---

## 四、移植清单（照着打勾）

**LED**
- [ ] `BSP/led/led.h` + `led.c` 新建（`LED1`/`LED2` 对应 PC14/PC15）
- [ ] `GPIO_MODE_OUTPUT_PP`，`GPIO_SPEED_FREQ_LOW`
- [ ] **`_On()` 写 `GPIO_PIN_RESET`**（灌电流，低电平点亮）
- [ ] Keil 加文件 + Include Path
- [ ] `App_Init()` 里 `LED_Init()`
- [ ] `LED.c` / `LED.h` / `PWM.c` / `PWM.h` **不要拷进工程**
- [ ] 确认板上的 R1/R11 不小于 1 kΩ（PC14/PC15 只有 3 mA）

**蜂鸣器**
- [ ] `BSP/buzzer/buzzer.h` + `buzzer.c` 新建（PC13）
- [ ] `Buzzer_On()` 写 `GPIO_PIN_SET`（高电平响，**逻辑和参考工程一致**）
- [ ] `App_Init()` 里 `Buzzer_Init()`
- [ ] 上电响一声验证
- [ ] 确认 `.ioc` 里 LSE 是 Disabled（PC14/PC15 拿来当 GPIO 的前提）
