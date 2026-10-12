#ifndef __STUB_STM32F1XX_HAL_H
#define __STUB_STM32F1XX_HAL_H

/* 桩: 模拟 STM32F1 的 HAL 头, 只保留源码里真正用到的部分。
 *
 * 寄存器名字和偏移按参考手册来 (BSRR 在 0x10, IDR 在 0x08), 这样 dht11.c
 * 里那种"直接操作 BSRR/IDR/CRL/CRH"的裸寄存器代码也能编译过。
 * ⚠️ 这是**验证用**的桩, 不进固件, 也不进 Keil 工程。 */

#include <stdint.h>

/* ---------------- 寄存器定义 ---------------- */
typedef struct {
    volatile uint32_t CRL;      /* 0x00 */
    volatile uint32_t CRH;      /* 0x04 */
    volatile uint32_t IDR;      /* 0x08 */
    volatile uint32_t ODR;      /* 0x0C */
    volatile uint32_t BSRR;     /* 0x10 */
    volatile uint32_t BRR;      /* 0x14 */
    volatile uint32_t LCKR;     /* 0x18 */
} GPIO_TypeDef;

/* 端口基地址: A=0x40010800, B=0x40010C00, C=0x40011000 (和 CubeMX 一样) */
#define GPIOA   ((GPIO_TypeDef *)0x40010800UL)
#define GPIOB   ((GPIO_TypeDef *)0x40010C00UL)
#define GPIOC   ((GPIO_TypeDef *)0x40011000UL)
#define GPIOD   ((GPIO_TypeDef *)0x40011400UL)

/* ---------------- 引脚 / 模式 ---------------- */
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

#define GPIO_PIN_0    ((uint16_t)0x0001)
#define GPIO_PIN_1    ((uint16_t)0x0002)
#define GPIO_PIN_2    ((uint16_t)0x0004)
#define GPIO_PIN_3    ((uint16_t)0x0008)
#define GPIO_PIN_4    ((uint16_t)0x0010)
#define GPIO_PIN_5    ((uint16_t)0x0020)
#define GPIO_PIN_6    ((uint16_t)0x0040)
#define GPIO_PIN_7    ((uint16_t)0x0080)
#define GPIO_PIN_8    ((uint16_t)0x0100)
#define GPIO_PIN_9    ((uint16_t)0x0200)
#define GPIO_PIN_10   ((uint16_t)0x0400)
#define GPIO_PIN_11   ((uint16_t)0x0800)
#define GPIO_PIN_12   ((uint16_t)0x1000)
#define GPIO_PIN_13   ((uint16_t)0x2000)
#define GPIO_PIN_14   ((uint16_t)0x4000)
#define GPIO_PIN_15   ((uint16_t)0x8000)

#define GPIO_MODE_INPUT      0x00000000u
#define GPIO_MODE_OUTPUT_PP  0x00000001u
#define GPIO_MODE_OUTPUT_OD  0x00000011u
#define GPIO_MODE_AF_PP      0x00000002u
#define GPIO_MODE_AF_OD      0x00000012u
#define GPIO_MODE_ANALOG     0x00000003u

#define GPIO_NOPULL        0x00000000u
#define GPIO_PULLUP        0x00000001u
#define GPIO_PULLDOWN      0x00000002u

#define GPIO_SPEED_FREQ_LOW       0x00000002u
#define GPIO_SPEED_FREQ_MEDIUM    0x00000001u
#define GPIO_SPEED_FREQ_HIGH      0x00000003u

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

/* ---------------- 状态码 ---------------- */
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

/* ---------------- 时钟使能宏 ---------------- */
#define __HAL_RCC_GPIOA_CLK_ENABLE()  do {} while (0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()  do {} while (0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()  do {} while (0)
#define __HAL_RCC_GPIOD_CLK_ENABLE()  do {} while (0)
#define __HAL_RCC_AFIO_CLK_ENABLE()   do {} while (0)
#define __HAL_RCC_TIM2_CLK_ENABLE()   do {} while (0)
#define __HAL_RCC_USART1_CLK_ENABLE() do {} while (0)
#define __HAL_RCC_USART2_CLK_ENABLE() do {} while (0)
#define __HAL_RCC_DMA1_CLK_ENABLE()   do {} while (0)

/* ---------------- HAL 函数声明 (只声明, 不实现) ---------------- */
void          HAL_Init(void);
void          HAL_Delay(uint32_t Delay);
uint32_t      HAL_GetTick(void);
void          HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
void          HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void          HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

void          HAL_NVIC_SetPriority(int IRQn, uint32_t PreemptPriority, uint32_t SubPriority);
void          HAL_NVIC_EnableIRQ(int IRQn);

#endif /* __STUB_STM32F1XX_HAL_H */
