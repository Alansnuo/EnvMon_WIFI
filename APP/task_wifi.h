#ifndef __TASK_WIFI_H
#define __TASK_WIFI_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* WiFi 账号密码与 MQTT 参数同属部署配置, 集中在 APP/app_config.h. */

/* 建 WiFi 任务, 由 App_Init() 在调度器启动前调用.
 *
 * 联网在任务内进行 (启动后第一件事, 失败每 5 秒重试), 之后进入上报循环;
 * 本函数立即返回, 不阻塞调度器启动. 详见 task_wifi.c. */
void TaskWifiCreate(void);

#endif /* __TASK_WIFI_H */
