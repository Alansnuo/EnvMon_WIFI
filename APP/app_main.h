#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "dht11.h"

/* WiFi 账号密码在 app_config.h (CFG_WIFI_SSID / CFG_WIFI_PASSWORD) */

/* ========================= 任务优先级 =========================
 * configMAX_PRIORITIES = 5, 可用 0~4 (数字越大优先级越高):
 *   4  → 定时器服务任务 (configTIMER_TASK_PRIORITY, FreeRTOS 占用)
 *   3  → WiFi 任务
 *   2  → DHT11 采集任务
 *   0  → 空闲任务 (FreeRTOS 占用)
 *
 * WiFi 排在采集之上: 网络有超时约束, 采集只是周期采样, 晚一点无所谓.
 * WiFi 任务阻塞在 vTaskDelay / 队列上时会自动让出 CPU, 不会把采集任务饿死.
 * 详见《FreeRTOS驱动适配笔记》3.7 */
#define APP_PRIO_WIFI           3
#define APP_PRIO_DHT11          2

/* ========================= 任务栈 (单位: 字, 1 字 = 4 字节) =========================
 * 实测余量看 uxTaskGetStackHighWaterMark() 的打印, 数值是"还剩多少字".
 *
 * WiFi 栈比采集大一倍: 联网阶段是全场最吃栈的 (ESP8266 驱动里有
 * char cmd[128], 加上 printf 自己的缓冲). 那个阶段占的栈比上报循环多得多,
 * 所以按联网的用量来定. 接了 MQTT 报文拼装之后还要再看一次余量. */
#define APP_STACK_WIFI          512
#define APP_STACK_DHT11         256

/* ========================= 任务周期 ========================= */
#define APP_DHT11_PERIOD_MS     2000    /* DHT11 两次读取间隔须 >= 1s */

/* ========================= 任务间数据通道 =========================
 * DHT11 任务生产, 通信任务消费. 队列长度 4 意味着最多囤 4 次采样,
 * 通信任务跟不上的时候采集任务不会被拖住 (满了就丢).
 *
 * 以后加光照: 是再开一条 g_xLightQueue, 还是所有传感器共用一条带来源
 * 标记的队列, 等真要写的时候再定 —— 见 task_light.c 里的说明. */
#define APP_DHT11_QUEUE_LEN     4

extern QueueHandle_t g_xDht11Queue;

/* 应用层总入口. 在 MX_xxx_Init() 之后、vTaskStartScheduler() 之前调用. */
void App_Init(void);

#endif /* __APP_MAIN_H */
