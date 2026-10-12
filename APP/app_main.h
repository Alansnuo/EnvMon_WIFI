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
 *   1  → 光照采集任务 / 语音命令任务 / OLED 显示任务
 *   0  → 空闲任务 (FreeRTOS 占用)
 *
 * WiFi 排在采集之上: 网络有超时约束, 采集只是周期采样, 晚一点无所谓.
 * WiFi 任务阻塞在 vTaskDelay / 队列上时会自动让出 CPU, 不会把采集任务饿死.
 * 详见《FreeRTOS驱动适配笔记》3.7 */
#define APP_PRIO_WIFI           3
#define APP_PRIO_DHT11          2
#define APP_PRIO_LIGHT          1       /* 比 DHT11 低: 光照刷不刷得及时无所谓 */
#define APP_PRIO_ASR            1       /* 和光照同档: 50ms 响应人耳感觉不到 */
#define APP_PRIO_OLED           1       /* 和采集同档: 1 秒刷一帧, 晚一拍无所谓 */

/* ========================= 任务栈 (单位: 字, 1 字 = 4 字节) =========================
 * 实测余量看 uxTaskGetStackHighWaterMark() 的打印, 数值是"还剩多少字".
 *
 * WiFi 栈比采集大一倍: 联网阶段是全场最吃栈的 (ESP8266 驱动里有
 * char cmd[128], 加上 printf 自己的缓冲). 那个阶段占的栈比上报循环多得多,
 * 所以按联网的用量来定. 接了 MQTT 报文拼装之后还要再看一次余量. */
#define APP_STACK_WIFI          512
#define APP_STACK_DHT11         256
#define APP_STACK_LIGHT         256
#define APP_STACK_ASR           256
#define APP_STACK_OLED          256     /* 和采集任务同档: 不接 printf 之外的大缓冲 */

/* ========================= 任务周期 ========================= */
#define APP_DHT11_PERIOD_MS     2000    /* DHT11 两次读取间隔须 >= 1s */
#define APP_LIGHT_PERIOD_MS     2000    /* BH1750 一次转换最长 180ms, 2s 绰绰有余 */
#define APP_ASR_PERIOD_MS       50      /* 语音响应延迟上限, 人耳感知不到
                                         * ⚠️ 绝不能设成 0: vTaskDelay(0) 不延时,
                                         *    任务会变忙等把 CPU 吃光 */
#define APP_OLED_PERIOD_MS      1000    /* 1 秒刷一帧, 既不闪烁也不拖累采集 */

/* ========================= 任务间数据通道 =========================
 * DHT11 任务生产, 通信任务消费. 队列长度 4 意味着最多囤 4 次采样,
 * 通信任务跟不上的时候采集任务不会被拖住 (满了就丢).
 *
 * 光照目前只串口打印, 没有走队列: "再开一条 g_xLightQueue, 还是所有传感器共用
 * 一条带来源标记的队列"这个设计还没定, 等做上报那一步再说 (见 task_light.c). */
#define APP_DHT11_QUEUE_LEN     4

extern QueueHandle_t g_xDht11Queue;

/* ========================= 最新值共享 (给 OLED 显示任务用) =========================
 * OLED 任务不直接读传感器 (会和 DHT11 的单总线 / BH1750 的转换时序冲突),
 * 改成读这两个全局: 采集任务每完成一次就更新一次, 显示任务周期性拷一份.
 *
 * volatile 防止编译器把循环里的读取合并成一次;
 * 结构体拷贝和 float 读取各非原子, 取数方用 taskENTER_CRITICAL 保证一致性.
 * 写方在采集任务的循环里, 周期 2s, 不会和显示任务的 1s 刷新撞出问题. */
extern volatile DHT11_Data g_xLatestDht11;
extern volatile float      g_xLatestLux;

/* 应用层总入口. 在 MX_xxx_Init() 之后、vTaskStartScheduler() 之前调用. */
void App_Init(void);

#endif /* __APP_MAIN_H */
