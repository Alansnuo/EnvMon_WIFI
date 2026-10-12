# 02 OLED 显示屏移植

**定位**：把参考工程的 0.96" SSD1306 软件 I2C 驱动，从 PB0/PB1 改到 PB8/PB9，用 HAL 重写，并挂成一个低优先级 FreeRTOS 任务。

**读之前先补**：[00-总纲](00-总纲-PCB引脚表与移植决策.md)（机制一延时、机制三任务优先级/栈）。

**参考文件**：`1.源代码（面包板可用）/Hardware/OLED.h`、`OLED.c`、`OLED_Font.h`

---

## 一、作用原理

### 这块屏是什么

0.96 寸单色 OLED，**128×64 像素**，驱动芯片是 **SSD1306**。4 个脚：GND / VCC(3V3) / SCL / SDA。你的网表里 `OLED1` 就是它，`OLED1.1→GND`、`OLED1.2→3V3`、`OLED1.3→PB8`、`OLED1.4→PB9`。

### 为什么它能自己发光

OLED 是**有机发光二极管**，每个像素自己发光，**不需要背光**。所以它：

- 黑底是真黑（像素不发光），对比度极高；
- 静态显示功耗很低（只有点亮的像素耗电）；
- 但**烧屏**——同一个画面放太久，亮的像素会老化得比暗的快。

### 它是怎么被寻址的

SSD1306 的内部显存（GDDRAM）是**分页**的：128 列 × 8 页，每页 8 行像素，一共 64 行。

> **注意这个坑**：显存不是"按行"存的，是**按页**存的。一页 8 个像素在内存里是**竖着**的一个字节：bit0 是最上面那行，bit7 是最下面那行。

所以参考工程里的 `OLED_SetCursor(Y, X)` 参数 `Y` 是**页号 0~7**，不是像素行号。一个 16 像素高的字符要写**两次**：先写上半页（`(Line-1)*2`），再写下半页（`(Line-1)*2+1`）。这就是 `OLED_ShowChar()` 里那两段 for 循环的来历，不是啰嗦，是显存结构决定的。

`OLED_SetCursor` 发的三条命令：

```c
OLED_WriteCommand(0xB0 | Y);                 // 页地址
OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4)); // 列地址高 4 位
OLED_WriteCommand(0x00 | (X & 0x0F));        // 列地址低 4 位
```

### 字模

`OLED_Font.h` 里有两张表：

| 表 | 尺寸 | 内容 | 占用 |
|---|---|---|---|
| `OLED_F8x16[][16]` | 8 宽 × 16 高 | ASCII 0x20~0x7E，95 个 | 95×16 = 1520 B |
| `Hzk1[][32]` | 16 宽 × 16 高 | 30 个汉字 | 30×32 = 960 B |

加起来约 **2.5 KB Flash**。`EnvMon_WIFI` 现在 code 段才 16 KB，STM32F103C8 有 64 KB，**放得下**。你自己试试加更多汉字也行，一个字 32 字节。

因为是 8×16 字模，**一屏 4 行（`Line` 1~4）× 16 列（`Column` 1~16）**。

### 和 MCU 怎么通

**I2C，从机地址 0x3C**（7 位），写字节是 `0x3C << 1 = 0x78`。参考工程里写死的那个 `0x78` 就是它。

每次传输是固定的 3 字节格式：

```
[0x78] [0x00 / 0x40] [数据]
  从机     控制字节      载荷
```

控制字节 `0x00` = 后面这个是**命令**，`0x40` = 后面这个是**显示数据**。所有 `OLED_WriteCommand()` / `OLED_WriteData()` 的区别就这一个字节。

⚠️ 有些批次的 0.96" 模块地址是 **0x3D**（写字节 `0x7A`）。如果你的屏**上电后一直全黑、但背光供电和接线都没问题**，第一个要试的就是把 `0x78` 换成 `0x7A`。

---

## 二、代码原理

### 软件 I2C：和 BH1750 那份几乎一样

