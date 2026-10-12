#ifndef __STUB_MAIN_H
#define __STUB_MAIN_H

/* 桩: 只补 CubeMX/诊断环境里缺的那几个句柄类型。
 *
 * ⚠️ 这里**故意不定义** GPIOA/GPIOB/GPIOC、GPIO_PIN_x、GPIO_MODE_*、
 *    GPIO_SPEED_*、__HAL_RCC_xxx_CLK_ENABLE、HAL_GPIO_WritePin 这些 ——
 *    它们由 Core/Inc/main.h -> stm32f1xx_hal.h 提供, 是 CubeMX 的地盘。
 *    在这儿再定义一遍就是和 CubeMX 打架(重定义 / 类型不一致),
 *    所以引脚和 HAL 一律走真实头文件, 本桩只补句柄类型。 */

#include "stm32f1xx_hal.h"

typedef struct { void *Instance; } UART_HandleTypeDef;
typedef struct { void *Instance; } I2C_HandleTypeDef;

void Error_Handler(void);

/* 真实 main.h 里 MX_xxx_Init 的声明, 本桩用不到但保持一致 */
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_I2C1_Init(void);
void MX_TIM2_Init(void);
void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);

#endif /* __STUB_MAIN_H */
