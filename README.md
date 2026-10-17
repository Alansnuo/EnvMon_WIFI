# EnvMon_WIFI

STM32F103C8T6 + FreeRTOS 的环境监测网关：DHT11 采温湿度，经 ESP8266 用 MQTT 上报到 OneNET 物模型。

## 功能

- **值变化才上报** —— 采到的值先拼成 JSON，和上次发成功的那份比一遍，一样就丢掉；变了下一条立刻发
- **断线自恢复** —— 连续 3 次发不出去就复位模块、重接热点、重连 Broker，回到干净状态
- **只上报，不订阅** —— 订阅交给手机 / MQTTX 之类的客户端，板子不做

## 数据流

```
DHT11 任务 ──队列(4 条)──> WiFi 任务 ──AT 指令──> ESP8266 ──MQTT──> OneNET
  2 秒采样                  值变了才发
```

采集和通信各跑各的任务，队列解耦：网络卡住不会拖住采样。

## 硬件接线

| 功能            | 引脚        | 备注                        |
| --------------- | ----------- | --------------------------- |
| 天问语音模块    | PA9 / PA10  | USART1，115200，收发都用    |
| ESP8266         | PA2 / PA3   | USART2，115200              |
| ESP8266 RST     | PB1         | 推挽输出，低电平复位        |
| DHT11           | PB12        | 单总线，需 4.7k~10k 上拉    |
| BH1750 光照     | PB6 / PB7   | 软件 I2C（SCL / SDA）       |
| OLED 0.96"      | PB8 / PB9   | 软件 I2C（SCL / SDA），0x78 |
| 按键 KEY1~KEY4  | PA0/PA1/PA4/PA5 | 内部上拉，按下接地      |

> USART1 现在**专职语音**，没有 printf 重定向 —— 调试助手只能单向"听" PA9，
> **TX 绝对不能接到 PA10**（那根线上已有模块的 TX 在驱动，两个输出顶在一起会损 IO）。

## 显示界面

128×64 OLED，8×16 字体一屏 4 行；汉字 16×16，一个汉字占**两列**。
KEY1 在三页之间循环：

```
   主页面 PAGE_ENV              菜单 PAGE_MENU           编辑 PAGE_EDIT
 ┌──────────────────┐        ┌──────────────────┐     ┌──────────────────┐
 │12:00:03          │        │Menu 1/7          │     │Set TempHi        │
 │WiFi:OK           │  KEY1  │>TempHi      30 C │     │TempHi =   30 C   │
 │                  │  ───>  │ TempLo      10 C │     │K2+ K3- K4=OK     │
 │                  │  <───  │ HumHi       80 % │     │                  │
 └──────────────────┘        └──────────────────┘     └──────────────────┘
```

- **主页面第一行显示网络对时时间，第二行显示 WiFi 状态**，静态显示（不闪烁）
- 阈值超限时，主页面第 1 行右侧**闪烁**一个警告图案；同时语音任务播报告警
- 中文标签用的是 `Hzk1[30][32]` 字模表，下标和参考工程一一对应
- 时间来自 **ESP8266 网络对时**：热点连上后经 `ESP8266_GetNetTime()` 取一次北京时间
  写进软件时钟，之后用 OLED 任务的 1 秒周期当秒脉冲走字，断电归零、联网成功自动校准

### 两张汉字字模表的切换

`BSP/OLED/OLED_Font.h` 里放了**两张** `Hzk1`，用一个开关二选一：

| 开关 | 用哪张 |
|---|---|
| `#define HZK1_USE_REFERENCE_FONT`（文件里默认就是这行，第 343 行） | 参考工程原表 |
| 把那行注释掉 | `_syntax_check/gen_font.py` 重新生成的表 |

也可以改在 MDK 的 `Options for Target → C/C++ → Define` 里覆盖文件默认值。

为什么留两张：参考工程那张笔画偏细、还有几组下标**逐字节完全相同**
（`2/6/10` 都是"度"，`11/19` 都是"系"，`12/20` 都是"统"，`16/18` 都是"动"）；
重新生成的那张密度均匀。按驱动实际写屏的方式还原成点阵实测：

```
                参考表    生成表
  度            16行/76   16行/76   ← 这个字两张没差别
  式            16行/55   16行/73
  自            16行/57   16行/95
  火            16行/36   16行/57
```

