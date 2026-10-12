#ifndef __TASK_OLED_H
#define __TASK_OLED_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建 OLED 显示任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用 ——
 * 此时只是把任务挂到就绪表, 真正开跑要等调度器起来.
 *
 * 全工程只有这一个任务碰屏. 显示内容从全局共享变量取
 * (g_xLatestDht11 / g_xLatestLux / g_xWifiState 见 app_main.h;
 *  阈值 / 菜单 / 页面状态 / 告警码见 app_ui.h), 不直接读传感器 ——
 * 避免和 DHT11 / Light 采集任务的时序冲突, 也就不需要任何锁. */
void TaskOledCreate(void);

#endif /* __TASK_OLED_H */
