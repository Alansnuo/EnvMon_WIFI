#ifndef __MYI2C_H
#define __MYI2C_H

#include "main.h"
#include <stdint.h>

/* 通用软件 I2C 总线 (STM32F1 + HAL).
 * 不用硬件 I2C: F1 的硬件 I2C 有死锁 errata, 用两个 GPIO 打时序更稳.
 * 两个 IO 都配开漏: 写 1 = 放开总线(靠模块上的 4.7k 拉高), 写 0 = 拉低.
 *
 * 引脚 PB6(SCL)/PB7(SDA) 由 CubeMX 配置(开漏输出, 初始高电平), 本模块不再重配.
 * 使用前确保 MX_GPIO_Init() 已执行. */

/* 总线引脚. 改线只改这里. */
#define MYI2C_SCL_PORT   GPIOB
#define MYI2C_SCL_PIN    GPIO_PIN_6
#define MYI2C_SDA_PORT   GPIOB
#define MYI2C_SDA_PIN    GPIO_PIN_7

/* 起始: SCL 高时 SDA 下降沿 */
void MyI2C_Start(void);

/* 停止: SCL 高时 SDA 上升沿 */
void MyI2C_Stop(void);

/* 发一字节. 返回从机应答: 0 = ACK, 1 = NACK */
uint8_t MyI2C_SendByte(uint8_t dat);

/* 收一字节. sendAck: 1 = 主机应答, 0 = 非应答(末字节) */
uint8_t MyI2C_RecvByte(uint8_t sendAck);

#endif /* __MYI2C_H */
