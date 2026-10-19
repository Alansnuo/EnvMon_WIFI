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
 *   1  → 光照采集任务 / 语音命令任务 / OLED 显示任务 / 按键任务
 *   0  → 空闲任务 (FreeRTOS 占用)
 *
 * WiFi 高于采集任务: 网络操作有超时约束, 采集为周期采样, 延后无影响. */
#define APP_PRIO_WIFI           3
#define APP_PRIO_DHT11          2
#define APP_PRIO_LIGHT          1       /* 低于 DHT11: 光照刷新延迟无影响 */
#define APP_PRIO_ASR            1       /* 与光照同级: 50ms 响应延迟人耳无感 */
#define APP_PRIO_OLED           1       /* 与采集同级: 1 秒刷一帧, 延迟一帧无影响 */
#define APP_PRIO_KEY            1       /* 与采集同级: 20ms 扫描一次, 开销极小 */

/* ========================= 任务栈 (单位: 字, 1 字 = 4 字节) ========================= */
#define APP_STACK_WIFI          512
#define APP_STACK_DHT11         256
#define APP_STACK_LIGHT         256
#define APP_STACK_ASR           256
#define APP_STACK_OLED          384     
#define APP_STACK_KEY           128     

/* ========================= 任务周期 ========================= */
#define APP_DHT11_PERIOD_MS     2000    /* DHT11 两次读取间隔须 >= 1s */
#define APP_LIGHT_PERIOD_MS     2000    /* BH1750 单次转换最长 180ms, 2s 足够 */
#define APP_ASR_PERIOD_MS       50      /* 语音响应延迟上限;*/
#define APP_OLED_PERIOD_MS      500     /* 0.5秒刷一帧; 同时作为软件时钟的秒脉冲 */

/* 语音定时播报周期
 * 120 秒一次; 与 APP_ASR_PERIOD_MS 为整除关系 (120000/50 = 2400 次轮询). */
#define APP_ASR_REPORT_PERIOD_MS 120000

/* ========================= 任务间数据通道 =========================
 * DHT11 任务生产, WiFi 任务消费: 温湿度 + 投递时刻打包的最新光照.
 * 队列长度 4 = 最多缓存 4 次采样 */
typedef struct {
    DHT11_Data dht;             /* 温湿度 (本次采样) */
    float      lux;             /* 光照 (投递时从 g_xLatestLux 打包) */
} App_EnvMsg;

#define APP_ENV_QUEUE_LEN       4

extern QueueHandle_t g_xEnvQueue;

/* ========================= 最新值共享 (给 OLED 显示任务用) =========================
 * OLED 任务不直接读传感器 (会与 DHT11 单总线 / BH1750 转换时序冲突),
 * 改读全局变量: 采集任务每次完成后更新, 显示任务周期性拷贝. */
extern volatile DHT11_Data g_xLatestDht11;
extern volatile float      g_xLatestLux;

/* ========================= WiFi 连接状态 (给 OLED 显示用) =========================
 * task_wifi.c 在联网过程中逐步更新, OLED 任务只读.
 * 细分状态用于区分两种失败: 热点未连上 / 热点已通但 MQTT 未通,
 * 由 OLED 显示词定位故障段. */
#define WIFI_STATE_CONNECTING   0U      /* 正在连 / 正在重连 */
#define WIFI_STATE_AP_OK        1U      /* 热点连上了, MQTT 还没通 */
#define WIFI_STATE_MQTT_OK      2U      /* 热点 + MQTT 都通了 = 数据在上传 */
#define WIFI_STATE_FAIL         3U      /* 这一步失败, 5 秒后重试 */

extern volatile uint8_t g_xWifiState;

/* 应用层总入口. 在 MX_xxx_Init() 之后、vTaskStartScheduler() 之前调用. */
void App_Init(void);

#endif /* __APP_MAIN_H */
