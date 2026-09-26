/* ============================================================================
 * WiFi 通信任务: 联网 + MQTT 上报, 所有碰 ESP8266 的事都在这里
 *
 * 分层: 业务策略在本文件, AT 指令与网络细节在 BSP/ESP8266, 两层各管各的.
 * 等待: 驱动是顺序阻塞的(发一条等一条), 一次连接最坏占十几秒 —— 占的是本任务
 *       自己, 采集/灯这些任务照跑, 所以这个任务的存在就是让它慢慢等.
 * 恢复: 开机连不上 → 外层循环重试; 运行中连续发不出去 → 退回去重连.
 * ========================================================================== */

#include "task_wifi.h"
#include "app_config.h"
#include "app_main.h"
#include <stdio.h>

#include "dht11.h"
#include "esp8266.h"

/* 重连前先歇 5 秒: ESP8266 刚复位完要时间稳定, 连着猛试没有意义;
 * 但太长又会让路由器重启后的恢复变得很慢. 5 秒是个折中. */
#define WIFI_RETRY_DELAY_MS     5000

/* 连续失败达到这个数才认定链路死了, 退回去重连.
 *
 * 不用"失败一次就重连": 单次失败可能只是网络抖一下, 而重连要复位模块 +
 * 重新关联 + 重连 Broker, 前后十几秒起步. 上报路径 2 秒一条, 3 次 = 6 秒
 * 就反应过来, 够快. */
#define WIFI_MAX_CONSEC_FAIL    3

static void    TaskWifi(void *argument);
static uint8_t WifiConnect(void);

/* ================================== 联网 ================================== */

/* 复位模块 → 接热点 → 配 MQTT 参数 → 连 Broker.
 * 每步只报成功/失败, 失败就返回 0, 由外层决定什么时候再试. */
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
    char       payload[192];            /* OneNET 物模型的 JSON 报文 */
    static uint32_t msgId = 0;          /* 报文序号, 每次上报 +1 填进 "id" */
    uint8_t    failStreak;              /* 连续失败计数, 成功一次就归零 */

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        /* --- 阶段 1: 联网 ---
         * 连不上就在这里重试, 不往下走: 没连上的话, 下面每次发送都会失败, 只是
         * 把错误刷屏, 不如老实重连.
         *
         * 这一段顺带就是"把模块整个复位": WifiConnect() 第一步是 ESP8266_Init(),
         * 而 Init 里拉了 RST 引脚 (见 esp8266.c). 所以不管模块是卡死了、socket
         * 半开着、还是只是 WiFi 掉了, 走一遍这里都能回到干净状态 —— 不用再写
         * 单独的清理代码. */
        while (WifiConnect() == 0)
        {
            printf("[WiFi] retry in %d s\r\n", WIFI_RETRY_DELAY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
        }

        /* --- 阶段 2: 上报 ---
         * 采集任务每 2 秒往队列里放一条, 来一条发一条. 连续失败到阈值说明链路
         * 真的死了 (热点关了 / broker 把我们踢了), 退出循环回外层重连. */
        failStreak = 0;
        while (failStreak < WIFI_MAX_CONSEC_FAIL)
        {
            if (xQueueReceive(g_xDht11Queue, &dht, portMAX_DELAY) != pdPASS)
            {
                break;                  /* 队列异常, 退出去重连 */
            }

            /* 拼 OneNET 物模型的 JSON: {"id","version","params"} 三件套, params
             * 里每个属性是 {"value": x}. 属性名 **Temp / Hum 必须和控制台
             * "功能定义"里的写法完全一致, 大小写敏感** —— 写成 temp/humi 平台
             * 不收 (而且不一定报错).
             *
             * 值写成 "%d.0" 而不是用 %f: DHT11 本来只给整数, 但属性在平台上
             * 定义的是浮点类型, 整数可能被判类型不符; 补个 .0 两边都满足, 又
             * 不用把 float 版 printf 链进来 (省 flash). */
            (void)snprintf(payload, sizeof(payload),
                "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
                    "\"Temp\":{\"value\":%d.0},"
                    "\"Hum\":{\"value\":%d.0}"
                "}}",
                (unsigned long)(++msgId), dht.temperature, dht.humidity);

            /* 载荷里的 " 和 , 由驱动负责转义 —— 这里照常写合法 JSON 就行,
             * 别自己预先转一遍 (转两遍会得到 \\\" 这种东西). */
            if (ESP8266_MQTT_Publish(CFG_MQTT_PUB_TOPIC, payload) == ESP8266_OK)
            {
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
