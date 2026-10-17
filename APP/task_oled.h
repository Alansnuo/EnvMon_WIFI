#ifndef __TASK_OLED_H
#define __TASK_OLED_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建 OLED 显示任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用:
 * 只挂就绪表, 调度器启动后才运行.
 *
 * 全工程仅此任务访问屏. 显示内容取自全局共享变量
 * (g_xLatestDht11 / g_xLatestLux / g_xWifiState 见 app_main.h;
 *  阈值 / 菜单 / 页面状态 / 告警码见 app_ui.h), 不直接读传感器,
 * 避免与 DHT11 / Light 采集任务时序冲突, 因而无需加锁. */
void TaskOledCreate(void);

#endif /* __TASK_OLED_H */
