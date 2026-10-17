#ifndef __TASK_KEY_H
#define __TASK_KEY_H

#include "main.h"

/* 按键任务: 每 KEY_SCAN_MS 扫描 4 个键一次, 将事件翻译为界面动作.
 * 界面状态(g_xUiPage / g_xMenuIdx)与阈值(g_xThresh)见 APP/app_ui.h */
void TaskKeyCreate(void);

#endif /* __TASK_KEY_H */