同样是 bit-bang：`OLED_I2C_Start/Stop/SendByte` 三个原语，逻辑和 01 篇讲的完全一致。

**但有两处和 BH1750 不同的设计**：

**第一，它不产生延时。** `OLED_I2C_SendByte()` 里没有 `Delay_us`：

```c
for (i = 0; i < 8; i++)
{
    OLED_W_SDA(Byte & (0x80 >> i));
    OLED_W_SCL(1);
    OLED_W_SCL(0);      // 拉高紧接着拉低, 靠 GPIO 速度撑起时钟
}
```

时钟周期就是几个 GPIO 写寄存器的开销，大概几百 kHz 到 1 MHz。**这是安全的做法**——SSD1306 的 I2C 没有最低速率要求，只有最高 400 kHz 的限制，而这个速度通常够不上。好处是刷屏快（全屏 1024 字节几十毫秒就完了）。

> 对比 01 篇的 BH1750：那边**必须加 5 μs 延时**，因为 BH1750 手册对时序有明确要求。两份驱动虽然同为软件 I2C，延时策略是**故意不同**的，别互相抄。

**第二，它不读应答。** 看这里：

```c
OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
OLED_W_SCL(0);
```

发完 8 位后，第九个时钟照打，但**不读 SDA**。协议上是"发送方在第九个时钟释放 SDA、接收方拉低表示 ACK"，这里当发送方但没去确认对方有没有拉低。

这个偷懒**在单主机单从机的场景下是可以接受的**：反正总线上只有一块屏，它不在场也没别的东西抢答。代价是**断线时你不会知道**——屏没插，程序照样跑完所有写操作，只是什么都不显示。这对"最短能跑通"是合适的取舍，不值得为它加代码。

### 开漏写得是对的

这点比 BH1750 那份好：

```c
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;   /* 开漏 */
```

I2C 就该开漏，参考工程这里没写错。所以 **OLED 那份驱动移植时不用改电气模式**，只改引脚号。

### `OLED_Init()` 里那个空循环

```c
for (i = 0; i < 1000; i++)          // 上电延时
{
    for (j = 0; j < 1000; j++);     // 空操作
}
```

两重空循环，编译器在 `-O0` 下大概几百毫秒，在 `-O2` 下**可能被优化掉**（虽然是 `uint32_t` 循环，编译器一般不会删，但这属于"靠天意"）。SSD1306 上电后内部要复位、充电泵要建立，手册要求等 100 ms 以上。**换成 `BSP_DelayMs(100)`**，意图明确且不会因优化等级而变。

### 初始化命令序列不用动

`0xAE`(关显示) → `0xD5/0x80`(时钟) → `0xA8/0x3F`(多路复用 64 行) → `0xD3/0x00`(偏移) → `0x40`(起始行) → `0xA1`(左右正) → `0xC8`(上下正) → `0xDA/0x12`(COM 配置) → `0x81/0xCF`(对比度) → `0xD9/0xF1`(预充电) → `0xDB/0x30`(VCOMH) → `0xA4`(跟随显存) → `0xA6`(正常显示) → `0x8D/0x14`(**开充电泵**) → `0xAF`(开显示)。

这一串是 SSD1306 的标准配方，**一个字都不用改**。特别提醒 `0x8D/0x14`（充电泵）**必须发**——0.96" 模块靠内部升压产生 OLED 需要的 ~7.5 V，不发这条屏就是黑的。

---

## 三、怎么移植到 PCB

### 第 1 步：拷文件 + 改引脚宏

把 `OLED_Font.h` 原样拷过来（**不动**），`OLED.c` / `OLED.h` 改写成 HAL 版。

新建 `BSP/oled/oled.h`：

