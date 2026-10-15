#ifndef __DS1302_H__
#define __DS1302_H__

#include "main.h"

/* DS1302 三线制 (CE/SCLK/IO), 非 I2C; 引脚由 CubeMX 配置. */

/* 引脚: 改线只改这里 */
#define DS1302_CE_PORT    GPIOA
#define DS1302_CE_PIN     GPIO_PIN_7
#define DS1302_SCLK_PORT  GPIOB
#define DS1302_SCLK_PIN   GPIO_PIN_1
#define DS1302_IO_PORT    GPIOB
#define DS1302_IO_PIN     GPIO_PIN_0

/* 时间数组: 0年 1月 2日 3时 4分 5秒 6星期 */
extern unsigned char DS1302_Time[];

void DS1302_Init(void);
void DS1302_WriteByte(unsigned char Command, unsigned char Data);
unsigned char DS1302_ReadByte(unsigned char Command);
void DS1302_SetTime(void);
void DS1302_ReadTime(void);

#endif
