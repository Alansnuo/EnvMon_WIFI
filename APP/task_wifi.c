/* ============================================================================
 * WiFi 通信任务: 联网 + MQTT 上报, 所有碰 ESP8266 的事都在这里
 * 业务策略在本文件, AT 指令与网络细节在 BSP/ESP8266。
 * 驱动顺序阻塞 (一次连接最坏十几秒), 故独立任务跑。
 * 上报: 值不变不发, 变了立刻发。
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

#define WIFI_RETRY_DELAY_MS     5000    /* 重连前歇一下, 等模块复位后稳定 */

/* 连续失败这么多次才判定链路死了. 单次失败可能只是网络抖一下, 而重连要十几秒 */
#define WIFI_MAX_CONSEC_FAIL    3

static void TaskWifi(void *argument);

/* ============================================================================
 * 上报报文 JSON
 * 报文 = {"id","version","params"} + 属性段; 载荷里的 " 和 , 多写一层反斜杠
 * (AT 指令按逗号切参数, 不转义会被切碎)。
 * 用法: 填值 → Report() 拼报文并判变化 → 发成功再 Commit()。
 * 未 Commit 时下次 Report() 仍报"变了", 发送失败天然带重试。
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

    /* 属性名必须与 OneNET 功能定义一致 (大小写敏感).
     * lux 放大 10 倍取整, 拆一位小数拼接, 避免链接 float 版 printf */
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

/* 发送成功后调用, 把当前值认作"已上报". 失败就别调 */
static void Json_Commit(void)
{
    (void)strcpy(s_Sent, s_Params);
}

/* ================================== 联网 ================================== */

static uint8_t WifiConnect(void);

/* 复位模块 → 接热点 → 对时 → 配 MQTT → 连 Broker. 失败返回 0 由外层重试;
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
    g_xWifiState = WIFI_STATE_AP_OK;        /* 热点通了, 接着搞 MQTT */

    /* 联网成功顺手对时 (软件时钟无掉电记忆); 失败不拦流程 */
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
        /* 连不上就重试; WifiConnect() 整体复位模块, 各种异常都能回到干净状态 */
        while (WifiConnect() == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
        }

        /* 采集任务每 2 秒往队列里放一条. 连续失败到阈值说明链路死了, 回去重连 */
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
                Json_Commit();          /* 发出去了才认它作当前值 */
                failStreak = 0;
            }
            else
            {
                failStreak++;
            }
        }
        /* 连续失败到阈值: 回到 for(;;) 顶部重连 */
    }
}