```c
#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/* 软件 I2C 引脚 —— 对应 PCB 上 OLED1: 1.3→PB8(SCL), 1.4→PB9(SDA) */
#define OLED_SCL_PORT   GPIOB
#define OLED_SDA_PORT   GPIOB
#define OLED_SCL_PIN    GPIO_PIN_8
#define OLED_SDA_PIN    GPIO_PIN_9

#define OLED_ADDR_W     0x78        /* 0x3C << 1; 若全黑试 0x7A */

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t num);

#endif
```

**改哪几行**（和参考工程对照）：

| 参考工程 `OLED.h` | 改成 | 为什么 |
|---|---|---|
| `#define OLED_SCL GPIO_Pin_1` | `GPIO_PIN_8` | PCB 上 OLED1.3 走 PB8 |
| `#define OLED_SDA GPIO_Pin_0` | `GPIO_PIN_9` | PCB 上 OLED1.4 走 PB9 |
| `#define OLED_PROT GPIOB` | 拆成 `SCL_PORT` / `SDA_PORT` | 都在 GPIOB，拆开是为了配 BH1750 那份的风格一致 |
| `GPIO_WriteBit(PORT, PIN, (BitAction)x)` | `HAL_GPIO_WritePin(PORT, PIN, x ? GPIO_PIN_SET : GPIO_PIN_RESET)` | SPL → HAL |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE)` | `__HAL_RCC_GPIOB_CLK_ENABLE()` | SPL → HAL |
| `GPIO_InitStructure` 填结构体 | 换 `GPIO_InitTypeDef` + `HAL_GPIO_Init()` | SPL → HAL |
| 1000×1000 空循环 | `BSP_DelayMs(100)` | 见第二节 |

⚠️ **PB8/PB9 在参考工程里是 Key1/Key2**（`Key.h` 是 `GPIO_Mode_IPU`，走 TIM2 中断里扫描）。你的板子上**没有按键**，所以这两个脚空出来给 OLED 用了，不冲突。但如果你以后想加按键，**不能再放 PB6~PB9**——PB6/PB7 给 BH1750 了，PB8/PB9 给 OLED 了。

### 第 2 步：`BSP/oled/oled.c`（HAL 版）

```c
/* ============================================================================
 * 0.96" SSD1306 OLED 软件 I2C 驱动 (STM32F1 + HAL)
 * 两个 IO 开漏: 高电平=释放总线, 半周期不加延时, 靠 GPIO 速度撑时钟
 * 显存按页组织: 一页 8 行像素挤在一个字节里, 所以 16 高的字要分上下两页写
 * ========================================================================== */

#include "oled.h"
#include "oled_font.h"
#include "bsp_delay.h"

#define OLED_SCL_H()  HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_SET)
#define OLED_SCL_L()  HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_RESET)
#define OLED_SDA_H()  HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_SET)
#define OLED_SDA_L()  HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_RESET)

/* ------------------------------ 软件 I2C ------------------------------ */

static void OLED_I2C_Start(void)
{
    OLED_SDA_H(); OLED_SCL_H();
    OLED_SDA_L();                       /* SCL 为高时 SDA 下降沿 = 起始 */
    OLED_SCL_L();
}

static void OLED_I2C_Stop(void)
{
    OLED_SDA_L(); OLED_SCL_H();
    OLED_SDA_H();                       /* SCL 为高时 SDA 上升沿 = 停止 */
}

static void OLED_I2C_SendByte(uint8_t byte)
{
    uint8_t i;

    for (i = 0; i < 8U; i++)
    {
        if (byte & (0x80U >> i)) { OLED_SDA_H(); } else { OLED_SDA_L(); }
        OLED_SCL_H();
        OLED_SCL_L();
    }

    OLED_SCL_H();                       /* 第九个时钟: 不读应答 */
    OLED_SCL_L();
}

/* ------------------------------ 命令 / 数据 ------------------------------ */

static void OLED_WriteCommand(uint8_t cmd)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_ADDR_W);
    OLED_I2C_SendByte(0x00);            /* 控制字节: 后面是命令 */
    OLED_I2C_SendByte(cmd);
    OLED_I2C_Stop();
}

