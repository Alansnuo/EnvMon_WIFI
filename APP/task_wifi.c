/* ============================================================================
 * WiFi 通信任务: 联网 + MQTT 上报, 所有碰 ESP8266 的事都在这里
 *
 * 业务策略在本文件, AT 指令与网络细节在 BSP/ESP8266.
 * 驱动是顺序阻塞的(发一条等一条), 一次连接最坏占十几秒, 所以要独立任务跑.
 * 上报: 值不变就不发, 变了下一条立刻发.
 * ========================================================================== */

#include "task_wifi.h"
#include "app_config.h"
#include "app_main.h"
#include <stdio.h>
#include <string.h>

#include "dht11.h"
#include "esp8266.h"

#define WIFI_RETRY_DELAY_MS     5000    /* 重连前歇一下, 等模块复位后稳定 */

/* 连续失败这么多次才判定链路死了. 单次失败可能只是网络抖一下, 而重连要十几秒 */
#define WIFI_MAX_CONSEC_FAIL    3

static void    TaskWifi(void *argument);
static uint8_t WifiConnect(void);

/* ================================== 联网 ================================== */

/* 复位模块 → 接热点 → 配 MQTT 参数 → 连 Broker. 失败返回 0, 由外层决定重试 */
static uint8_t WifiConnect(void)
{
    if (ESP8266_Init() != ESP8266_OK)
    {
        printf("[WiFi] init failed\r\n");
        return 0;
    }

    if (ESP8266_ConnectAP(CFG_WIFI_SSID, CFG_WIFI_PASSWORD) != ESP8266_OK)
    {
        printf("[WiFi] connect failed\r\n");
        return 0;
    }
    printf("[WiFi] connected\r\n");

    if (ESP8266_MQTT_SetParam(CFG_MQTT_CLIENT_ID, CFG_MQTT_USERNAME,
                              CFG_MQTT_PASSWORD, CFG_MQTT_KEEPALIVE_S) != ESP8266_OK)
    {
        printf("[WiFi] MQTT set param failed\r\n");
        return 0;
    }

    if (ESP8266_MQTT_ConnectBroker(CFG_MQTT_HOST, CFG_MQTT_PORT) != ESP8266_OK)
    {
        printf("[WiFi] MQTT connect failed\r\n");
        return 0;
    }
    printf("[WiFi] MQTT connected\r\n");

    return 1;
}

/* ================================= 建任务 ================================= */
void TaskWifiCreate(void)
{
    if (xTaskCreate(TaskWifi, "WiFi", APP_STACK_WIFI, NULL,
                    APP_PRIO_WIFI, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(WiFi) failed (heap too small?)\r\n");
        Error_Handler();
    }
}

/* ======================== 任务本体: 先联网, 再上报 ======================== */
static void TaskWifi(void *argument)
{
    DHT11_Data dht;
    char       params[64];              /* 本次采样的 JSON 属性段 */
    char       payload[128];            /* 完整报文 */
    static char     reported[64];       /* 当前值: 上次发成功的那份 params */
    static uint32_t msgId = 0;          /* 报文序号, 每次上报 +1 填进 "id" */
    uint8_t    failStreak;              /* 连续失败计数, 成功一次就归零 */

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        /* 连不上就在这儿重试. 走一遍 WifiConnect() 等于把模块整个复位, 所以
         * 卡死 / 半连接 / 只是 WiFi 掉了, 都能回到干净状态 */
        while (WifiConnect() == 0)
        {
            printf("[WiFi] retry in %d s\r\n", WIFI_RETRY_DELAY_MS / 1000);
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

            /* 属性名必须和控制台"功能定义"里完全一致, 大小写敏感.
             * 值写 "%d.0": 属性是浮点型, 补 .0 又不用链进 float 版 printf */
            (void)snprintf(params, sizeof(params),
                "{\"Temp\":{\"value\":%d.0},\"Hum\":{\"value\":%d.0}}",
                dht.temperature, dht.humidity);

            /* 值没变就不发; 比的是整段 JSON, 以后加属性不用动这里 */
            if (strcmp(params, reported) == 0)
            {
                continue;
            }

            /* 补报文头, 凑成物模型要的 {"id","version","params"} 三件套.
             * id 只在这时候 +1 */
            (void)snprintf(payload, sizeof(payload),
                "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":%s}",
                (unsigned long)(++msgId), params);

            /* 载荷里的 " 和 , 由驱动负责转义, 这里照常写合法 JSON */
            if (ESP8266_MQTT_Publish(CFG_MQTT_PUB_TOPIC, payload) == ESP8266_OK)
            {
                (void)strcpy(reported, params);
                failStreak = 0;
            }
            else
            {
                failStreak++;
            }
        }

        printf("[WiFi] publish failed %u times in a row, reconnecting\r\n",
               (unsigned)failStreak);
    }
}
