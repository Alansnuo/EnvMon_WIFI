#ifndef __KEY_H
#define __KEY_H

#include "main.h"
#include <stdint.h>

/* ============================================================================
 * 4 个独立按键 —— 扫描式读取, 不用中断
 *
 * 写法参照 D:\Download\kile5\Project\C51\电子密码锁 的 BSP/Key/MatrixKey.c:
 * 直接读引脚、直接比较、返回一个"按了哪个键"的编号, **不用结构体数组、
 * 不用指针、不做多层状态机**。区别只有两处, 下面说清楚。
 *
 * ---- 一、全部用内部上拉 ----
 *    松手 = 高电平, 按下 = 接地 = 低电平。所以外面不用接电阻,
 *    只要"按键 + 两根线": 一脚接 IO, 一脚接 GND。
 *
 * ---- 二、和参考工程唯一必须不同的地方: 不能阻塞等松手 ----
 *    参考工程是裸机, 主循环里可以直接
 *        if (KEY == 0) { Delay(20); while (KEY == 0); Delay(20); return 键号; }
 *    也就是"按下 -> 消抖 -> 卡住等松手 -> 才返回"。
 *
 *    ⚠️ 这里**不能**那么写: 本工程跑 FreeRTOS, 等松手等于把 CPU 让出去
 *       (甚至被人按住不放就永远不返回), 按键任务以外的活全停。
 *    所以改成: **消抖确认按下就立刻返回一次, 然后记住"这个键还没松手",
 *               松手之前不再重复报**。效果和参考工程一样(按一下报一次),
 *               但不阻塞。
 *
 * ---- 三、扫描周期就是消抖时间 ----
 *    每 KEY_SCAN_MS 调一次 Key_Scan(), 连续两次读到同一个电平才认。
 *    一次抖动通常 <10ms, 20ms 够。
 *
 * ⚠️ 引脚特意避开的: PA13/PA14(SWD 调试口), PA15/PB3/PB4(JTAG 复用),
 *    PB2(BOOT1), PB6~PB9(两路软件 I2C), PA2/3/9/10(两路串口),
 *    PB12(DHT11), PB13(蜂鸣器), PC13(备用). 换引脚只改下面 4 行 + KEY_PORT。 */
#define KEY_PORT        GPIOA
#define KEY1_PIN        GPIO_PIN_0      /* 切页 */
#define KEY2_PIN        GPIO_PIN_1      /* 上移 / 加 / 播报 */
#define KEY3_PIN        GPIO_PIN_4      /* 下移 / 减 */
#define KEY4_PIN        GPIO_PIN_5      /* 确认 */

#define KEY_ALL_PINS    (KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN)

/* 按键编号。想用"哪个键"的语义就用这几个宏, 不要在别处写裸数字 */
#define KEY_ID_1        0U
#define KEY_ID_2        1U
#define KEY_ID_3        2U
#define KEY_ID_4        3U
#define KEY_ID_NUM      4U

/* 扫描周期, 同时也是消抖时间 */
#define KEY_SCAN_MS     20U

/* 按住超过这么多毫秒算长按 (只在按住期间报一次, 松手不再报短按) */
#define KEY_LONG_MS     1000U

/* 一次扫描的结果: 按了哪个键 + 是什么动作 */
typedef enum {
    KEY_EVENT_NONE = 0,     /* 没动作 */
    KEY_EVENT_PRESS,        /* 按下 (消抖确认后立刻报, 一次按住只报一次) */
    KEY_EVENT_LONG,         /* 按住满 KEY_LONG_MS 报一次 */
    KEY_EVENT_RELEASE       /* 松手 (想"松手才动作"的场合用) */
} Key_Event;

/* 配好 4 个脚为"输入 + 上拉"。在 App_Init() 里调一次 */
void Key_Init(void);

/* 扫一次。每 KEY_SCAN_MS 调一次; 4 个键各自独立调用, 互不影响。
 * 传 KEY_ID_1 ~ KEY_ID_4 */
Key_Event Key_Scan(uint8_t id);

/* 参考工程那种"直接拿编号"的用法, 内部就是 Key_Scan:
 *      按下返回 1~4, 没动作返回 0
 * 不想管事件类型时用这个, 读起来和 MatrixKey() 一样直白 */
uint8_t Key_GetNum(uint8_t id);

#endif /* __KEY_H */
