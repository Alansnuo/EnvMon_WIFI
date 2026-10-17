#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"
#include <stdint.h>


#define DHT11_PORT      GPIOB
#define DHT11_PIN       GPIO_PIN_12

typedef struct {
    uint8_t humidity;           /* 湿度 %RH */
    uint8_t temperature;        /* 温度 ℃ */
} DHT11_Data;

uint8_t DHT11_Init(void);

/* 读一次温湿度, 成功返回 1; 间隔需 >= 1s */
uint8_t DHT11_Read(DHT11_Data *data);

#endif /* __DHT11_H */
