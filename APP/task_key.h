#ifndef __TASK_KEY_H
#define __TASK_KEY_H

#include "main.h"

/* 按键任务: 每 KEY_SCAN_MS 扫一遍 4 个键, 把事件翻译成界面动作.
 * 界面状态(g_xUiPage / g_xMenuIdx)和阈值(g_xThresh)都在 APP/app_ui.h */
void TaskKeyCreate(void);

#endif /* __TASK_KEY_H */
