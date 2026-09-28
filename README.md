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

| 功能            | 引脚        | 备注                  |
| --------------- | ----------- | --------------------- |
| 调试串口 printf | PA9 / PA10  | USART1，115200        |
| ESP8266         | PA2 / PA3   | USART2，115200        |
| ESP8266 RST     | PB1         | 推挽输出，低电平复位  |
| DHT11           | PB12        | 单总线                |

## 目录结构

```
APP/          业务层
  app_main.c      总入口：建数据通道 + 建任务
  app_config.h    WiFi / MQTT 参数，换环境只改这里
  task_dht11.c    DHT11 采集任务
  task_wifi.c     联网 + 上报任务
  task_light.c    光照任务（占位，未实现）
BSP/          板级驱动
  ESP8266/        AT 指令封装 + WiFi / MQTT 流程
  dht11/          DHT11 单总线时序
  bsp_delay/      统一延时：裸机走 HAL_Delay，调度器起来后自动走 vTaskDelay
Core/         CubeMX 生成的 HAL 初始化
freertos/     FreeRTOS 内核 + 移植层
MDK-ARM/      Keil 工程
```

## 两个设计取舍

**串口接收用 DMA 环形缓冲 + 轮询 CNDTR，不用接收中断。**
F1 的串口只有一个字节的接收寄存器，没有 FIFO。而 DHT11 读一次要关中断约 4ms，115200 下这段时间会来几十个字节，用 RXNE 中断收必丢。

**MQTT 报文走 `AT+MQTTPUB`，主题和载荷里的 `"` `\` `,` 都得转义。**
AT 指令按逗号切参数，JSON 载荷里全是逗号和引号，不转义会被切成多余的参数，模块直接回 `+MQTTPUB:FAIL`。

## 编译

Keil MDK-ARM 打开 `MDK-ARM/project.uvprojx`（ARMCC V5.06）。当前占用：

```
Code=16740  RO-data=440  RW-data=160  ZI-data=12912
```

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

USART1（115200）打印运行日志：

```
DHT11 Sensor Ready!
[DHT11] #1  Temp: 25 C  Humi: 60%
[WiFi] connected
[WiFi] MQTT connected
```
