#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"
#include <stdint.h>

/* 数据引脚: 接带 4.7k~10k 上拉电阻的 DHT11 模块 */
#define DHT11_PORT      GPIOB
#define DHT11_PIN       GPIO_PIN_12
#define DHT11_PIN_NUM   12      /* 必须和 DHT11_PIN 对应: 驱动用它算 CRL/CRH 里的位置 */

typedef struct {
    uint8_t humidity;           /* 相对湿度 %RH, 整数部分 */
    uint8_t temperature;        /* 温度 ℃, 整数部分 */
} DHT11_Data;

/* 必须在 MX_GPIO_Init() 之后调用 (依赖 GPIOB 时钟) */
uint8_t DHT11_Init(void);

/* 读一次温湿度, 返回 1 表示成功且校验通过; 两次读取间隔需 >= 1s */
uint8_t DHT11_Read(DHT11_Data *data);

#endif /* __DHT11_H */
