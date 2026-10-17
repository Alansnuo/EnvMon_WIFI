#ifndef __OLED_H
#define __OLED_H

#include "main.h"
#include <stdint.h>

/* OLED (SSD1306 128x64) 软件 I2C 驱动; 开漏, 单向推数据不应答.
 * 连线: PB8=SCL, PB9=SDA */

#define OLED_SCL_PORT    GPIOB
#define OLED_SCL_PIN     GPIO_PIN_8
#define OLED_SDA_PORT    GPIOB
#define OLED_SDA_PIN     GPIO_PIN_9

/* 7 位 0x3C << 1 = 0x78; SA0 接 VCC 时为 0x7A */
#define OLED_ADDR_W      0x78

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif /* __OLED_H */
