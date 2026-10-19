/* ============================================================================
 * OLED 显示任务: 全工程唯一访问屏的任务 (单写者, 无需互斥锁)
 *
 * 页面 (页面号见 app_ui.h): 首页(时钟 + WiFi 状态) / 温度 / 湿度 / 光照 /
 * 设置面 / 菜单 / 编辑, 由按键任务切页, 本任务按页绘制.
 *
 * 画法: 每个字段 snprintf 拼成定宽段, 补空格后整段写出, 覆盖上一帧残字.
 * 时间: WiFi 任务对时校准; 秒进位按 tick 累计 (循环周期 = 刷屏耗时 + 1s > 1s,
 * 按循环次数计会越走越慢).
 * ========================================================================== */

#include "task_oled.h"
#include "app_main.h"
#include "app_ui.h"

#include "OLED.h"

#include <stdio.h>
#include <string.h>


#define OLED_COLS       16U     /* 128x64 配 8x16 字体, 一行 16 个 ASCII */
#define MENU_ROWS       3U      /* 菜单页除标题外能显示几项 */

static void TaskOled(void *argument);

/* WiFi 状态 → 屏上显示词 */
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

/* 将 ASCII 串补齐到 width 列后写出, 覆盖上一帧残字.
 * buf 须可写; size 传 sizeof(buf), 写入位置按 size 夹紧. */
static void ShowPad(uint8_t line, uint8_t col, char *buf, uint32_t size, uint8_t width)
{
    uint32_t n = strlen(buf);

    if (n >= size)                          /* 内容已经填满缓冲: 只截断, 不再补 */
    {
        buf[size - 1U] = '\0';
        OLED_ShowString(line, col, buf);
        return;
    }

    while ((n < width) && (n < (size - 1U)))
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

/* --- 主页面: 时间 + WiFi 连接状态 --- */
static void DrawEnvPage(void)
{
    char       buf[OLED_COLS + 4U];
    uint8_t    wifi = g_xWifiState;
    Clock_Time t    = AppClock_Now();

    (void)snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                   (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
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

    /* 选中项尽量落在第 2 行, 且列表不滚出边界 */
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
    uint8_t    lastPage = 0xFFU;
    TickType_t lastTick = xTaskGetTickCount();

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();

        /* 秒脉冲按 tick 累计: 每满 1000ms 加 1 秒 (按循环次数计会越走越慢) */
        while ((now - lastTick) >= pdMS_TO_TICKS(1000))
        {
            AppClock_Tick();
            lastTick += pdMS_TO_TICKS(1000);
        }

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
