#ifndef __KEY_H
#define __KEY_H

#include "main.h"
#include <stdint.h>

/* 4 个独立按键 —— 扫描式读取, 不用中断.
 * 全部内部上拉: 松手=高, 按下=低 (不用外接电阻).
 * FreeRTOS 下不能阻塞等松手: 消抖确认按下即返回, 松手前不重复报.
 * 扫描周期即消抖时间 (KEY_SCAN_MS, 一次抖动通常 <10ms).
 * 引脚避开 SWD/JTAG/串口/软件I2C/DHT11/蜂鸣器; 换脚只改下面 4 行 + KEY_PORT. */
#define KEY_PORT        GPIOA
#define KEY1_PIN        GPIO_PIN_0      /* 切页 */
#define KEY2_PIN        GPIO_PIN_1      /* 上移 / 加 / 播报 */
#define KEY3_PIN        GPIO_PIN_4      /* 下移 / 减 */
#define KEY4_PIN        GPIO_PIN_5      /* 确认 */

#define KEY_ALL_PINS    (KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN)

/* 按键编号 */
#define KEY_ID_1        0U
#define KEY_ID_2        1U
#define KEY_ID_3        2U
#define KEY_ID_4        3U
#define KEY_ID_NUM      4U

/* 扫描周期, 同时也是消抖时间 */
#define KEY_SCAN_MS     20U

/* 按住超过此值算长按 (只报一次) */
#define KEY_LONG_MS     1000U

/* 一次扫描的结果: 按了哪个键 + 是什么动作 */
typedef enum {
    KEY_EVENT_NONE = 0,     /* 没动作 */
    KEY_EVENT_PRESS,        /* 按下 (消抖确认后立刻报, 一次按住只报一次) */
    KEY_EVENT_LONG,         /* 按住满 KEY_LONG_MS 报一次 */
    KEY_EVENT_RELEASE       /* 松手 (想"松手才动作"的场合用) */
} Key_Event;

/* 配 4 个脚为输入+上拉, App_Init() 里调一次 */
void Key_Init(void);

/* 扫一次, 每 KEY_SCAN_MS 调一次; 传 KEY_ID_1 ~ KEY_ID_4 */
Key_Event Key_Scan(uint8_t id);

/* 按下返回 1~4, 没动作返回 0 (内部调 Key_Scan) */
uint8_t Key_GetNum(uint8_t id);

#endif /* __KEY_H */
