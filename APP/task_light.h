#ifndef __TASK_LIGHT_H
#define __TASK_LIGHT_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建光照采集任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用:
 * 只挂就绪表, 调度器启动后才运行. */
void TaskLightCreate(void);

#endif /* __TASK_LIGHT_H */
