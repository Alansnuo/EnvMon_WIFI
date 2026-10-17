/* ============================================================================
 * WiFi 通信任务: 联网 + MQTT 上报; 所有 ESP8266 操作集中于此
 * 业务策略在本文件, AT 指令与网络细节在 BSP/ESP8266.
 * 驱动为顺序阻塞式 (一次连接最坏十几秒), 故单独任务运行.
 * 上报策略: 值不变不发, 变化立即发.
 * ========================================================================== */

#include "task_wifi.h"
#include "app_config.h"
#include "app_main.h"
#include "app_ui.h"

#include "dht11.h"
#include "esp8266.h"
#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#define WIFI_RETRY_DELAY_MS     5000    /* 重连前等待, 等模块复位稳定 */

/* 连续失败达此次数判定链路断开: 单次失败可能只是网络抖动, 而重连需十几秒 */
#define WIFI_MAX_CONSEC_FAIL    3

static void TaskWifi(void *argument);

/* ============================================================================
 * 上报报文 JSON
 * 报文 = {"id","version","params"} + 属性段; 载荷里的 " 和 , 多加一层反斜杠
 * (AT 指令按逗号切参数, 不转义会被切开).
 * 用法: 填值 → Report() 拼报文并判变化 → 发送成功后再 Commit().
 * 未 Commit 时下次 Report() 仍判定为变化, 发送失败即自带重试.
 * ========================================================================== */

#define JSON_PARAMS_SIZE    96      /* 三段属性拼完最长约 78 字节 */
#define JSON_TEXT_SIZE      192     /* 报文 = 帧头 + 属性段, 最长约 131 字节 */

static char     s_Buf[JSON_TEXT_SIZE];          /* 拼好的报文 */
static char     s_Params[JSON_PARAMS_SIZE];     /* 当前值的属性段 */
static char     s_Sent[JSON_PARAMS_SIZE];       /* 上次发成功的属性段 */
static uint32_t s_MsgId;                        /* 报文序号 */
static int      s_Temp;
static int      s_Humi;
static float    s_Lux;

/* 填入要上报的值 */
static void Json_SetTemp(int temp) { s_Temp = temp; }
static void Json_SetHumi(int humi) { s_Humi = humi; }
static void Json_SetLux(float lux) { s_Lux = lux; }

/* 与上次已发值比较并拼报文进 s_Buf. 返回 1=变了可发, 0=没变 */
static uint8_t Json_Report(void)
{
    unsigned long lux10;

    /* 属性名须与 OneNET 功能定义一致 (大小写敏感).
     * lux 放大 10 倍取整后拆位拼接, 避免链接 float 版 printf */
    lux10 = (unsigned long)(s_Lux * 10.0f + 0.5f);      /* 四舍五入到 0.1 lux */

    (void)snprintf(s_Params, sizeof(s_Params),
        "\\\"Temp\\\":{\\\"value\\\":%d.0}\\,"
        "\\\"Hum\\\":{\\\"value\\\":%d.0}\\,"
        "\\\"lux\\\":{\\\"value\\\":%lu.%lu}",
        s_Temp, s_Humi, lux10 / 10U, lux10 % 10U);

    if (strcmp(s_Params, s_Sent) == 0)
    {
        return 0;                               /* 值没变, 不用发 */
    }

    (void)snprintf(s_Buf, sizeof(s_Buf),
        "{\\\"id\\\":\\\"%lu\\\"\\,\\\"version\\\":\\\"1.0\\\"\\,\\\"params\\\":{%s}}",
        (unsigned long)(++s_MsgId), s_Params);

    return 1;
}

/* 发送成功后调用, 将当前值记为已上报; 失败不调用 */
static void Json_Commit(void)
{
    (void)strcpy(s_Sent, s_Params);
}

/* ================================== 联网 ================================== */

static uint8_t WifiConnect(void);

/* 复位模块 → 接热点 → 对时 → 配 MQTT → 连 Broker. 失败返回 0, 由外层重试;
 * 过程中更新 g_xWifiState 供 OLED 显示进度. */
static uint8_t WifiConnect(void)
{
    g_xWifiState = WIFI_STATE_CONNECTING;

    if (ESP8266_Init() != ESP8266_OK)
    {
        g_xWifiState = WIFI_STATE_FAIL;
        return 0;
    }

    if (ESP8266_ConnectAP(CFG_WIFI_SSID, CFG_WIFI_PASSWORD) != ESP8266_OK)
    {
        g_xWifiState = WIFI_STATE_FAIL;
        return 0;
    }
    g_xWifiState = WIFI_STATE_AP_OK;        /* 热点已连, 继续 MQTT */

    /* 联网成功后对时 (软件时钟无掉电保持); 失败不影响后续流程 */
    {
        uint8_t hour;
        uint8_t min;
        uint8_t sec;

        if (ESP8266_GetNetTime(&hour, &min, &sec) == ESP8266_OK)
        {
            AppClock_SetTime(hour, min, sec);
        }
    }

    if (ESP8266_MQTT_SetParam(CFG_MQTT_CLIENT_ID, CFG_MQTT_USERNAME,
                              CFG_MQTT_PASSWORD, CFG_MQTT_KEEPALIVE_S) != ESP8266_OK)
    {
        g_xWifiState = WIFI_STATE_FAIL;
        return 0;
    }

    if (ESP8266_MQTT_ConnectBroker(CFG_MQTT_HOST, CFG_MQTT_PORT) != ESP8266_OK)
    {
        g_xWifiState = WIFI_STATE_FAIL;
        return 0;
    }

    g_xWifiState = WIFI_STATE_MQTT_OK;
    return 1;
}

/* ================================= 建任务 ================================= */
void TaskWifiCreate(void)
{
    if (xTaskCreate(TaskWifi, "WiFi", APP_STACK_WIFI, NULL,
                    APP_PRIO_WIFI, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ======================== 任务本体: 先联网, 再上报 ======================== */
static void TaskWifi(void *argument)
{
    DHT11_Data   dht;
    float        lux;
    uint8_t      failStreak;            /* 连续失败计数, 成功一次就归零 */

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        /* 连不上则重试; WifiConnect() 每次整体复位模块, 各异常状态均可恢复 */
        while (WifiConnect() == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
        }

        /* 采集任务每 2 秒入队一条; 连续失败达阈值即判定链路断开, 返回重连 */
        failStreak = 0;
        while (failStreak < WIFI_MAX_CONSEC_FAIL)
        {
            if (xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY) != pdPASS)
            {
                break;                  /* 队列异常, 退出去重连 */
            }

            /* 光照不走队列, 直接拷共享最新值 */
            taskENTER_CRITICAL();
            lux = g_xLatestLux;
            taskEXIT_CRITICAL();

            Json_SetTemp(dht.temperature);
            Json_SetHumi(dht.humidity);
            Json_SetLux(lux);

            if (Json_Report() == 0)
            {
                continue;               /* 值没变, 不用发 */
            }

            if (ESP8266_MQTT_Publish(CFG_MQTT_PUB_TOPIC, s_Buf) == ESP8266_OK)
            {
                Json_Commit();          /* 发送成功才记为当前值 */
                failStreak = 0;
            }
            else
            {
                failStreak++;
            }
        }
        /* 连续失败达阈值: 回到循环顶部重连 */
    }
}
