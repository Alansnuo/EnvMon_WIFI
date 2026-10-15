#ifndef __DS1302_H__
#define __DS1302_H__

#include "main.h"
/* DS1302 实时时钟 —— 三线制 (CE / SCLK / IO), 不是 I2C.
 * 引脚 PA7=CE, PB1=SCLK, PB0=IO, 由 CubeMX 配置, 驱动内只按读写需要切 IO 方向. */

/* 引脚定义. 改线只改这里. */
#define DS1302_CE_PORT    GPIOA
#define DS1302_CE_PIN     GPIO_PIN_7
#define DS1302_SCLK_PORT  GPIOB
#define DS1302_SCLK_PIN   GPIO_PIN_1
#define DS1302_IO_PORT    GPIOB
#define DS1302_IO_PIN     GPIO_PIN_0

//外部可调用时间数组，索引0~6分别为年、月、日、时、分、秒、星期
extern unsigned char DS1302_Time[];

void DS1302_Init(void);
void DS1302_WriteByte(unsigned char Command, unsigned char Data);
unsigned char DS1302_ReadByte(unsigned char Command);
void DS1302_SetTime(void);
void DS1302_ReadTime(void);

#endif
