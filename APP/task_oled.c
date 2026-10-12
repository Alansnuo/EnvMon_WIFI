/* ============================================================================
 * OLED 显示任务: 每 1 秒刷一次屏
 *   第 1 行  WiFi / MQTT 连接状态
 *   第 2 行  温度 / 湿度
 *   第 3 行  光照
 *   第 4 行  刷新计数 (确认任务还活着)
 *
 * 只显示, 不采集 —— 数据从全局共享变量取 (见 app_main.h), 避免和 DHT11 /
 * Light 任务争抢总线时序。
 *
 * ⚠️ 串口那一路现在专职天问语音, 所以这里看到的就是**唯一的运行状态出口**。
 * ========================================================================== */

#include "task_oled.h"
#include "app_main.h"

#include "OLED.h"

static void TaskOled(void *argument);

/* WiFi 状态 → 屏上那 3 个字的短词 */
static const char *WifiText(uint8_t state)
{
    switch (state)
    {
        case WIFI_STATE_MQTT_OK:  return "OK ";      /* 热点+MQTT 都通 */
        case WIFI_STATE_AP_OK:    return "AP ";      /* 热点通了, MQTT 还没 */
        case WIFI_STATE_FAIL:     return "ERR";      /* 这一步失败, 在重试 */
        default:                  return "...";      /* 正在连 */
    }
}

/* ========================= 建任务 ========================= */
void TaskOledCreate(void)
{
    if (xTaskCreate(TaskOled, "OLED", APP_STACK_OLED, NULL,
                    APP_PRIO_OLED, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskOled(void *argument)
{
    uint32_t     count = 0;
    DHT11_Data   dht;
    float        lux;
    unsigned int lux10;                 /* lux x10 取整, 用来拼出一位小数 */
    uint8_t      t, td, h, hd;
    uint8_t      wifi;

    (void)argument;                     /* 未使用, 显式忽略 */

    /* 开机欢迎画面, 等 1 秒让采集任务先出数 */
    OLED_ShowString(1, 1, "Env Monitor");
    OLED_ShowString(2, 1, "Starting...");

    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        count++;

        /* 拷一份共享值. 两个 uint8_t 字段 + 一个 float, 用临界区保证一致性:
         * 采集任务可能在拷到一半时切过来改值. 进出临界区极短, 不影响采集. */
        taskENTER_CRITICAL();
        dht  = g_xLatestDht11;          /* volatile -> 非 volatile, 直接赋值即可 */
        lux  = g_xLatestLux;
        wifi = g_xWifiState;
        taskEXIT_CRITICAL();

        t     = dht.temperature;
        td    = dht.temperature_dec;
        h     = dht.humidity;
        hd    = dht.humidity_dec;
        lux10 = (unsigned int)((lux * 10.0f) + 0.5f);

        /* 第 1 行: WiFi 状态. 后面跟一个字母前缀区分是"哪一段通了" */
        OLED_ShowString(1, 1, "WiFi:");
        OLED_ShowString(1, 6, (char *)WifiText(wifi));

        /* 第 2 行: 温湿度, 各带一位小数.
         * 模板 "T:  . C H:  . %" 的 3~4 / 6 / 11~12 / 14 列留给数字和点。
         * ⚠️ DHT11 的小数位恒为 0, 所以这一位会一直是 .0 —— 想看到真实小数要换
         *    DHT22/AM2302(时序一样, 但数据格式不同, 驱动解析要改)。 */
        OLED_ShowString(2, 1, "T:  . C H:  . %");
        OLED_ShowNum (2, 3,  t, 2);
        OLED_ShowChar(2, 6,  (char)('0' + td));
        OLED_ShowNum (2, 11, h, 2);
        OLED_ShowChar(2, 14, (char)('0' + hd));

        /* 第 3 行: 光照 "L:xxxx.x lx" —— 用一位小数撑住分辨率 (BH1750 0.833 lx) */
        OLED_ShowString(3, 1, "L:        lx");
        OLED_ShowNum(3, 3, lux10 / 10U, 4);
        OLED_ShowChar (3, 7, '.');
        OLED_ShowNum(3, 8, lux10 % 10U, 1);

        /* 第 4 行: 刷新计数, 用来确认任务还活着 */
        OLED_ShowString(4, 1, "N:            ");
        OLED_ShowNum(4, 3, count, 5);

        /* OLED 刷屏不重, 但每次写数据要逐字节走 I2C, 全场约 80 字节.
         * 周期 1 秒绰绰有余, 不会拖累别的任务. */
        vTaskDelay(pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}
