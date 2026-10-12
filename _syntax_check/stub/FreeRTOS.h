#ifndef __STUB_FREERTOS_H
#define __STUB_FREERTOS_H

/* 桩: FreeRTOS 最小面 —— 只够编译任务/临界区相关代码 */
#include <stdint.h>
#include <stddef.h>

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;

#define pdPASS      1
#define pdTRUE      1
#define pdFALSE     0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)

typedef void *TaskHandle_t;

typedef struct { uint32_t dummy; } StaticQueue_t;
typedef void *QueueHandle_t;

BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                       uint16_t stack, void *arg, UBaseType_t prio,
                       TaskHandle_t *handle);

void vTaskDelay(TickType_t ticks);

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);

uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t t);

/* 临界区: 真实实现是关中断, 这里给空操作 */
void vPortEnterCritical(void);
void vPortExitCritical(void);
#define taskENTER_CRITICAL() vPortEnterCritical()
#define taskEXIT_CRITICAL()  vPortExitCritical()

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif /* __STUB_FREERTOS_H */
