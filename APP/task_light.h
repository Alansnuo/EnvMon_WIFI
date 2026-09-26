#ifndef __TASK_LIGHT_H
#define __TASK_LIGHT_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建光照采集任务. 由启动任务在联网成功后调用 (此时调度器已在跑).
 * 还没实现, 见 task_light.c 的说明. */
void TaskLightCreate(void);

#endif /* __TASK_LIGHT_H */
