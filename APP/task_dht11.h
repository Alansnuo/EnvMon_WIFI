#ifndef __TASK_DHT11_H
#define __TASK_DHT11_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 建 DHT11 采集任务. 由 App_Init() 在 vTaskStartScheduler() 之前调用:
 * 只挂就绪表, 调度器启动后才运行. */
void TaskDht11Create(void);

#endif /* __TASK_DHT11_H */
