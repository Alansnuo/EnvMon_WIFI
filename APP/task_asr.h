#ifndef __TASK_ASR_H
#define __TASK_ASR_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建语音命令执行任务. */
void TaskAsrCreate(void);

#endif /* __TASK_ASR_H */
