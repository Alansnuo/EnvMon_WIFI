/**
 * esp8266.h
 * ESP8266 WiFi 模块驱动函数声明
 * @date 2026.09.23
 *
 * 基于 USART2 (DMA 环形接收 + 中断发送), 把 WiFi / MQTT 操作翻译成 AT 指令.
 * 顺序阻塞: 每个接口内部"发一条命令 → 等应答 → 再发下一条", 返回前调用者不会
 * 往下走 —— 只适合放在独立任务里, 一次连不上的流程最坏能连续占一分钟.
 *
 * 用法: Init() → ConnectAP() → MQTT_SetParam() → MQTT_ConnectBroker() → 循环 Publish()
 *
 * 指令名以本固件实际支持的为准: 老资料里的 AT+MQTTCFG 在这儿叫
 * AT+MQTTUSERCFG + AT+MQTTCONNCFG; 心跳由模块自己按 keepalive 发
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"

/* -------------------------------- 硬件接口 -------------------------------- */

extern UART_HandleTypeDef huart2;           /* USART2, 定义在 usart.c */

#define ESP8266_UART          huart2        /* USART2: PA2=TX, PA3=RX */
#define ESP8266_RST_PIN       GPIO_PIN_1    /* RST: PB1 */
#define ESP8266_RST_PORT      GPIOB

/* -------------------------------- 超时(ms) -------------------------------- */
#define ESP8266_TIMEOUT       5000          /* 一般 AT 命令等应答 */
#define ESP8266_AP_TIMEOUT    15000         /* 接热点: 要等到 DHCP 拿到 IP */
#define ESP8266_MQTT_TIMEOUT  15000         /* 连 broker: DNS + 握手 + CONNACK */
#define ESP8266_TX_TIMEOUT    100           /* 中断发送兜底 (256 字节 22ms 就发完) */

/* -------------------------------- 缓冲大小 -------------------------------- */
#define ESP8266_AT_CMD_MAX    256           /* 单条指令上限, 模块文档写死的 */
#define ESP8266_CMD_BUF_SIZE  288           /* 命令区: 比 256 多留 32, 给拼完字符串后接的数字尾巴 */
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

/**
 * @brief  初始化模块: 复位 + 挂上 DMA 接收 + 探活
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_Init(void);

/* ================================== WiFi ================================== */

/**
 * @brief  接热点 (含设成 Station 模式和清掉可能残留的关联)
 * @param  ssid      热点名
 * @param  password  密码
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_ConnectAP(const char *ssid, const char *password);

/* ================================== MQTT ================================== */

/* 模块自己讲 MQTT 协议(报文在它内部拼), 我们只管发 AT 指令.
 * 用法: SetParam() → ConnectBroker() → Publish() */

/**
 * @brief  配 MQTT 鉴权 + 连接参数 (底下三条指令: USERCFG / PASSWORD / CONNCFG).
 *         Token 124 字节超过 USERCFG 的 64 字节上限, 所以单独走 AT+MQTTPASSWORD;
 *         keepalive 必须显式给, 模块会把 0 强制改成 120
 * @param  clientId   设备名
 * @param  username   产品 ID
 * @param  password   鉴权 Token
 * @param  keepalive  心跳周期(s)
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_SetParam(const char *clientId, const char *username,
                                     const char *password, uint16_t keepalive);

/**
 * @brief  连 broker. 必须匹配 "+MQTTCONNECTED", 不能只写 "CONNECTED"
 *         ("+MQTTDISCONNECTED" 里含着它)
 * @param  host  域名或 IP (传域名的话模块自己做 DNS)
 * @param  port  端口
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_ConnectBroker(const char *host, uint16_t port);

/**
 * @brief  发布一条消息 (QoS 0). 主题和载荷里的 " \ , 由本函数转义;
 *         返回 OK 只代表模块收下了指令
 * @param  topic  主题
 * @param  data   载荷
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_Publish(const char *topic, const char *data);

#endif /* __ESP8266_H */