static void OLED_WriteData(uint8_t data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_ADDR_W);
    OLED_I2C_SendByte(0x40);            /* 控制字节: 后面是显示数据 */
    OLED_I2C_SendByte(data);
    OLED_I2C_Stop();
}

/* Y 是页号 0~7 (不是像素行号), X 是列 0~127 */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

/* ------------------------------ 对外接口 ------------------------------ */

void OLED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* I2C 必须开漏 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;  /* 这里靠 GPIO 速度撑时钟, 用 HIGH */
    HAL_GPIO_Init(OLED_SCL_PORT, &gpio);

    OLED_SCL_H();
    OLED_SDA_H();

    BSP_DelayMs(100);                   /* 等屏内部复位和充电泵建立 */

    OLED_WriteCommand(0xAE);            /* 关显示 */
    OLED_WriteCommand(0xD5); OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8); OLED_WriteCommand(0x3F);   /* 64 行 */
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14);   /* 开充电泵, 不发就是黑屏 */
    OLED_WriteCommand(0xAF);            /* 开显示 */

    OLED_Clear();
}

void OLED_Clear(void)
{
    uint8_t page, col;

    for (page = 0; page < 8U; page++)
    {
        OLED_SetCursor(page, 0);
        for (col = 0; col < 128U; col++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;

    OLED_SetCursor((uint8_t)((Line - 1U) * 2U), (uint8_t)((Column - 1U) * 8U));
    for (i = 0; i < 8U; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);      /* 上半页 */
    }
    OLED_SetCursor((uint8_t)((Line - 1U) * 2U + 1U), (uint8_t)((Column - 1U) * 8U));
    for (i = 0; i < 8U; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8U]); /* 下半页 */
    }
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;

    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i), String[i]);
    }
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t r = 1U;
    while (Y--) { r *= X; }
    return r;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i),
                      (char)(Number / OLED_Pow(10U, Length - i - 1U) % 10U + '0'));
    }
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t n;

    if (Number >= 0) { OLED_ShowChar(Line, Column, '+'); n = (uint32_t)Number; }
    else             { OLED_ShowChar(Line, Column, '-'); n = (uint32_t)(-Number); }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i + 1U),
                      (char)(n / OLED_Pow(10U, Length - i - 1U) % 10U + '0'));
    }
}

void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i, single;

    for (i = 0; i < Length; i++)
    {
        single = (uint8_t)(Number / OLED_Pow(16U, Length - i - 1U) % 16U);
        OLED_ShowChar(Line, (uint8_t)(Column + i),
                      (char)(single < 10U ? (single + '0') : (single - 10U + 'A')));
    }
}

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i),
                      (char)(Number / OLED_Pow(2U, Length - i - 1U) % 2U + '0'));
    }
}

void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t num)
{
    uint8_t i;

    OLED_SetCursor((uint8_t)((Line - 1U) * 2U), (uint8_t)((Column - 1U) * 16U));
    for (i = 0; i < 16U; i++) { OLED_WriteData(Hzk1[num][i]); }
    OLED_SetCursor((uint8_t)((Line - 1U) * 2U + 1U), (uint8_t)((Column - 1U) * 16U));
    for (i = 0; i < 16U; i++) { OLED_WriteData(Hzk1[num][i + 16U]); }
}
```

**要点说明**

1. **`OLED_SetCursor` / `OLED_Clear` / 两个 `OLED_WriteXxx` 改成了 `static`**。参考工程把它们放在头文件里暴露出去，但外部根本不需要——**少暴露就少踩**。`OLED_Pow` 保留非 static 是为了和参考工程一致（外部没人用，也可以改 static）。
2. `OLED_ShowBinNum` 参考工程里是 `Number / (1 << (Length - i - 1)) % 2`，这里换成 `OLED_Pow(2, ...)`，逻辑等价，省得两种写法混着看。
3. **`K` 变量名冲突提醒**：`OLED_F8x16` 这个名字原样保留，所以 `oled_font.h` 里的两张表名字**不要改**。

### 第 3 步：拷字模 + 加进工程

- 把 `OLED_Font.h` 拷到 `BSP/oled/oled_font.h`，**内容一字不改**；
- Keil 工程里加两个 Group：`BSP/oled/oled.c`、`BSP/oled/oled_font.h`（`.h` 不用加进编译，但要保证 Include Path 里有 `BSP/oled`）。

### 第 4 步：建 OLED 任务

**关键设计决定：全工程只有 OLED 任务碰这块屏。**

别的任务（DHT11、光照、WiFi）只往全局变量里写值，OLED 任务读出来刷。这样就不需要任何互斥锁——因为**只有一个人在写 I2C**。如果你让多个任务都去调 `OLED_ShowString()`，两次刷屏的 I2C 事务会交错，屏上会出现随机乱码。

在 `APP/app_main.h` 里加一个显示用的结构体和全局量：

```c
typedef struct {
    uint8_t  temp;        /* 摄氏度 */
    uint8_t  humi;        /* 百分比 */
    uint16_t lux;         /* 照度 */
    uint8_t  wifiOk;      /* 1 = 已连上 OneNET */
} OledData;