⚠️ 两者对"度"都是完整 16 行 76 墨点，所以**如果"度"看起来只有一半，
换字模表不会解决问题** —— 那更可能是页写入、识别方向或 I2C 时序的问题，
得看实物照片才能定位。`_syntax_check/verify_font.py` 可以随时重新出这张对比表。

## 目录结构

```
APP/          业务层
  app_main.c      总入口：建数据通道 + 建任务
  app_ui.c        界面状态：软件时钟（网络对时） + 阈值 + 菜单 + 当前页面
  app_config.h    WiFi / MQTT / 阈值初值，换环境只改这里
  task_dht11.c    DHT11 采集任务
  task_light.c    BH1750 光照采集任务
  task_wifi.c     联网 + 上报任务（上报报文的拼装也在本文件里）
  task_oled.c     OLED 显示任务（全工程唯一碰屏的任务）
  task_key.c      按键扫描 + 界面动作
  task_asr.c      语音播报 / 告警
  app_delay.c     让 BSP_DelayMs 在调度器起来后改走 vTaskDelay
BSP/          板级驱动
  ESP8266/        AT 指令封装 + WiFi / MQTT 流程
  dht11/          DHT11 单总线时序
  BH1750/         光照传感器（软件 I2C）
  OLED/           SSD1306 驱动 + OLED_Font.h（ASCII 8x16 与汉字 16x16 字模）
  KEY/            4 个按键（扫描 + 消抖）
  ASR/            天问语音模块收发
  bsp_delay/      统一延时：裸机走 HAL_Delay
Core/         CubeMX 生成的 HAL 初始化
freertos/     FreeRTOS 内核 + 移植层
MDK-ARM/      Keil 工程
```

### 按键驱动（`BSP/KEY/`）

写法参照 `D:\Download\kile5\Project\C51\电子密码锁` 的 `BSP/Key/MatrixKey.c`：
直接读引脚、直接比较、返回"按了哪个键"，**不用结构体数组、不用指针、不做多层状态机**。

只有一处**必须**和参考工程不同：参考工程是裸机，主循环里可以

```c
if (P1_7 == 0) { Delay(20); while (P1_7 == 0); Delay(20); KeyNum = 1; }
```

也就是"按下 → 消抖 → 卡住等松手 → 才返回"。这里跑 FreeRTOS，**等松手等于把 CPU
让出去**（被人按住不放就永远不返回，别的任务全停）。所以改成：消抖确认按下就
立刻报一次，然后记住"这个键还没松手"，松手之前不再重复报 —— 效果一样，不阻塞。

对外就两个函数（4 个脚的输入+上拉由 CubeMX 的 `MX_GPIO_Init` 统一配置，不再有 `Key_Init`）：

| 函数 | 用途 |
|---|---|
| `Key_Event Key_Scan(uint8_t id)` | 每 `KEY_SCAN_MS`(20ms) 调一次，返回事件 |
| `uint8_t Key_GetNum(uint8_t id)` | 参考工程那种用法：按下返回 1~4，没动作返回 0 |

事件有 `KEY_EVENT_PRESS`（按下，一次按住只报一次）、`KEY_EVENT_LONG`（按住满 1 秒报一次）、
`KEY_EVENT_RELEASE`（松手）。界面只用 `PRESS`。

行为有测试台（`_syntax_check/key_test.c`，把真实的 `key.c` 编进 PC 程序里跑）：
短按只报一次、长按报一次且松手不补短按、连按三下正好 3 次、20ms 抖动不误报、
940ms 不报长按而 1100ms 报、非法编号不崩。`run_checks.ps1` 第 6 节会自动跑它。

## 三个设计取舍

**串口接收用 DMA 环形缓冲 + 轮询 CNDTR，不用接收中断。**
F1 的串口只有一个字节的接收寄存器，没有 FIFO。而 DHT11 读一次要关中断约 4ms，115200 下这段时间会来几十个字节，用 RXNE 中断收必丢。

**MQTT 报文走 `AT+MQTTPUB`，JSON 里的 `"` 和 `,` 得写成 `\"` 和 `\,`。**
AT 指令按逗号切参数，JSON 载荷里全是逗号和引号，不转义会被切成多余的参数，模块直接回 `+MQTTPUB:FAIL`。转义符是直接写在 `task_wifi.c` 里 `Json_Report()` 的格式串上的，没另做一层转义函数。

