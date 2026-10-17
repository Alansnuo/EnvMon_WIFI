/* ============================================================================
 * PWM 调光: TIM1_CH1 (PA8) 硬件输出, 零 CPU 开销
 * LED 阳极接 3.3V、阴极接 PA8 → 低电平点亮, 故 CCR 与亮度成反比
 * ========================================================================== */

#include "pwm.h"
#include "tim.h"

/* 启动 PWM 输出并先置灭 */
void PWM_Init(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    PWM_SetBright(0U);
}

/**
 * @brief 设置亮度.
 * @param bright 0~PWM_MAX_BRIGHT, 越大越亮.
 */
void PWM_SetBright(uint16_t bright)
{
    if (bright > PWM_MAX_BRIGHT) { bright = PWM_MAX_BRIGHT; }

    /* 低电平点亮: 亮度越高, 高电平占空比(CCR)越小 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, PWM_MAX_BRIGHT - bright);
}
