/* ============================================================================
 * 界面状态实现 —— 时钟 + 阈值 + 菜单 + 页面
 *
 * 定义见 app_ui.h。本文件只做纯数据操作, 不碰任何外设, 不调 vTaskDelay,
 * 所以调度器起来前后调用都安全 (UI_Init() 就是在 App_Init() 里调的)。
 * ========================================================================== */

#include "app_ui.h"
#include "app_config.h"

/* ========================= 一、时钟 ========================= */

static volatile Clock_Time s_Now;

void AppClock_Init(uint8_t hour, uint8_t min)
{
    s_Now.hour = (uint8_t)(hour % 24U);
    s_Now.min  = (uint8_t)(min  % 60U);
    s_Now.sec  = 0U;
}

void AppClock_Tick(void)
{
    s_Now.sec++;
    if (s_Now.sec >= 60U)
    {
        s_Now.sec = 0U;
        s_Now.min++;
        if (s_Now.min >= 60U)
        {
            s_Now.min  = 0U;
            s_Now.hour = (uint8_t)((s_Now.hour + 1U) % 24U);
        }
    }
}

Clock_Time AppClock_Now(void)
{
    Clock_Time t;

    t.hour = s_Now.hour;
    t.min  = s_Now.min;
    t.sec  = s_Now.sec;

    return t;
}

uint8_t AppClock_GetHour(void)
{
    return s_Now.hour;
}

void AppClock_SetHour(uint8_t hour)
{
    s_Now.hour = (uint8_t)(hour % 24U);
}

/* ========================= 二、阈值 ========================= */

volatile int16_t g_xThresh[TH_COUNT];

/* 每一项的取值范围和步进。光照用 10 lx 一档, 不然从 0 按到 600 得按 600 下 */
typedef struct {
    int16_t min;
    int16_t max;
    int16_t step;
} Thresh_Range;

static const Thresh_Range s_Range[TH_COUNT] = {
    {   0,   60,  1 },      /* 温度上限 */
    {   0,   60,  1 },      /* 温度下限 */
    {   0,  100,  1 },      /* 湿度上限 */
    {   0,  100,  1 },      /* 湿度下限 */
    {   0, 2000, 10 },      /* 光照下限 */
    {   0, 2000, 10 },      /* 光照上限 */
};

/* 菜单上显示的项名, 索引和 Thresh_Id 一一对应 */
static const char *s_MenuName[MENU_COUNT] = {
    "TempHi", "TempLo", "HumHi", "HumLo", "LuxLo", "LuxHi"
};

/* 上下限不能撞在一起: 调上限时不许低于下限, 反之亦然 */
static int16_t Thresh_PairClamp(Thresh_Id id, int16_t v)
{
    switch (id)
    {
        case TH_TEMP_UP: if (v < g_xThresh[TH_TEMP_DN]) { v = g_xThresh[TH_TEMP_DN]; } break;
        case TH_TEMP_DN: if (v > g_xThresh[TH_TEMP_UP]) { v = g_xThresh[TH_TEMP_UP]; } break;
        case TH_HUMI_UP: if (v < g_xThresh[TH_HUMI_DN]) { v = g_xThresh[TH_HUMI_DN]; } break;
        case TH_HUMI_DN: if (v > g_xThresh[TH_HUMI_UP]) { v = g_xThresh[TH_HUMI_UP]; } break;
        case TH_LUX_UP:  if (v < g_xThresh[TH_LUX_DN])  { v = g_xThresh[TH_LUX_DN];  } break;
        case TH_LUX_DN:  if (v > g_xThresh[TH_LUX_UP])  { v = g_xThresh[TH_LUX_UP];  } break;
        default: break;
    }
    return v;
}

int16_t Thresh_Get(Thresh_Id id)
{
    if ((uint32_t)id >= (uint32_t)TH_COUNT) { return 0; }
    return g_xThresh[id];
}

void Thresh_Adj(Thresh_Id id, int8_t dir)
{
    int16_t v;

    if ((uint32_t)id >= (uint32_t)TH_COUNT) { return; }

    v = g_xThresh[id];
    v += (dir > 0) ? s_Range[id].step : (int16_t)(-s_Range[id].step);

    if (v < s_Range[id].min) { v = s_Range[id].min; }
    if (v > s_Range[id].max) { v = s_Range[id].max; }

    v = Thresh_PairClamp(id, v);
    g_xThresh[id] = v;
}

/* ========================= 三、菜单 ========================= */

volatile uint8_t g_xMenuIdx = 0U;

const char *Menu_Name(uint8_t idx)
{
    if (idx >= MENU_COUNT) { return "??"; }
    return s_MenuName[idx];
}

const char *Menu_Unit(uint8_t idx)
{
    switch ((Thresh_Id)idx)
    {
        case TH_TEMP_UP:
        case TH_TEMP_DN: return "C";
        case TH_HUMI_UP:
        case TH_HUMI_DN: return "%";
        default:         return "lx";
    }
}

int16_t Menu_Value(uint8_t idx)
{
    if (idx >= MENU_COUNT) { return 0; }
    return Thresh_Get((Thresh_Id)idx);
}

void Menu_Adj(uint8_t idx, int8_t dir)
{
    if (idx >= MENU_COUNT) { return; }
    Thresh_Adj((Thresh_Id)idx, dir);
}

/* ========================= 四、设置面分类 ========================= */

volatile uint8_t g_xSetIdx = 0U;

static const char *s_SetName[SET_COUNT] = {
    "Temperature", "Humidity", "Light", "Threshold"
};

const char *Set_Name(uint8_t idx)
{
    if (idx >= SET_COUNT) { return "??"; }
    return s_SetName[idx];
}

/* ========================= 五、页面 ========================= */

volatile uint8_t g_xUiPage    = UI_PAGE_ENV;
volatile uint8_t g_xAlarmCode = 0U;

/* ------------------------------ 初始化 ------------------------------ */

void UI_Init(void)
{
    g_xThresh[TH_TEMP_UP] = (int16_t)CFG_TH_TEMP_UP;
    g_xThresh[TH_TEMP_DN] = (int16_t)CFG_TH_TEMP_DN;
    g_xThresh[TH_HUMI_UP] = (int16_t)CFG_TH_HUMI_UP;
    g_xThresh[TH_HUMI_DN] = (int16_t)CFG_TH_HUMI_DN;
    g_xThresh[TH_LUX_DN]  = (int16_t)CFG_TH_LUX_DN;
    g_xThresh[TH_LUX_UP]  = (int16_t)CFG_TH_LUX_UP;

    g_xMenuIdx   = 0U;
    g_xSetIdx    = 0U;
    g_xUiPage    = UI_PAGE_ENV;
    g_xAlarmCode = 0U;

    /* 模拟时钟: 板上没有 RTC, 先从一个假起点开始走。
     * 以后接上真时间, 换掉 AppClock_Now() 的实现就行, 界面代码不用动。 */
    AppClock_Init(12U, 0U);
}
