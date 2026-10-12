/* ============================================================================
 * 按键任务 —— 每 20ms 扫一遍, 把"按键事件"翻译成"界面动作"
 *
 * 键位:
 *   KEY1 (PA0)  **直接切换页面**   主页面 -> 菜单 -> 编辑 -> 主页面 循环
 *   KEY2 (PA1)  **上移 / 加**      菜单里往上选一项; 编辑里 +1
 *   KEY3 (PA4)  **下移 / 减**      菜单里往下选一项; 编辑里 -1
 *   KEY4 (PA5)  确认              菜单 -> 编辑该项; 编辑 -> 回菜单
 *
 * 主页面里 KEY2 按下 = 让语音模块立刻播报一次 (必做3 的"按按键触发")。
 *
 * 按键怎么读、怎么消抖在 BSP/KEY/key.c; 本文件只认 KEY_EVENT_PRESS
 * (按下那一刻报一次, 按住不放不会重复报)。
 *
 * 本任务只改共享变量, 不碰 OLED / 不碰语音 —— 那两个任务各自去读。
 * 单向数据流, 所以不需要加锁。
 * ========================================================================== */

#include "task_key.h"
#include "app_main.h"
#include "app_ui.h"

#include "asr.h"
#include "key.h"

static void TaskKey(void *argument);

/* ------------------------------ KEY1: 直接切页 ------------------------------ */
/* 主页面 -> 菜单 -> 编辑 -> 主页面, 一圈循环 */
static void Key1_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    switch (g_xUiPage)
    {
        case UI_PAGE_ENV:  g_xUiPage = UI_PAGE_MENU; break;
        case UI_PAGE_MENU: g_xUiPage = UI_PAGE_EDIT; break;
        case UI_PAGE_EDIT: g_xUiPage = UI_PAGE_ENV;  break;
        default:           g_xUiPage = UI_PAGE_ENV;  break;
    }
}

/* ------------------------------ KEY2: 上移 / 加 ------------------------------ */
static void Key2_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    if (g_xUiPage == UI_PAGE_MENU)
    {
        if (g_xMenuIdx > 0U) { g_xMenuIdx--; }          /* 往上选一项, 到顶就停 */
    }
    else if (g_xUiPage == UI_PAGE_EDIT)
    {
        Menu_Adj(g_xMenuIdx, +1);
    }
    else
    {
        g_xAsrSpeakReq = 1U;                            /* 主页面: 手动播报一次 */
    }
}

/* ------------------------------ KEY3: 下移 / 减 ------------------------------ */
static void Key3_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    if (g_xUiPage == UI_PAGE_MENU)
    {
        if (g_xMenuIdx < (uint8_t)(MENU_COUNT - 1U)) { g_xMenuIdx++; }  /* 到底就停 */
    }
    else if (g_xUiPage == UI_PAGE_EDIT)
    {
        Menu_Adj(g_xMenuIdx, -1);
    }
}

/* ------------------------------ KEY4: 确认 ------------------------------ */
static void Key4_Action(Key_Event ev)
{
    if (ev != KEY_EVENT_PRESS) { return; }

    if (g_xUiPage == UI_PAGE_EDIT)
    {
        g_xUiPage = UI_PAGE_MENU;       /* 改完了回菜单。
                                         * 阈值本来就在 RAM 变量里, 没什么可"写"的;
                                         * 以后要断电保存, 在这里落一次 Flash 即可 */
    }
    else
    {
        g_xUiPage = UI_PAGE_MENU;       /* 主页面/菜单 都先进菜单 */
    }
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
