/* ============================================================================
 * 按键任务 —— 每 20ms 扫一遍, 把"按键事件"翻译成"界面动作"
 *
 * 显示页 (KEY2/KEY3 循环切页: 首页 -> 温度 -> 湿度 -> 光照 -> 首页):
 *   KEY1  进入设置面 (菜单)
 *   KEY2  上一页          KEY3  下一页          KEY4  无动作
 *
 * 设置面 (菜单) / 编辑页:
 *   KEY1  菜单 -> 退出到首页; 编辑 -> 回菜单
 *   KEY2  菜单里往上选一项; 编辑里 +1 (阈值加)
 *   KEY3  菜单里往下选一项; 编辑里 -1 (阈值减)
 *   KEY4  确认: 菜单 -> 进编辑该项; 编辑 -> 回菜单
 *
 * 消抖在 BSP/KEY/key.c; 本文件只认 KEY_EVENT_PRESS (按下报一次)。
 * 只改共享变量, 不碰 OLED / 语音 —— 单向数据流, 无需加锁。
 * ========================================================================== */

#include "task_key.h"
#include "app_main.h"
#include "app_ui.h"

#include "key.h"

static void TaskKey(void *argument);

/* 显示页顺序表 —— KEY2/KEY3 就在这几页之间循环 */
#define DISP_PAGE_COUNT 4U
static const uint8_t s_DispPage[DISP_PAGE_COUNT] = {
    UI_PAGE_ENV, UI_PAGE_TEMP, UI_PAGE_HUMI, UI_PAGE_LUX
};

/* 在显示页之间上下切页 (dir = -1 上一页 / +1 下一页), 到头自动绕回 */
static void DispPage_Cycle(int8_t dir)
{
    uint8_t i;
    int8_t  idx = -1;

    for (i = 0U; i < DISP_PAGE_COUNT; i++)
    {
        if (s_DispPage[i] == g_xUiPage) { idx = (int8_t)i; break; }
    }
    if (idx < 0) { g_xUiPage = UI_PAGE_ENV; return; }

    idx += dir;
    if (idx < 0) { idx = (int8_t)(DISP_PAGE_COUNT - 1U); }
    if (idx >= (int8_t)DISP_PAGE_COUNT) { idx = 0; }

    g_xUiPage = s_DispPage[idx];
}

/* ------------------------------ KEY1: 进设置面 / 返回 ------------------------------ */
static void Key1_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    switch (g_xUiPage)
    {
        case UI_PAGE_SET:  g_xUiPage = UI_PAGE_ENV;  break;   /* 设置面 -> 首页 */
        case UI_PAGE_MENU: g_xUiPage = UI_PAGE_SET;  break;   /* 阈值列表 -> 设置面 */
        case UI_PAGE_EDIT: g_xUiPage = UI_PAGE_MENU; break;   /* 编辑   -> 阈值列表 */
        default:           g_xUiPage = UI_PAGE_SET;  break;   /* 显示页 -> 进设置面 */
    }
}

/* ------------------------------ KEY2: 上一页 / 上移 / 加 ------------------------------ */
static void Key2_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    switch (g_xUiPage)
    {
        case UI_PAGE_SET:
            if (g_xSetIdx > 0U) { g_xSetIdx--; }
            break;
        case UI_PAGE_MENU:
            if (g_xMenuIdx > 0U) { g_xMenuIdx--; }      /* 往上选一项, 到顶就停 */
            break;
        case UI_PAGE_EDIT:
            Menu_Adj(g_xMenuIdx, +1);                   /* 阈值 + */
            break;
        default:
            DispPage_Cycle(-1);                         /* 显示页: 上一页 */
            break;
    }
}

/* ------------------------------ KEY3: 下一页 / 下移 / 减 ------------------------------ */
static void Key3_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    switch (g_xUiPage)
    {
        case UI_PAGE_SET:
            if (g_xSetIdx < (uint8_t)(SET_COUNT - 1U)) { g_xSetIdx++; }
            break;
        case UI_PAGE_MENU:
            if (g_xMenuIdx < (uint8_t)(MENU_COUNT - 1U)) { g_xMenuIdx++; }  /* 到底就停 */
            break;
        case UI_PAGE_EDIT:
            Menu_Adj(g_xMenuIdx, -1);                   /* 阈值 - */
            break;
        default:
            DispPage_Cycle(+1);                         /* 显示页: 下一页 */
            break;
    }
}

/* ------------------------------ KEY4: 确认 ------------------------------ */
static void Key4_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    if (g_xUiPage == UI_PAGE_SET)
    {
        switch (g_xSetIdx)
        {
            case SET_TEMP: g_xUiPage = UI_PAGE_TEMP; break;
            case SET_HUMI: g_xUiPage = UI_PAGE_HUMI; break;
            case SET_LUX:  g_xUiPage = UI_PAGE_LUX;  break;
            default:       g_xUiPage = UI_PAGE_MENU; break;
        }
    }
    else if (g_xUiPage == UI_PAGE_MENU)
    {
        g_xUiPage = UI_PAGE_EDIT;       /* 确认 -> 进编辑该项 */
    }
    else if (g_xUiPage == UI_PAGE_EDIT)
    {
        g_xUiPage = UI_PAGE_MENU;       /* 改完回设置面 (阈值就在 RAM 变量里) */
    }
    /* 显示页: KEY4 无动作 */
}

/* ========================= 建任务 ========================= */
void TaskKeyCreate(void)
{
    if (xTaskCreate(TaskKey, "Key", APP_STACK_KEY, NULL,
                    APP_PRIO_KEY, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskKey(void *argument)
{
    (void)argument;                 /* 未使用, 显式忽略 */

    for (;;)
    {
        Key1_Action(Key_Scan(KEY_ID_1));
        Key2_Action(Key_Scan(KEY_ID_2));
        Key3_Action(Key_Scan(KEY_ID_3));
        Key4_Action(Key_Scan(KEY_ID_4));

        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_MS));
    }
}