**按课设标准写，不按工业标准写。**
驱动的参数不做 `NULL` 检查，每条 AT 命令用一句 `snprintf` 拼完就发，超长会截断（由 `ESP8266_Exec` 里的长度检查兜住，不会发出半条）。目标是能跑通、能读懂，不是产品级加固。

## 编译

Keil MDK-ARM 打开 `MDK-ARM/project.uvprojx`（ARMCC V5.06）。
加了 OLED 汉字表（`Hzk1` 约 960 字节）和按键/语音任务之后，占用要重新看一次
Keil 的编译输出（`Program Size: Code=... RO-data=...`），README 里原来那组
数字是只有 DHT11 + WiFi 时的旧值，已经不适用。

### 没有 Keil 时的自检

`_syntax_check/` 是一套**最小桩头文件**（HAL / FreeRTOS / 各驱动的空实现），
配合 `arm-none-eabi-gcc` 可以在没有 Keil 的机器上把改过的文件真编译一遍，
用来抓语法错误、拼写错误、结构体字段对不上这类问题：

```powershell
powershell -NoProfile -File _syntax_check\run_checks.ps1
```

四项检查：

| 脚本 | 抓什么 |
|---|---|
| 逐文件 `-fsyntax-only -Wall -Wextra` | 语法 / 类型 / 拼写 / 未使用变量 |
| `check_font.c`（编译期断言） | 字库表维度必须是 `Hzk1[30][32]`、`OLED_F8x16[95][16]` |
| `check_dupes.py`（`nm` 扫符号表） | **全局符号重复定义**（就是 `L6200E: Symbol Hzk1 multiply defined` 那一类），逐文件语法检查**抓不到**这个，只有链接才报 |
| `check_layout.py` | 每个界面字段的列宽不超 16 列 |

⚠️ 两条硬规矩（都踩过）：

1. **`BSP/OLED/OLED_Font.h` 只能被 `BSP/OLED/OLED.c` 一个 .c 包含。**
   那里面 `Hzk1` / `OLED_F8x16` 是**定义**不是声明，第二个 .c 一包含就是
   `L6200E` 重复定义。要字形下标就自己写一份 `enum`（`APP/task_oled.c`
   就是这么做的）。
2. **桩头文件里不要重复定义 CubeMX / HAL 已有的东西** —— `GPIOA`/`GPIOB`、
   `GPIO_PIN_x`、`GPIO_MODE_*`、`__HAL_RCC_*_CLK_ENABLE`、`HAL_GPIO_*`
   全部由 `Core/Inc/main.h` → `stm32f1xx_hal.h` 提供。桩只补句柄类型
   （`UART_HandleTypeDef` 之类）。

⚠️ 桩头文件**不参与固件**，也不进 Keil 工程。

## 跑起来

在 `APP/app_config.h` 里填自己的 WiFi 和 OneNET 参数：

```c
#define CFG_WIFI_SSID         "奈何神明耍诈"
#define CFG_WIFI_PASSWORD     "G123456789"
#define CFG_MQTT_HOST         "mqtts.heclouds.com"
#define CFG_MQTT_PORT         1883
#define CFG_MQTT_CLIENT_ID    "test1"      /* 设备名 */
#define CFG_MQTT_USERNAME     "v9cSbzDFqr" /* 产品 ID */
#define CFG_MQTT_PASSWORD     "version=2018-10-31&res=products%2Fv9cSbzDFqr%2Fdevices%2Ftest1&et=1821308308&method=sha1&sign=9Cyhm1fn76GI5cURLVvT6AV4XCg%3D"
#define CFG_MQTT_PUB_TOPIC    "$sys/v9cSbzDFqr/test1/thing/property/post"
```

上报的属性名 `Temp` / `Hum` 必须和 OneNET 控制台「功能定义」里完全一致，大小写敏感 —— 写成 `temp` / `humi` 平台不收，而且不一定报错。

⚠️ **USART1 已经不打印日志了**：那一路串口现在专职接天问语音模块，`fputc`
重定向已删。要看运行日志得把 `printf` 重定向到别的串口（比如 USART3 的
PB10/PB11），**不要**改回 USART1 —— 调试文本会和语音数据帧糊在一起。
屏上的主页面/菜单页就是现在的"状态显示"：联网状态停在 `WiFi:...` / `AP` /
`ERR` 哪一档，就知道该查哪一段。
