#ifndef __TASK_WIFI_H
#define __TASK_WIFI_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* WiFi 账号密码已经搬到 APP/app_config.h (CFG_WIFI_SSID / CFG_WIFI_PASSWORD),
 * 和 MQTT 的参数放在一起 —— 它俩是同一类东西: 换个环境就得改的部署配置. */

/* 建 WiFi 任务, 由 App_Init() 在调度器启动前调用.
 *
 * 联网不在这里做 —— 那是任务自己起来后干的第一件事 (失败每 5 秒重试),
 * 之后才进上报循环. 所以本函数立刻返回, 不会拖着调度器不启动.
 * 详见 task_wifi.c. */
void TaskWifiCreate(void);

#endif /* __TASK_WIFI_H */
