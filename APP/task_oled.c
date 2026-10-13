/* ============================================================================
 * OLED 显示任务 —— 全工程唯一碰屏的任务
 *
 * 别的任务 (DHT11 / 光照 / WiFi / 语音) 只往全局变量里写值, 本任务读出来刷。
 * 所以不需要任何互斥锁: **只有一个人在写 I2C**。要是两个任务都去调
 * OLED_ShowXxx(), 两次刷屏的 I2C 事务会交错, 屏上就是随机乱码。
 *
 * 页面 (KEY1 依次循环):
 *
 *   主页面 PAGE_ENV            菜单 PAGE_MENU           编辑 PAGE_EDIT
 *  ┌──────────────────┐      ┌──────────────────┐    ┌──────────────────┐
 *  │温度 25C           │      │Menu 1/7          │    │Set TempHi        │
 *  │湿度 60%           │      │>TempHi      30 C │    │温 30 C           │
 *  │光照 300lx         │      │ TempLo      10 C │    │K2+ K3- K4=OK     │
 *  │     12:00:03     │      │ HumHi       80 % │    │                  │
 *  └──────────────────┘      └──────────────────┘    └──────────────────┘
 *
 * 主页面一屏放齐 必做2 要的四项: 温度 / 湿度 / 光照 / 当前时间。
 *
 * 画法: **每行(每个数值字段)都用 snprintf 拼成定宽的一小段, 再补空格写整段**。
 *   ⚠️ 为什么不"写模板 + 再单独写数字": 模板往往盖不满该字段, 上一帧留在末尾的
 *      字符不会被擦掉, 看起来就是"显示被覆盖/串字"。定宽覆盖就没这问题。
 *   ⚠️ 汉字 16 像素宽 = 占 2 列, ASCII 8 像素宽 = 占 1 列。两者列号不能混着
 *      累加, 所以下面汉字和 ASCII 的起始列都是**算好写死**的。
 *
 * WiFi 状态**不闪烁**, 只显示状态词 (按你的要求)。
 * 超限告警时第 4 行右侧闪烁一个警告图案 (必做4 的"OLED 闪烁提示"),
 * 告警码由语音任务算好写在 g_xAlarmCode 里 —— 只算一处, 两边不会打架。
 *
 * 时间还是**模拟值**(板上没有 RTC), 用本任务的 1 秒周期当秒脉冲, 见 app_ui.h。
 * ========================================================================== */

#include "task_oled.h"
#include "app_main.h"
#include "app_ui.h"

#include "OLED.h"

#include <stdio.h>
#include <string.h>

/* 汉字字形下标 HZK_xxx 在 OLED.h 的枚举里定义, 本文件 include "OLED.h" 即可用。
 * 字模表 Hzk1[30][32] 在 OLED_Font.h, 只有 OLED.c include 它 (避免重复定义)，
 * task_oled.c 不直接碰字模, 只用下标调 OLED_ShowChinese()。 */

#define OLED_COLS       16U     /* 128x64 配 8x16 字体, 一行 16 个 ASCII */
#define MENU_ROWS       3U      /* 菜单页除标题外能显示几项 */

static void TaskOled(void *argument);

/* 每帧翻转一次, 用来做告警闪烁 (周期 = 2 x APP_OLED_PERIOD_MS) */
static uint8_t s_Blink = 0U;

/* WiFi 状态 → 屏上那几个字的短词 */
static const char *WifiText(uint8_t state)
{
    switch (state)
    {
        case WIFI_STATE_MQTT_OK:  return "OK";
        case WIFI_STATE_AP_OK:    return "AP";
        case WIFI_STATE_FAIL:     return "ERR";
        default:                  return "...";
    }
}

/* 把一段 ASCII 补齐到 width 列再写 —— 保证整个字段被覆盖,
 * 不留上一帧的残字。
 *
 * ⚠️ buf 必须**可写**: 本函数会往里补空格, 传字符串字面量会写崩。
 * ⚠️ size 传 sizeof(buf), 不要手填: 本函数按 size 夹住写入位置,
 *    这样"内容比 width 长"或"width 比 buf 大"都只会少补空格, 不会写飞。
 *    调用方统一写 ShowPad(line, col, buf, sizeof(buf), width)。 */
