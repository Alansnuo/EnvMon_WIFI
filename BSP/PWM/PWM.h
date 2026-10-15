#ifndef __PWM_H
#define __PWM_H

#include "main.h"
#include <stdint.h>

/**
 * @brief 硬件 PWM 调光 (TIM1_CH1 → PA8).
 * @note  LED 阳极接 3.3V、阴极接 PA8, 拉低点亮; 亮度 = 低电平占空比.
 */
#define PWM_MAX_BRIGHT   1000U          /* 满亮度档位 (TIM1 ARR=999, 周期 1000) */

void PWM_Init(void);                    /* 启动 TIM1_CH1, 须在 MX_TIM1_Init() 后调用 */
void PWM_SetBright(uint16_t bright);    /* 0=全灭, PWM_MAX_BRIGHT=全亮 */

#endif /* __PWM_H */
