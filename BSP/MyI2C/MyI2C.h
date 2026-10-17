#ifndef __MYI2C_H
#define __MYI2C_H

#include "main.h"
#include <stdint.h>

/* 通用软件 I2C 总线 (STM32F1 + HAL).
 * 不用硬件 I2C (F1 有死锁 errata); 两个 IO 开漏, 引脚 PB6(SCL)/PB7(SDA)
 * 由 CubeMX 配置(开漏, 初始高), 本模块不再重配. */

/* 总线引脚, 改线只改这里 */
#define MYI2C_SCL_PORT   GPIOB
#define MYI2C_SCL_PIN    GPIO_PIN_6
#define MYI2C_SDA_PORT   GPIOB
#define MYI2C_SDA_PIN    GPIO_PIN_7

/* 产生 I2C 起始条件 (SCL 高时 SDA 下降沿) */
void MyI2C_Start(void);

/* 产生 I2C 停止条件 (SCL 高时 SDA 上升沿) */
void MyI2C_Stop(void);

/* 发送一字节, 返回 0=ACK, 1=NACK */
uint8_t MyI2C_SendByte(uint8_t dat);

/* 接收一字节; sendAck: 1=应答, 0=非应答 */
uint8_t MyI2C_RecvByte(uint8_t sendAck);

#endif /* __MYI2C_H */
