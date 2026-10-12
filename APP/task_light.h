#ifndef __TASK_LIGHT_H
#define __TASK_LIGHT_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建光照采集任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用 ——
 * 此时只是把任务挂到就绪表, 真正开跑要等调度器起来. */
void TaskLightCreate(void);

#endif /* __TASK_LIGHT_H */