static void ShowPad(uint8_t line, uint8_t col, char *buf, uint32_t size, uint8_t width)
{
    uint8_t n = (uint8_t)strlen(buf);

    if ((uint32_t)n >= size)             /* 内容已经填满缓冲: 只截断, 不再补 */
    {
        buf[size - 1U] = '\0';
        OLED_ShowString(line, col, buf);
        return;
    }

    while ((n < width) && ((uint32_t)n < (size - 1U)))
    {
        buf[n] = ' ';
        n++;
    }
    buf[n] = '\0';

    OLED_ShowString(line, col, buf);
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

/* ========================= 各页面 ========================= */

/* --- 主页面: 温度 / 湿度 / 光照 / 时间 (必做2 要求的四项) --- */
static void DrawEnvPage(void)
{
    DHT11_Data   dht;
    float        lux;
    unsigned int lux10;
    uint8_t      wifi  = g_xWifiState;
    uint8_t      alarm = g_xAlarmCode;
    Clock_Time   now;
    char         buf[OLED_COLS + 4U];

    /* 三个采集值一起拷出来, 免得同一屏里读到"半新半旧"的组合 */
    taskENTER_CRITICAL();
    dht = g_xLatestDht11;
    lux = g_xLatestLux;
    taskEXIT_CRITICAL();

    lux10 = (unsigned int)((lux * 10.0f) + 0.5f);
    now   = AppClock_Now();

    /* 第 1 行: 温度。
     * ⚠️ "温"占第 1~2 列, "度"占第 3~4 列 (16 像素 = 2 列), 所以数值必须从
     *    第 5 列 (像素 32) 起 —— 从第 4 列起会和"度"的右半边 (像素 24~31) 重叠,
     *    屏上看到的就是"度"被削掉一块。 */
    OLED_ShowChinese(1U, 1U, HZK_WEN);
    OLED_ShowChinese(1U, 2U, HZK_DU);
    (void)snprintf(buf, sizeof(buf), "%2d.%dC",
                   (int)dht.temperature, (int)dht.temperature_dec);
    ShowPad(1U, 5U, buf, sizeof(buf), 5U);

    /* 第 2 行: 湿度 (汉字排列同第 1 行, 数值也从第 5 列起) */
    OLED_ShowChinese(2U, 1U, HZK_SHI);
    OLED_ShowChinese(2U, 2U, HZK_DU);
    (void)snprintf(buf, sizeof(buf), "%2d.%d%%",
                   (int)dht.humidity, (int)dht.humidity_dec);
    ShowPad(2U, 5U, buf, sizeof(buf), 5U);

    /* 第 3 行: 光照强度。4 个汉字排在 1~8 列, 数值从第 10 列起
     * (光照强度 = HZK_GUANG, HZK_ZHAO, HZK_QIANG, HZK_DU3 —— 第 4 个字是"度") */
    OLED_ShowChinese(3U, 1U, HZK_GUANG);
    OLED_ShowChinese(3U, 2U, HZK_ZHAO);
    OLED_ShowChinese(3U, 3U, HZK_QIANG);
    OLED_ShowChinese(3U, 4U, HZK_DU3);
    (void)snprintf(buf, sizeof(buf), "%4u lx", lux10 / 10U);
    ShowPad(3U, 10U, buf, sizeof(buf), 7U);

    /* WiFi 状态: 静态显示, 不闪烁 (按你的要求)。放第 4 行左边, 和时间同一行 */
    (void)snprintf(buf, sizeof(buf), "WiFi:%s", WifiText(wifi));
    ShowPad(4U, 1U, buf, sizeof(buf), 7U);

    /* 第 4 行: 时间 (居中在右半屏) */
    (void)snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                   (unsigned)now.hour, (unsigned)now.min, (unsigned)now.sec);
    ShowPad(4U, 9U, buf, sizeof(buf), 8U);

    /* 超限告警: 第 1 行右侧闪烁, 画在汉字和温度值都够不到的地方 (必做4)。
     * 只画"亮"的那半周期, "灭"的时候整个字段写空格盖掉 */
    if ((alarm != 0U) && (s_Blink != 0U))
    {
        OLED_ShowChinese(1U, 7U, HZK_JINGGAO);      /* 第 13~14 列 */
    }
    else
    {
        OLED_ShowString(1U, 13U, "  ");             /* 一个汉字宽 = 两列 */
    }
}

/* --- 菜单页: 滚动列表, 选中项前面加 '>' --- */
static void DrawMenuPage(void)
{
    uint8_t idx = g_xMenuIdx;
    uint8_t top;
    uint8_t row;
    char    buf[OLED_COLS + 4U];

    if (idx >= MENU_COUNT) { idx = 0U; }

    /* 让选中项尽量落在第 2 行, 同时不让列表滚出边界 */
    top = (idx > 0U) ? (uint8_t)(idx - 1U) : 0U;
    if (top > (uint8_t)(MENU_COUNT - MENU_ROWS)) { top = (uint8_t)(MENU_COUNT - MENU_ROWS); }

    (void)snprintf(buf, sizeof(buf), "Menu %u/%u",
                   (unsigned)(idx + 1U), (unsigned)MENU_COUNT);
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    for (row = 0U; row < MENU_ROWS; row++)
    {
        uint8_t item = (uint8_t)(top + row);
        uint8_t line = (uint8_t)(row + 2U);

        if (item >= MENU_COUNT)
        {
            ShowPad(line, 1U, (char *)"", 1U, OLED_COLS);
            continue;
        }

        (void)snprintf(buf, sizeof(buf), "%c%-6s %4d %s",
                       (item == idx) ? '>' : ' ',
                       Menu_Name(item), (int)Menu_Value(item), Menu_Unit(item));
        ShowPad(line, 1U, buf, sizeof(buf), OLED_COLS);
    }
}

/* --- 编辑页: 改选中项的值 --- */
static void DrawEditPage(void)
{
    uint8_t idx = g_xMenuIdx;
    char    buf[OLED_COLS + 4U];

    if (idx >= MENU_COUNT) { idx = 0U; }

    (void)snprintf(buf, sizeof(buf), "Set %s", Menu_Name(idx));
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    (void)snprintf(buf, sizeof(buf), "%s = %4d %s",
                   Menu_Name(idx), (int)Menu_Value(idx), Menu_Unit(idx));
    ShowPad(2U, 1U, buf, sizeof(buf), OLED_COLS);

    ShowPad(3U, 1U, (char *)"K2+ K3- K4=OK", 14U, OLED_COLS);
    ShowPad(4U, 1U, (char *)"", 1U, OLED_COLS);
}

/* ========================= 任务本体 ========================= */
static void TaskOled(void *argument)
{
    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        AppClock_Tick();                /* 本任务周期正好 1 秒, 当秒脉冲用 */
        s_Blink ^= 1U;                  /* 告警闪烁: 每帧翻一次 */

        switch (g_xUiPage)
        {
            case UI_PAGE_MENU: DrawMenuPage(); break;
            case UI_PAGE_EDIT: DrawEditPage(); break;
            default:           DrawEnvPage();  break;
        }

        vTaskDelay(pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}
