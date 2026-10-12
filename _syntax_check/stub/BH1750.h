#ifndef __STUB_BH1750_H
#define __STUB_BH1750_H
#include "main.h"
#include <stdint.h>
uint8_t BH1750_Init(void);
uint8_t BH1750_ReadLuxChecked(float *lux);
#endif
