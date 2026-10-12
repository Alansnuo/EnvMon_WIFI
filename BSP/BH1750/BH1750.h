#ifndef __BH1750_H
#define __BH1750_H

#include "main.h"
#include <stdint.h>

/* BH1750 数字光照传感器 (GY-302 小板) 软件 I2C 驱动.
 * 直接输出照度: lux = 原始计数值 / 1.2, 量程 0~54612 lx, 分辨率 1 lx.
 * GY-302 自带 4.7k 上拉, 不用外加. */

/* 对应 PCB 上 U6: U6.3 → PB6(SCL), U6.4 → PB7(SDA).
 * 参考工程用 PB4/PB5. PB6/PB7 不是 JTAG 脚, 所以不需要 JTAG 重映射. */
#define BH1750_SCL_PORT  GPIOB
#define BH1750_SCL_PIN   GPIO_PIN_6
#define BH1750_SDA_PORT  GPIOB
#define BH1750_SDA_PIN   GPIO_PIN_7

/* 7 位地址 0x23 左移一位 + 写方向. 读操作 = BH1750_ADDR_W | 0x01.
 * GY-302 的 ADDR 脚接地, 所以地址就是 0x23. */
#define BH1750_ADDR_W    0x46

/* 必须在 MX_GPIO_Init() 之后调用. 内部有 BSP_DelayMs, 调度器启动前调用也安全. */
void BH1750_Init(void);

/* 读一次照度 (lx).
 * 不报错: 总线断线时读回全 1, 返回 54612 (或全 0). 读数恒定 54612/0 时先查线. */
float BH1750_ReadLux(void);

/* 带成功标志的版本: 成功返回 1, 总线故障返回 0; *lux 两种情况都会写入.
 * 判据: 命令和两个数据字节都被应答, 且 raw != 0xFFFF.
 * (raw == 0 不算失败 —— 全黑环境就是 0 lx.) */
uint8_t BH1750_ReadLuxChecked(float *lux);

#endif /* __BH1750_H */
