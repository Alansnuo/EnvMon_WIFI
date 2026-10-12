#ifndef __STUB_KEY_H
#define __STUB_KEY_H

/* 桩: 只保留 BSP/KEY/key.h 的对外接口, 供语法检查用。
 * ⚠️ 数值必须和真实 key.h 一致, 否则 task_key.c 里 switch 的语义会假通过。 */
#include "main.h"
#include <stdint.h>

#define KEY_ID_1        0U
#define KEY_ID_2        1U
#define KEY_ID_3        2U
#define KEY_ID_4        3U
#define KEY_ID_NUM      4U

#define KEY_SCAN_MS     20U
#define KEY_LONG_MS     1000U

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_PRESS,
    KEY_EVENT_LONG,
    KEY_EVENT_RELEASE
} Key_Event;

void      Key_Init(void);
Key_Event Key_Scan(uint8_t id);
uint8_t   Key_GetNum(uint8_t id);

#endif
