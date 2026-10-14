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
 *  │WiFi State        │      │Menu 1/7          │    │Set TempHi        │
 *  │Connecting        │      │>TempHi      30 C │    │温 30 C           │
 *  │                  │      │ TempLo      10 C │    │K2+ K3- K4=OK     │
 *  │                  │      │ HumHi       80 % │    │                  │
 *  └──────────────────┘      └──────────────────┘    └──────────────────┘
 *
 * 主页面只显示 WiFi 连接状态。
 *
 * 画法: **每行(每个数值字段)都用 snprintf 拼成定宽的一小段, 再补空格写整段**。
 *   ⚠️ 为什么不"写模板 + 再单独写数字": 模板往往盖不满该字段, 上一帧留在末尾的
 *      字符不会被擦掉, 看起来就是"显示被覆盖/串字"。定宽覆盖就没这问题。
 *   ⚠️ 汉字 16 像素宽 = 占 2 列, ASCII 8 像素宽 = 占 1 列。两者列号不能混着
 *      累加, 所以下面汉字和 ASCII 的起始列都是**算好写死**的。
 *
 * WiFi 状态只显示状态词, 不闪烁。
 *
 * 时钟仍在后台走字 (app_ui.c 的 AppClock_Tick), 但首页不再显示时间。
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

/* WiFi 状态 → 屏上那几个字的短词 */
static const char *WifiText(uint8_t state)
{
    switch (state)
    {
        case WIFI_STATE_MQTT_OK:  return "Online";
        case WIFI_STATE_AP_OK:    return "AP Linked";
        case WIFI_STATE_FAIL:     return "Connect Fail";
        default:                  return "Connecting";
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
    char    buf[OLED_COLS + 4U];
    uint8_t wifi = g_xWifiState;

    (void)snprintf(buf, sizeof(buf), "WiFi State");
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    (void)snprintf(buf, sizeof(buf), "%s", WifiText(wifi));
    ShowPad(2U, 1U, buf, sizeof(buf), OLED_COLS);
}

/* --- 温度显示页 --- */
static void DrawTempPage(void)
{
    DHT11_Data dht;
    char       buf[OLED_COLS + 4U];

    taskENTER_CRITICAL();
    dht = g_xLatestDht11;
    taskEXIT_CRITICAL();

    (void)snprintf(buf, sizeof(buf), "Temperature");
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    (void)snprintf(buf, sizeof(buf), "%u C", (unsigned)dht.temperature);
    ShowPad(2U, 1U, buf, sizeof(buf), OLED_COLS);
}

/* --- 湿度显示页 --- */
static void DrawHumiPage(void)
{
    DHT11_Data dht;
    char       buf[OLED_COLS + 4U];

    taskENTER_CRITICAL();
    dht = g_xLatestDht11;
    taskEXIT_CRITICAL();

    (void)snprintf(buf, sizeof(buf), "Humidity");
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    (void)snprintf(buf, sizeof(buf), "%u %%RH", (unsigned)dht.humidity);
    ShowPad(2U, 1U, buf, sizeof(buf), OLED_COLS);
}

/* --- 光照显示页 --- */
static void DrawLuxPage(void)
{
    float lux;
    char  buf[OLED_COLS + 4U];

    taskENTER_CRITICAL();
    lux = g_xLatestLux;
    taskEXIT_CRITICAL();

    (void)snprintf(buf, sizeof(buf), "Light (lx)");
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    (void)snprintf(buf, sizeof(buf), "%d lx", (int)lux);
    ShowPad(2U, 1U, buf, sizeof(buf), OLED_COLS);
}

/* --- 设置面第一级: 分类列表 (温度/湿度/光照/阈值设置) --- */
static void DrawSetPage(void)
{
    uint8_t idx = g_xSetIdx;
    uint8_t top;
    uint8_t row;
    char    buf[OLED_COLS + 4U];

    if (idx >= SET_COUNT) { idx = 0U; }

    top = (idx > 0U) ? (uint8_t)(idx - 1U) : 0U;
    if (top > (uint8_t)(SET_COUNT - MENU_ROWS)) { top = (uint8_t)(SET_COUNT - MENU_ROWS); }

    (void)snprintf(buf, sizeof(buf), "Settings %u/%u",
                   (unsigned)(idx + 1U), (unsigned)SET_COUNT);
    ShowPad(1U, 1U, buf, sizeof(buf), OLED_COLS);

    for (row = 0U; row < MENU_ROWS; row++)
    {
        uint8_t item = (uint8_t)(top + row);
        uint8_t line = (uint8_t)(row + 2U);

        if (item >= SET_COUNT)
        {
            char empty[OLED_COLS + 1U];
            empty[0] = '\0';
            ShowPad(line, 1U, empty, sizeof(empty), OLED_COLS);
            continue;
        }

        (void)snprintf(buf, sizeof(buf), "%c%s",
                       (item == idx) ? '>' : ' ', Set_Name(item));
        ShowPad(line, 1U, buf, sizeof(buf), OLED_COLS);
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
            char empty[OLED_COLS + 1U];
            empty[0] = '\0';
            ShowPad(line, 1U, empty, sizeof(empty), OLED_COLS);
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

    {
        char tmp[OLED_COLS + 4U];
        (void)snprintf(tmp, sizeof(tmp), "K2+ K3- K4=OK");
        ShowPad(3U, 1U, tmp, sizeof(tmp), OLED_COLS);
    }
    {
        char empty[OLED_COLS + 1U];
        empty[0] = '\0';
        ShowPad(4U, 1U, empty, sizeof(empty), OLED_COLS);
    }
}

/* ========================= 任务本体 ========================= */
static void TaskOled(void *argument)
{
    uint8_t lastPage = 0xFFU;

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        AppClock_Tick();                /* 本任务周期正好 1 秒, 当秒脉冲用 */

        if (g_xUiPage != lastPage)      /* 只在切页时整屏擦一次, 避免每帧闪烁 */
        {
            OLED_Clear();
            lastPage = g_xUiPage;
        }

        switch (g_xUiPage)
        {
            case UI_PAGE_TEMP: DrawTempPage(); break;
            case UI_PAGE_HUMI: DrawHumiPage(); break;
            case UI_PAGE_LUX:  DrawLuxPage();  break;
            case UI_PAGE_SET:  DrawSetPage();  break;
            case UI_PAGE_MENU: DrawMenuPage(); break;
            case UI_PAGE_EDIT: DrawEditPage(); break;
            default:           DrawEnvPage();  break;
        }

        vTaskDelay(pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}
