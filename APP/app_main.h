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
 * WiFi 高于采集任务: 网络操作有超时约束, 采集为周期采样, 延后无影响.
 * WiFi 任务阻塞在 vTaskDelay / 队列上时会自动让出 CPU, 不会饿死采集任务. */
#define APP_PRIO_WIFI           3
#define APP_PRIO_DHT11          2
#define APP_PRIO_LIGHT          1       /* 低于 DHT11: 光照刷新延迟无影响 */
#define APP_PRIO_ASR            1       /* 与光照同级: 50ms 响应延迟人耳无感 */
#define APP_PRIO_OLED           1       /* 与采集同级: 1 秒刷一帧, 延迟一帧无影响 */
#define APP_PRIO_KEY            1       /* 与采集同级: 20ms 扫描一次, 开销极小 */

/* ========================= 任务栈 (单位: 字, 1 字 = 4 字节) =========================
 * 实测余量看 uxTaskGetStackHighWaterMark(), 数值为剩余字数.
 *
 * WiFi 栈为采集任务的两倍: 联网阶段栈占用最大 (ESP8266 驱动中
 * s_Cmd[ESP8266_CMD_BUF_SIZE] 拼 AT 指令), 按该阶段用量确定.
 * 接入 MQTT 报文拼装后需再核对余量. */
#define APP_STACK_WIFI          512
#define APP_STACK_DHT11         256
#define APP_STACK_LIGHT         256
#define APP_STACK_ASR           256
#define APP_STACK_OLED          384     /* newlib snprintf 开销大, 留足余量 */
#define APP_STACK_KEY           128     /* 仅 GPIO 读 + 若干赋值, 128 字足够 */

/* ========================= 任务周期 ========================= */
#define APP_DHT11_PERIOD_MS     2000    /* DHT11 两次读取间隔须 >= 1s */
#define APP_LIGHT_PERIOD_MS     2000    /* BH1750 单次转换最长 180ms, 2s 足够 */
#define APP_ASR_PERIOD_MS       50      /* 语音响应延迟上限, 人耳无感;
                                         * 不可设为 0: vTaskDelay(0) 不延时, 任务变忙等占满 CPU */
#define APP_OLED_PERIOD_MS      1000    /* 1 秒刷一帧; 同时作为软件时钟的秒脉冲 (见 app_ui.h) */

/* 语音定时播报周期 (必做3: 天问语音模块定时触发).
 * 120 秒一次; 与 APP_ASR_PERIOD_MS 为整除关系 (120000/50 = 2400 次轮询). */
#define APP_ASR_REPORT_PERIOD_MS 120000

/* ========================= 任务间数据通道 =========================
 * DHT11 任务生产, 通信任务消费. 队列长度 4 = 最多缓存 4 次采样,
 * 通信任务跟不上时采集任务不被阻塞 (队列满则丢弃). */
#define APP_DHT11_QUEUE_LEN     4

extern QueueHandle_t g_xDht11Queue;

/* ========================= 最新值共享 (给 OLED 显示任务用) =========================
 * OLED 任务不直接读传感器 (会与 DHT11 单总线 / BH1750 转换时序冲突),
 * 改读全局变量: 采集任务每次完成后更新, 显示任务周期性拷贝.
 *
 * volatile 防止编译器合并循环内的读取; 结构体拷贝与 float 读取均非原子,
 * 读取方用 taskENTER_CRITICAL 保证一致性. */
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
