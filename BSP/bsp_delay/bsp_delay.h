#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include <stdint.h>

/* 毫秒级延时 (所有驱动共用). 弱定义在 bsp_delay.c, 强定义在 app_delay.c,
 * 链接器优先采用强定义 (裸机走 HAL_Delay, 上 RTOS 走 vTaskDelay). */

/* 延时 ms 毫秒 */
void BSP_DelayMs(uint32_t ms);

/* 延时 us 微秒 (忙等, TIM2 计数) */
void BSP_DelayUs(uint32_t us);

/* 读当前微秒计时值 (TIM2 16 位, 每 65536us 回绕) */
uint16_t BSP_GetUs(void);

#endif /* __BSP_DELAY_H */
