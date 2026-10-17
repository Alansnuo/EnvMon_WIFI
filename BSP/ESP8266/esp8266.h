/**
 * esp8266.h
 * ESP8266 WiFi 模块驱动函数声明
 *
 * 基于 USART2 (DMA 环形接收 + 中断发送), 把 WiFi / MQTT 操作翻译成 AT 指令.
 * 顺序阻塞: 每个接口内部"发一条命令 → 等应答 → 再发下一条", 返回前调用者不会
 * 往下走 —— 只适合放在独立任务里, 一次连不上的流程最坏能连续占一分钟.
 *
 * 用法: Init() → ConnectAP() → GetNetTime() → MQTT_SetParam() → MQTT_ConnectBroker() → 循环 Publish()
 *
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"

/* -------------------------------- 硬件接口 -------------------------------- */
extern UART_HandleTypeDef huart2;          /* huart2 在 main.c 里定义*/

#define ESP8266_UART          huart2        /* USART2: PA2=TX, PA3=RX */
#define ESP8266_RST_PORT      GPIOA
#define ESP8266_RST_PIN       GPIO_PIN_6    /* RST: PA6 */

/* -------------------------------- 超时(ms) -------------------------------- */
//根据自己的网络环境调整
#define ESP8266_TIMEOUT       5000          /* 一般 AT 命令等应答 */
#define ESP8266_AP_TIMEOUT    15000         /* 接热点: 要等到 DHCP 拿到 IP */
#define ESP8266_MQTT_TIMEOUT  15000         /* 连 broker: DNS + 握手 + CONNACK */
#define ESP8266_TX_TIMEOUT    100           /* 中断发送兜底 (256 字节 22ms 就发完) */

/* -------------------------------- 缓冲大小 -------------------------------- */
#define ESP8266_AT_CMD_MAX    256           /* 单条指令上限, 模块文档写死的 */
#define ESP8266_CMD_BUF_SIZE  288           /* 命令缓冲: 最长一条拼完约 150 字节, 留够 */
#define ESP8266_RX_BUF_SIZE   512           /* 解析缓冲: 累积整条应答, 供 strstr 匹配 */
#define ESP8266_DMA_BUF_SIZE  256           /* DMA 环形缓冲: 装两次搬运之间的突发 */

/* ---------------------------------- 状态 ---------------------------------- */
typedef enum {
    ESP8266_OK = 0,          /* 成功 */
    ESP8266_ERR_TIMEOUT,     /* 超时 */
    ESP8266_ERR_AT,          /* 模块回了 ERROR/FAIL */
    ESP8266_ERR_PARAM,       /* 参数错 (含"拼出来的命令超过 256 字节") */
    ESP8266_ERR_BUF_FULL     /* 接收缓冲满了, 没匹配到应答 */
} ESP8266_Status;

/* ================================= 初始化 ================================= */

/// @brief  初始化模块: 复位 + 挂上 DMA 接收 + 探活
ESP8266_Status ESP8266_Init(void);

/* ================================== WiFi ================================== */
/// @brief  接热点 (含设成 Station 模式和清掉可能残留的关联)
ESP8266_Status ESP8266_ConnectAP(const char ssid[], const char password[]);

/* ================================== 对时 ================================== */

/// @brief  联网对一次时, 内部定死时区 UTC+8, 直接解析模块回的格式化时间串
ESP8266_Status ESP8266_GetNetTime(uint8_t *hour, uint8_t *min, uint8_t *sec);

/* ================================== MQTT ================================== */

/* 用法: SetParam() → ConnectBroker() → Publish() */

/// @brief  配 MQTT 鉴权 + 连接参数 (底下三条指令: USERCFG / PASSWORD / CONNCFG).
ESP8266_Status ESP8266_MQTT_SetParam(const char clientId[], const char username[],
                                     const char password[], uint16_t keepalive);

/// @brief  发布一条消息 (QoS 0). 载荷里的 " 和 , 要调用方先转义成 \" 和 \,
ESP8266_Status ESP8266_MQTT_ConnectBroker(const char host[], uint16_t port);

/// @brief  连 broker. 必须匹配 "+MQTTCONNECTED"
ESP8266_Status ESP8266_MQTT_Publish(const char topic[], const char data[]);

#endif /* __ESP8266_H */