extern OledData g_oledData;
```

`APP/task_oled.c`：

```c
/* ============================================================================
 * OLED 显示任务: 全工程唯一碰屏的任务, 别的任务只往 g_oledData 里写值
 * 优先级给最低(1): 刷屏是慢速外设, 只能捡空闲时间做, 不能抢 WiFi 和采集
 * ========================================================================== */

#include "task_oled.h"
#include "app_main.h"
#include "oled.h"

OledData g_oledData;

void TaskOled(void *argument)
{
    char buf[17];

    OLED_Init();
    OLED_ShowString(1, 1, "STM32 EnvMon");

    for (;;)
    {
        snprintf(buf, sizeof(buf), "Temp: %2u C  ", g_oledData.temp);
        OLED_ShowString(2, 1, buf);

        snprintf(buf, sizeof(buf), "Humi: %2u %%  ", g_oledData.humi);
        OLED_ShowString(3, 1, buf);

        snprintf(buf, sizeof(buf), "Lux : %4u   ", g_oledData.lux);
        OLED_ShowString(4, 1, buf);

        vTaskDelay(pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}

void TaskOledCreate(void)
{
    if (xTaskCreate(TaskOled, "Oled",
                    APP_STACK_OLED, NULL,
                    APP_PRIO_OLED, NULL) != pdPASS)
    {
        printf("ERROR: create OLED task failed\r\n");
        Error_Handler();
    }
}
```

`APP/app_main.h` 里补：

```c
#define APP_OLED_PERIOD_MS   500
#define APP_PRIO_OLED        1        /* 最低: 刷屏不能抢采集和联网 */
#define APP_STACK_OLED       256
```

⚠️ **为什么 `OLED_Init()` 放在任务里而不是 `App_Init()` 里**：任务里调就没有"调度器没起来"这个分支，简单；而且 `OLED_Init()` 里有 `BSP_DelayMs(100)`，在任务里是 `vTaskDelay`（让出 CPU），在 `App_Init()` 里是忙等。放任务里对启动速度也更友好。

⚠️ **`snprintf` 要 `#include <stdio.h>`**，并且链接时要保证 `printf` 的浮点支持没被打开（这里全是整数，不需要）。

⚠️ **`char buf[17]`**：一屏 16 列，加结束符正好 17。**别写 `char buf[16]`**，那会截掉最后一个字符。

### 第 5 步：`App_Init()` 里挂上

```c
TaskWifiCreate();
TaskDht11Create();
TaskLightCreate();
TaskOledCreate();        /* ← 新增 */
```

### 第 6 步：别的任务往里写值

`APP/task_dht11.c` 里读到数据后加一句：

```c
g_oledData.temp = dht.temperature;
g_oledData.humi = dht.humidity;
```

`APP/task_light.c` 里：

```c
g_oledData.lux = (uint16_t)lux;
```

`APP/task_wifi.c` 里连上/断开时：

```c
g_oledData.wifiOk = 1;   /* 或 0 */
```

**这些都是单字节/双字节写**，在 Cortex-M3 上对 `uint8_t`/`uint16_t` 的赋值是原子的，不会读到"写了一半"的值，**不需要临界区**。

### 第 7 步：验证

1. **只调 `OLED_Init()`**，观察屏——上电后应该闪一下（清屏），然后全黑。
2. **`OLED_ShowString(1, 1, "Hello")`**，屏上第一行出现 `Hello`。
   - **全黑**：先换地址试试 `0x7A`；再确认 PB8=SCL / PB9=SDA 有没有接反（这是最常见的错）；再确认 `0x8D/0x14` 充电泵那条有没有发。
   - **显示错乱、有雪花**：多半是 GPIO 速度档位太低。`OLED_SCL_PIN|OLED_SDA_PIN` 那句的 `Speed` 改成 `GPIO_SPEED_FREQ_HIGH`（已经是了），再不行就把 `BSP_DelayUs(1)` 塞进 `OLED_I2C_SendByte` 的两个时钟边沿之间降速。
   - **整屏反了 / 上下颠倒**：改 `0xA1`↔`0xA0`（左右）、`0xC8`↔`0xC0`（上下）。
3. **挂上任务**，屏幕上应该每 500 ms 刷新一次温湿度。
4. **故意制造抖动**：把 `APP_OLED_PERIOD_MS` 改成 50，看 WiFi 还连不连得上、DHT11 任务还准不准。如果连不上，说明 OLED 任务优先级给高了或者刷屏太频繁——改回 500 ms。
5. **看栈**：`OLED_ShowString` 内部调用链比较深（`snprintf` 本身吃栈），如果 `uxTaskGetStackHighWaterMark()` 剩余不足 50 字，把 `APP_STACK_OLED` 从 256 加到 384。

### 内存账（加完 OLED 之后）

| 用途 | 优先级 | 栈（字） | 字节 |
|---|---|---|---|
| 定时器服务 | 4 | 128 | 512 |
| WiFi | 3 | 512 | 2048 |
| DHT11 | 2 | 256 | 1024 |
| 光照 | 1 | 256 | 1024 |
| **OLED** | **1** | **256** | **1024** |
| idle | 0 | 128 | 512 |
| 两个队列 | — | — | ~100 |

**约 6.2 KB / 10 KB**。还够。再加语音任务就是 ~7.2 KB，仍然放得下，但如果还想加东西，考虑把 `configTOTAL_HEAP_SIZE` 提到 12288（12 KB）——STM32F103C8 有 20 KB RAM，当前 ZI-data 是 13 KB，还有余量。

---

## 四、移植清单（照着打勾）

- [ ] `BSP/oled/oled_font.h`（从参考工程原样拷贝，内容不改）
- [ ] `BSP/oled/oled.h`：`OLED_SCL_PIN`/`OLED_SDA_PIN` 改成 `GPIO_PIN_8`/`GPIO_PIN_9`
- [ ] `BSP/oled/oled.c`：全部 SPL 调用换成 HAL，空循环换 `BSP_DelayMs(100)`
- [ ] Keil 工程加文件 + Include Path 加 `BSP/oled`
- [ ] `APP/task_oled.c/h` 新建，`OledData` 结构体放在 `app_main.h`
- [ ] `APP/app_main.h` 加 `APP_OLED_PERIOD_MS` / `APP_PRIO_OLED` / `APP_STACK_OLED`
- [ ] `APP/app_main.c` 加 `TaskOledCreate()`
- [ ] DHT11 / 光照 / WiFi 任务往里写 `g_oledData`
- [ ] 屏上能看到 `Temp` / `Humi` / `Lux` 三个值
- [ ] 确认加了 OLED 任务后 WiFi 依然能连上（优先级没给高）
