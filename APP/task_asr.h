#ifndef __TASK_ASR_H
#define __TASK_ASR_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建语音命令执行任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用 */
void TaskAsrCreate(void);

#endif /* __TASK_ASR_H */
