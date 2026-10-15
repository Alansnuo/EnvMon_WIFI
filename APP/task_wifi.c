/* ============================================================================
 * WiFi 通信任务: 联网 + MQTT 上报, 所有碰 ESP8266 的事都在这里
 *
 * 业务策略在本文件, AT 指令与网络细节在 BSP/ESP8266。
 * 驱动是顺序阻塞的(发一条等一条), 一次连接最坏占十几秒, 所以要独立任务跑。
 * 上报: 值不变就不发, 变了下一条立刻发。
 *
 * 上报报文(JSON)原来单独放 APP/app_json.c, 现在折进本文件 ——
 * 只有这一个任务用它, 拆成两个文件只是多一层 include。
 * ========================================================================== */

#include "task_wifi.h"
#include "app_config.h"
#include "app_main.h"
#include "app_ui.h"

#include "dht11.h"
#include "esp8266.h"
#include "app_ui.h"
#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#define WIFI_RETRY_DELAY_MS     5000    /* 重连前歇一下, 等模块复位后稳定 */

/* 连续失败这么多次才判定链路死了. 单次失败可能只是网络抖一下, 而重连要十几秒 */
#define WIFI_MAX_CONSEC_FAIL    3

static void TaskWifi(void *argument);

/* ============================================================================
 * 上报报文 (原 app_json.c)
 *
 * 属性段是 \"Temp\":{\"value\":25.0}\,\"Hum\":{\"value\":60.0}\,
 *          \"lux\":{\"value\":256.8} 这一段,
 * 报文 = {"id","version","params"} + 属性段。
 *
 * 里面的 " 和 , 都多写了一层反斜杠: ESP8266 的 AT 指令按逗号切参数,
 * 载荷不转义会被当成分隔符切碎 —— 参考工程也是把转义符直接写在字面量里的。
 *
 * 用法: 填值 → Report() 看值变没变 → 变了的报文就在 s_Buf 里 → 发成功再 Commit()。
 * 这样"发送失败"不用另写重试: 没 Commit, 下次 Report() 还会说"变了"。
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

/* 比较当前值和上次发成功的那份, 顺便把报文拼进 s_Buf.
 * 返回 1 = 值变了, s_Buf 里是能发出去的报文; 0 = 没变, 不用发 */
static uint8_t Json_Report(void)
{
    unsigned long lux10;

    /* 属性名必须和 OneNET 控制台"功能定义"里完全一致, 大小写敏感.
     * Temp/Hum 补 .0 冒充浮点; lux 是真浮点, 先放大 10 倍取整再拆成
     * 整数位/小数位拼一位小数 —— 都不用链进 float 版 printf */
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

/* 复位模块 → 接热点 → 对时 → 配 MQTT 参数 → 连 Broker. 失败返回 0, 由外层决定重试.
 * 过程中同步更新 g_xWifiState, 让 OLED 能看出卡在哪一步. */
static uint8_t WifiConnect(void)
{
    uint8_t hour;
    uint8_t min;
    uint8_t sec;

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

    /* 顺手对一次时: 软件时钟没有掉电记忆, 每次联网成功都校准一遍.
     * 失败不拦流程 —— 时钟顶多先显示 00:00, MQTT 该连还连 */
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
        /* 连不上就在这儿重试. 走一遍 WifiConnect() 等于把模块整个复位, 所以
         * 卡死 / 半连接 / 只是 WiFi 掉了, 都能回到干净状态 */
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

            /* 光照不走队列, 直接拷共享最新值 (和 OLED/语音任务同款读法) */
            taskENTER_CRITICAL();
            lux = g_xLatestLux;
            taskEXIT_CRITICAL();

            Json_SetTemp(dht.temperature);
            Json_SetHumi(dht.humidity);
            Json_SetLux(lux);

            /* 值没变就没必要发, 报文也不用拼 */
            if (Json_Report() == 0)
            {
                continue;
            }

            /* 载荷里的 " 和 , 由驱动的转义逻辑配合(见上面 Json_Report 的说明) */
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
