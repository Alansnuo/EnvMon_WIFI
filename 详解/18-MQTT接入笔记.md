# MQTT 接入笔记（OneNET 云平台）

> 目标：把现在"裸 TCP 发数据"的链路，改成"通过 MQTT 协议上报到 OneNET"。
>
> 本笔记先讲**为什么**，再讲**改哪里**。动手之前请务必先做完第一节。

---

## 目录

- [一、动手前必须先确认的两件事](#一动手前必须先确认的两件事)
- [二、你给的连接信息逐条解读](#二你给的连接信息逐条解读)
- [三、决定：走 1883 明文](#三决定走-1883-明文)
- [四、两条实现路线](#四两条实现路线)
- [五、改动清单](#五改动清单)
- [六、MQTT 报文长什么样](#六mqtt-报文长什么样)
- [七、Keep Alive 与自动重连：MQTTX 帮你做的，你得自己写](#七keep-alive-与自动重连mqttx-帮你做的你得自己写)
- [八、调试步骤](#八调试步骤)

---

## 一、动手前必须先确认的两件事

**不要跳过这一节。** 这两件事的答案决定了改动量差 10 倍。

### 1.1 你的 ESP8266 固件支不支持 MQTT AT 指令？

**ESP8266 不是 ESP32。** `AT+MQTTUSERCFG` / `AT+MQTTCONN` 这一整套 MQTT AT 指令是
**ESP32 的 AT 固件**才有的。ESP8266 的官方 AT 固件大概率没有；
但市面上有些第三方/定制固件带。

串口发这三条命令，看返回：

```
AT+GMR              ← 看固件版本和 AT 版本号
AT+MQTTUSERCFG=?    ← 返回 OK 或一串参数说明  →  支持
                    ← 返回 ERROR              →  不支持
AT+CIPSTART=?       ← 看类型列表里有没有 "SSL"
```

### 1.2 记下答案，对照下表选路线

| `AT+MQTTUSERCFG=?` | 走哪条路线 |
|---|---|
| 支持 | **路线 A**（最省事，见第四节） |
| 不支持 | **路线 B**（自己拼 MQTT 报文，工作量大但通用） |

---

## 二、你给的连接信息逐条解读

> 📌 **表格已按最新配置更新**。最初给的是 TLS 那份（`mqtts://` / `8883`），
> 后来改成了明文的（`mqtt://` / `1883`）。**划掉的字段就是这次改掉的。**

| 字段 | 值 | 进代码吗 | 说明 |
|---|---|---|---|
| 名称 | `Decive` | ❌ | 只是 MQTTX 里给这条连接起的**备注名**，协议里不存在 |
| 协议 | ~~`mqtts://`~~ → `mqtt://` | ✅ | **改动项**。`mqtts://` 表示 TLS，`mqtt://` 是明文 |
| 服务器地址 | `mqtts.heclouds.com` | ✅ | **改动项**（原为 `mqttstls.heclouds.com`，那是 TLS 专用域名） |
| 端口 | `1883` | ✅ | **改动项**（原为 8883，那是 TLS 端口） |
| **Client ID** | `test1` | ✅ | = 设备名称。**MQTT CONNECT 报文里要填** |
| **用户名** | `v9cSbzDFqr` | ✅ | = 产品 ID。**MQTT CONNECT 报文里要填** |
| **密码** | `version=2018-10-31&res=...` | ✅ | = 鉴权 Token，124 字节。**MQTT CONNECT 报文里要填** |
| ~~SSL/TLS~~ | ~~开启~~ | ❌ | **改动项**。走明文就不开 TLS 了 |
| ~~SSL 安全~~ | ~~关闭~~ | ❌ | 明文下无意义 |
| ~~证书类型~~ | ~~CA signed server certificate~~ | ❌ | 明文下无意义。以后上 TLS 才要处理 |
| **MQTT 版本** | `3.1.1` | ✅ | 决定 CONNECT 报文里"协议级别"那个字节 = `0x04` |
| **连接超时** | 10 秒 | ✅ | 等 CONNACK 的超时时间 |
| **Keep Alive** | 60 秒 | ✅ | 见第七节，**要改任务结构** |
| 自动重连 | 开启 | ✅ | **MQTTX 的功能，不是协议要求 —— 得自己实现** |
| 重连周期 | 4000 ms | ✅ | 同上，自己实现 |

**改掉的 5 个字段里，只有前 4 个进代码；后 3 个（SSL 相关）是纯粹删掉。**

### 2.1 关于那个 Token

```
version=2018-10-31
&res=products%2Fv9cSbzDFqr%2Fdevices%2Ftest1      ← products/{产品ID}/devices/{设备名}
&et=1821308308                                     ← 过期时间戳(Unix 秒)
&method=sha1                                       ← 签名算法
&sign=9Cyhm1fn76GI5cURLVvT6AV4XCg%3D               ← HMAC-SHA1 签名(base64, 再 URL 编码)
```

**它不是随便一串字符，是用"设备密钥"算出来的。** 算法：

```
sign = base64( HMAC-SHA1(设备密钥, "{et}
{method}
{res}
{version}") )
```

⚠️ 四个字段之间是**换行符 `
` 分隔，不是 `&`**，顺序也固定是
et → method → res → version。分隔符或顺序错一个，sign 就对不上，
而平台只会回一个含义模糊的 `CONNACK code 4: 用户名或密码错误`。

`et=1821308308` 换算过来是 **2027-09-18**，所以这个 Token 一年内有效。

⚠️ **`et` 永远是 10 位**（10 位能表示到 2286 年）。抄成 11 位就会落进两千多年后 ——
本项目就栽在这上面：`et` 里多打了一个 `1` 变成 `18211308308`，换算过来是 2547 年，
而 `sign` 是对着**正确**的值算的，于是验签不过。设备死活连不上，平台上只显示"离线"，
代码和硬件全是好的。完整复盘见 `APP/app_config.h` 里那段警告。

**实践建议：在电脑上用 OneNET 的 Token 工具生成好，直接把结果硬编码进
`app_config.h`。** 不要在 STM32 上算 —— HMAC-SHA1 要额外百来行加密代码，
而 Token 又基本不用换。

代价：以后要换 Token 必须重新烧录。可以接受。

> ⚠️ 如果平台报 `MQTT_BAD_USERNAME_OR_PASSWORD`，先试把 Token 里的
> `%2F` `%3D` 做 URL 解码后再填（不同平台版本要求不一致）。

---

## 三、决定：走 1883 明文

> **结论已定** —— 用 `mqtt://` + `mqtts.heclouds.com` + `1883`（明文）。
> 这一节记录**为什么这么定**、**放弃了什么**。以后想上 TLS 的时候，
> 回来看这节就知道当初卡在哪。

### 3.1 原来的配置：被放弃的 TLS 方案

MQTTX 里原来那份是 `mqtts://` + `mqttstls.heclouds.com` + `8883`，带 TLS。
**在 ESP8266 上代价太大：**

TLS 握手要跑 RSA/ECC 运算、要缓存证书链，内存开销很大。
ESP8266 可用堆通常只有 40~50KB，还要留给自己跑 WiFi 协议栈。

实测反馈：ESP8266 AT 固件设 TLS 相关参数时容易直接报
**`0x6009 TLS config error`** 或连接超时。

**TLS 加解密是在 ESP8266 里做的，不是 STM32。** 所以 STM32F103C8 只有
20KB RAM 这件事反而不影响 —— 瓶颈在 ESP8266 那头。

### 3.2 两个端点对照

| 用途 | MQTTX 里选 | 域名 | 端口 |
|---|---|---|---|
| **本项目用这个** | `mqtt://` | `mqtts.heclouds.com` | **1883** |
| 不用 | `mqtts://` | `mqttstls.heclouds.com` | 8883 |

⚠️ **`mqtts.heclouds.com` 里的 `s` 不代表加密。** 那是 OneNET 的历史命名，
1883 就是明文。真正加密的域名是 `mqttstls`（多一个 `tls`），端口 8883。

**判断明文还是加密，看的是「端口 + MQTTX 里选的协议头」，不是域名。**

### 3.3 代价：认了

**密码会以明文经过网络**，那个 124 字节 Token 会被同一网段的人抓到。

对环境监测 demo 可以接受。**如果要做成产品，必须回来上 TLS。**

### 3.4 这个决定最大的好处

**`BSP/ESP8266/` 一行都不用改。**

现有的 `ESP8266_TCPConnect(host, port)` 干的就是
`AT+CIPSTART="TCP","host",port` —— 1883 明文要的正好就是这个。
只有 TLS 才需要新写 `ESP8266_SSLConnect()`（走 `AT+CIPSTART="SSL"`，
还得先发 `AT+CIPSSLSIZE` 申请缓冲区）。

**所以这次改动被锁死在 `APP/` 里**：传输层完全不动，协议层和应用层是新增的。

---

## 四、两条实现路线

传输层定了（`ESP8266_TCPConnect` + 1883），剩下一个问题：
**MQTT 报文谁拼？**

### 路线 A：让 ESP8266 固件拼

固件要是自带 MQTT 指令（`AT+MQTTUSERCFG=?` 返回 OK），可以直接让模块干：

```
AT+MQTTUSERCFG=0,1,"test1","v9cSbzDFqr","<Token>",0,0,""
AT+MQTTCONNCFG=0,60,0,"","",0,0
AT+MQTTCONN=0,"mqtts.heclouds.com",1883,0
AT+MQTTCONN?
```

- 第 2 个参数 `1` = MQTT over TCP（填 TLS 相关值就是 `0x6009`）
- STM32 侧只要按现有 `ESP8266_SendCmd()` 的模式封装几条命令
- **不用写 MQTT 报文拼装** —— 省掉 `APP/mqtt.c` 一整个文件

⚠️ **但这套指令是 ESP32 AT 固件的。** ESP8266 的官方 AT 固件
（就是 `esp8266.c:358` 注释里提到的 v1.x / v2.x）**没有**。
只有第三方定制固件才可能有。

### 路线 B：STM32 自己拼

ESP8266 只当一根"透明管道"：

```
STM32 自己拼好的 MQTT 二进制字节  →  AT+CIPSEND  →  ESP8266  →  服务器
```

- 工作量大，`APP/mqtt.c` 大约 250 行
- **不依赖固件能力**：以后换 ESP32、换 4G 模块、换以太网，这份代码照用
- 能真正看懂 MQTT 协议本身，而不是背几条 AT 命令

### 怎么选

先跑这一条：

```
AT+MQTTUSERCFG=?
```

| 返回 | 走哪条 |
|---|---|
| 一串参数说明 / `OK` | 路线 A（省事） |
| `ERROR` | 路线 B（没得选） |
| 不确定 / 懒得测 | **走 B** |

**不确定就走 B。** B 在任何固件上都能跑，A 只在特定固件上能跑 ——
而且 B 写出来的东西是能带走的，A 换块模块就白写了。

---

## 五、改动清单

以**路线 B**（STM32 自己拼报文 + 明文 1883）为准。

### 5.1 新建 `APP/app_config.h`

把所有**部署参数**集中到一处。改密码/换服务器只动这一个文件。

```c
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ---- WiFi ---- */
#define CFG_WIFI_SSID           "奈何神明耍诈"
#define CFG_WIFI_PASSWORD       "G123456789"

/* ---- MQTT (OneNET) ---- */
#define CFG_MQTT_HOST           "mqtts.heclouds.com"    /* 明文端点; TLS 是 mqttstls.heclouds.com */
#define CFG_MQTT_PORT           1883                    /* TLS 是 8883 */
#define CFG_MQTT_CLIENT_ID      "test1"                 /* = 设备名称 */
#define CFG_MQTT_USERNAME       "v9cSbzDFqr"            /* = 产品 ID  */
#define CFG_MQTT_PASSWORD       "version=2018-10-31&res=products%2Fv9cSbzDFqr..."  /* Token */
#define CFG_MQTT_KEEPALIVE_S    60                      /* 秒, 和 MQTTX 里一致 */
#define CFG_MQTT_TIMEOUT_MS     10000                   /* 等 CONNACK, 10 秒 */

/* ---- 上报 ---- */
/* 采集周期不在这里 —— 它在 APP/app_main.h:37, 叫 APP_DHT11_PERIOD_MS (2000).
 * app_config.h 只放"换设备/换平台要改的部署参数", 采集周期属于任务参数. */

#endif /* __APP_CONFIG_H */
```

### 5.2 新建 `APP/mqtt.c` / `APP/mqtt.h`

MQTT 3.1.1 报文的**编解码**。纯字节操作，**不碰硬件、不碰 FreeRTOS**。

报文的拼装缓冲放在 `mqtt.c` 内部（外加一个 256 字节的发送缓冲），
**调用方不接触缓冲区**，只需要发一条命令、拿一个成败。对外就三个函数：

```c
uint8_t MQTT_Connect(void);                                        /* 发 CONNECT, 等 CONNACK */
uint8_t MQTT_Publish(const char *topic, const char *payload);      /* 发 PUBLISH (QoS 0) */
uint8_t MQTT_Ping(void);                                           /* 发 PINGREQ, 等 PINGRESP */
```

返回值统一是 `1 = 成、0 = 败`。`MQTT_Connect` 失败时会把 CONNACK 的返回码
翻译成人话打到串口上（`mqtt.c` 里的 `Mqtt_ConnackReason`）。

放在 `APP/` 而不是 `BSP/`：它是**应用层协议**，不含任何板级信息。

### 5.3 改 `APP/task_wifi.c`

| 函数 | 改什么 |
|---|---|
| `WifiConnect()` | 末尾加：`ESP8266_TCPConnect(CFG_MQTT_HOST, CFG_MQTT_PORT)` → 拼 CONNECT → 发 → 等 CONNACK |
| `TaskWifi()` | ① `portMAX_DELAY` 改成**带超时的接收**（见第七节）<br>② 收到数据 → 拼 PUBLISH → `ESP8266_TCPSend()`<br>③ 超时醒来 → 发 PINGREQ |

### 5.4 不用改的

| 文件 | 为什么 |
|---|---|
| `BSP/ESP8266/esp8266.c/.h` | 1883 走的就是现有的 `ESP8266_TCPConnect` + `ESP8266_TCPSend` |
| `Core/Src/main.c` | 装配方式没变 |
| `APP/task_dht11.c` | 采集逻辑和数据通道都没变 |

> `ESP8266_TCPConnect()` 的参数名叫 `ip`，但**传域名也能用** ——
> ESP8266 会自己做 DNS 解析。

### 5.5 以后要上 TLS 的话（现在不做）

| 文件 | 加什么 |
|---|---|
| `esp8266.c/.h` | `ESP8266_SSLConnect(const char *host, uint16_t port)`<br>先发 `AT+CIPSSLSIZE=4096` 申请缓冲区，再发 `AT+CIPSTART="SSL","host",port` |
| `app_config.h` | 主机名换回 `mqttstls.heclouds.com`，端口换 8883 |
| `task_wifi.c` | 只改调用：`ESP8266_TCPConnect` → `ESP8266_SSLConnect`<br>**`mqtt.c` 一个字都不用动** |

**注意 SSL 连接必须传域名，不能传 IP** —— 证书校验和 SNI 都依赖域名。

> 💡 这就是"传输层和协议层分开"的好处：`mqtt.c` 只管往一个
> 已经连好的管道里塞字节，管道是明文还是加密的它不关心。
> 换 TLS 只动 `BSP` 那一层，上面全部不动。

---

## 六、MQTT 报文长什么样

用你实际的信息手算一遍。**这是理解 MQTT 最直接的方式。**

### 6.1 CONNECT 报文的三个部分

```
┌─────────────────────────────────────────────┐
│ 固定头 (Fixed Header)                        │
│   字节1: 报文类型(4bit) + 标志(4bit)          │
│   字节2~: 剩余长度(变长编码)                   │
├─────────────────────────────────────────────┤
│ 可变头 (Variable Header)                     │
│   协议名 + 协议级别 + 连接标志 + Keep Alive    │
├─────────────────────────────────────────────┤
│ 载荷 (Payload)                               │
│   Client ID + 用户名 + 密码                   │
└─────────────────────────────────────────────┘
```

### 6.2 逐字节算出来

```
固定头:
  10                    ← 0x10 = CONNECT 类型(1) << 4
  9B 01                 ← 剩余长度 = 155, 变长编码占 2 字节

可变头:
  00 04 4D 51 54 54     ← 协议名 "MQTT" (长度 4)
  04                    ← 协议级别 = 4 → MQTT 3.1.1
  C2                    ← 连接标志: 用户名(0x80) + 密码(0x40) + 清理会话(0x02)
  00 3C                 ← Keep Alive = 60 秒

载荷:
  00 05 74 65 73 74 31                    ← Client ID "test1"
  00 0A 76 39 63 53 62 7A 44 46 71 72     ← Username "v9cSbzDFqr"
  00 7C <124 字节 Token>                   ← Password (0x7C = 124)
```

### 6.3 长度怎么算出来的

```
可变头    10  = 2+4(协议名) + 1(级别) + 1(标志) + 2(KeepAlive)
ClientID   7  = 2(长度前缀) + 5("test1")
Username  12  = 2 + 10("v9cSbzDFqr")
Password 126  = 2 + 124(Token)
─────────────────────────────────────
剩余长度 155
整包     158 字节
```

### 6.4 剩余长度的变长编码（最容易写错的地方）

规则：**每字节 7 位数据 + 最高位是续位标志**，最多 4 字节。

```
155 = 0b1001_1011
  低 7 位   = 0b001_1011 = 27 = 0x1B  → 高位补续位 → 0x9B
  剩下     = 155 / 128 = 1            → 还有内容   → 0x01
结果: 9B 01
```

写代码时用循环，别硬编码：

```c
do {
    uint8_t b = length % 128;
    length /= 128;
    if (length > 0) { b |= 0x80; }   /* 后面还有字节 */
    *p++ = b;
} while (length > 0);
```

**这个函数写错，服务器会直接断开连接或返回格式错误，而且不会有任何有用的提示。**

### 6.5 其他要拼的报文

| 报文 | 首字节 | 内容 | 何时用 |
|---|---|---|---|
| PUBLISH | `0x30` | 主题 + 载荷 | 上报数据 |
| PINGREQ | `0xC0` | 空 | 保活 |
| DISCONNECT | `0xE0` | 空 | 主动断开 |

服务器回来的：

| 报文 | 首字节 | 要做什么 |
|---|---|---|
| CONNACK | `0x20` | 检查第 4 字节的返回码，`0x00` 才是成功 |
| PINGRESP | `0xD0` | 收到就说明链路还活着 |
| SUBACK / PUBACK | `0x90` / `0x40` | QoS0 用不到 |

> **QoS 选 0。** 选 1 要多实现 PUBACK 确认和重传逻辑，
> 对 2 秒一次的温湿度上报不值得。OneNET 也主要按 QoS0 用。

---

## 七、Keep Alive 与自动重连：MQTTX 帮你做的，你得自己写

### 7.1 这是最容易被忽略的认知点

在 MQTTX 里，"Keep Alive 60 秒""自动重连 4000ms"是**填两个框**；
自己写代码，是**写两段逻辑**。

**MQTTX 是个完整的 MQTT 客户端**，它内部一直在跑定时器、维护状态机。
你的 STM32 现在什么都没有，这些都得从零建。

### 7.2 Keep Alive 会直接改动你的任务结构

现在的 `TaskWifi()` 长这样：

```c
for (;;)
{
    xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY);   /* ← 永久阻塞 */
    /* 发数据 */
}
```

**问题**：`portMAX_DELAY` 意味着队列一直没数据就永远不醒。
而 Keep Alive 要求**每 60 秒至少发一次 PINGREQ**，否则服务器
（OneNET 是 1.5 倍 Keep Alive 时间，即 90 秒）会主动断开。

**必须改成带超时的等待**，让任务定期醒来：

```c
for (;;)
{
    /* 超时给 KeepAlive 的一半, 保证心跳不会迟到 */
    if (xQueueReceive(g_xDht11Queue, &dht,
                      pdMS_TO_TICKS(CFG_MQTT_KEEPALIVE_S * 1000 / 2)) == pdPASS)
    {
        /* 有数据 → 发 PUBLISH */
    }
    else
    {
        /* 超时醒来 → 发 PINGREQ */
    }
}
```

**要点**：超时时间取 Keep Alive 的**一半**，这样即使某次心跳丢了，
下一个周期还能补上，不会踩到 90 秒的断线线。

### 7.3 自动重连要自己写状态机

MQTTX 的"4000ms 重连周期"意味着它在做：

```
连接断开 → 等 4 秒 → 重新 TCP 连接 → 重新发 CONNECT → 重新等 CONNACK
                    ↑______________________________|
                              失败就循环
```

你的代码里对应的是：

```c
static uint8_t MqttIsConnected = 0;

/* 发 PINGREQ 后等 PINGRESP; 等不到 → 标记断开 → 走重连 */
/* PUBLISH 后如果 ESP8266 返回 SEND FAIL → 也标记断开 */
```

**注意**：连接断了之后，**DHT11 采集不能停**。数据通道是队列，
队列满了就丢（`xQueueSend(..., 0)` 已经是这个行为），这是对的 ——
网络恢复后从新数据开始发，不补发旧的。

---

## 八、调试步骤

**不要一次全写完再烧。** 按下面顺序分段验证，每步都要有明确的成功标志。

### 第 1 步：先确认 MQTTX 能连上

你已经在 MQTTX 里连上了吗？**如果 MQTTX 都连不上，代码肯定连不上。**

先把 MQTTX 用同样的参数（Client ID / 用户名 / 密码）连成功，
确认五元组是对的。这一步排除了账号问题。

### 第 2 步：确认固件能力

```
AT+GMR
AT+MQTTUSERCFG=?
AT+CIPSTART=?
```

对照第四节选路线。

### 第 3 步：先只连 TCP，不发 MQTT

传输层已经定成 1883 了，`WifiConnect()` 里加一句：

```c
if (ESP8266_TCPConnect(CFG_MQTT_HOST, CFG_MQTT_PORT) == ESP8266_OK)
{
    printf("MQTT TCP connected!\r\n");
}
```

**成功标志**：串口打出 `MQTT TCP connected!`，且 ESP8266 返回里有 `CONNECT`。

**失败排查**：
- 返回 `DNS Fail` → 域名写错，或 ESP8266 没通外网
- 返回 `ERROR` → 端口错，或被路由器/防火墙挡了 1883
- 超时无响应 → 等久一点，`AT+CIPSTART` 可能要十几秒

### 第 4 步：发 CONNECT，看 CONNACK

用第六节算出来的字节流，通过 `ESP8266_TCPSend()` 发出去。

**成功标志**：ESP8266 回显 `+IPD,4:20 02 00 00`
（第 1 字节 `0x20` = CONNACK，第 4 字节 `0x00` = 接受）

**失败排查**：

| CONNACK 返回码 | 含义 |
|---|---|
| `0x01` | 协议版本不对 → 检查协议级别字节是不是 `0x04` |
| `0x02` | Client ID 被拒绝 |
| `0x04` | 用户名或密码错 → **最常见**，检查 Token |
| `0x05` | 未授权 |

**连 CONNACK 都收不到** → 大概率是**剩余长度算错了**，
服务器解析失败直接断开。回去用第六节 6.4 的方法重新算。

### 第 5 步：发 PUBLISH

**成功标志**：OneNET 平台上能看到数据点。

主题（Topic）要按你平台上产品的实际定义填。OneNET 有两套接口，**别抄错**：

```
$sys/{产品ID}/{设备名}/thing/property/post     ← 物模型属性上报（本项目用这个）
$sys/{产品ID}/{设备名}/dp/post/json            ← 数据流接口（另一套，本项目不用）
```

⚠️ **不是 `thing/event/property/post`** —— 那是 OneNET 另一套接口，发过去
平台不认，而且**不报错、直接断掉 TCP**。设备这边看到的 `SEND OK` 是 ESP8266
自己回的（"我塞进 TCP 了"），不是平台回执，于是日志会呈现"第一条上报成功、
后面全失败"，极易误判成模块坏了。本项目踩过（2026-09-20）。

**用 MQTTX 先手动发一条**，确认主题和 JSON 格式平台能接受，
再搬到代码里。这比在代码里试错快得多。

### 第 6 步：最后才加心跳和重连

先把单次上报跑通，再加定时器逻辑。

---

## 九、一句话总结

**先确认固件能力，再走 1883 明文路线把 MQTT 报文跑通，最后才考虑 TLS。**

MQTT 不是"另一种 TCP" —— 它是要自己一个字节一个字节拼的应用层协议。
在 MQTTX 里点几下就有的功能（Keep Alive、自动重连、QoS），
写到 MCU 上都是要自己实现的逻辑。

---

## 参考资料

- [OneNET MQTT 设备连接文档](https://open.iot.10086.cn/doc/iot_platform/book/device-connect&manager/MQTT/mqtt-device-development.html)
- [OneNET MQTT 开发指南](https://open.iot.10086.cn/doc/mqtt/book/device-develop/manual.html)
- [ESP8266 连接 OneNET 报 MQTT_BAD_USERNAME_OR_PASSWORD 讨论](https://bbs.elecfans.com/m/jishu_2500815_1_1.html)
- MQTT 3.1.1 规范：OASIS `mqtt-v3.1.1-os` 第 2.2 节（报文格式）、第 3.1 节（CONNECT）
