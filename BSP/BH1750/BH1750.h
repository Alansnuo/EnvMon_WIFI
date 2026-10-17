#ifndef __BH1750_H
#define __BH1750_H

#include "main.h"
#include <stdint.h>

/* BH1750 数字光照传感器 (GY-302 小板) 软件 I2C 驱动.
 * 直接输出照度: lux = 原始计数值 / 1.2, 量程 0~54612 lx, 分辨率 1 lx.
 * GY-302 自带 4.7k 上拉, 不用外加. */

/* 总线引脚已抽到 BSP/MyI2C (PB6=SCL, PB7=SDA), 本驱动只保留器件地址 */

/* 7 位地址 0x23 左移一位 + 写方向; 读 = BH1750_ADDR_W | 0x01 */
#define BH1750_ADDR_W    0x46

void BH1750_Init(void);

/* 读一次照度 (lx). 不报错: 断线时返回 54612 或 0 */
float BH1750_ReadLux(void);

/* 带成功标志: 成功返回 1, 故障返回 0; *lux 两种情况都写入 */
uint8_t BH1750_ReadLuxChecked(float *lux);

#endif /* __BH1750_H */
