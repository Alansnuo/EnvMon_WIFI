#ifndef __BH1750_H
#define __BH1750_H

#include "main.h"
#include <stdint.h>

/* 7 位地址 0x23 */
#define BH1750_ADDR_W    0x46 // 写； 0x23 << 1 
#define BH1750_ADDR_R    0x47 // 读； 0x23 << 1 | 1

void BH1750_Init(void);

/* 读一次照度 (lx). 成功返回 1, 故障返回 0;  */
/* 输出照度: lux = 原始计数值 / 1.2, 量程 0~54612 lx, 分辨率 1 lx. */
uint8_t BH1750_ReadLux(float *lux);

#endif /* __BH1750_H */
