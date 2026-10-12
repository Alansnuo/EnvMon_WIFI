#ifndef __STUB_DHT11_H
#define __STUB_DHT11_H
#include "main.h"
#include <stdint.h>
typedef struct {
    uint8_t humidity;
    uint8_t humidity_dec;
    uint8_t temperature;
    uint8_t temperature_dec;
} DHT11_Data;
uint8_t DHT11_Init(void);
uint8_t DHT11_Read(DHT11_Data *data);
#endif
