#ifndef __STUB_ASR_H
#define __STUB_ASR_H
#include "main.h"
#include <stdint.h>
extern UART_HandleTypeDef huart1;
#define ASR_REQ_TEMP "TEMP"
#define ASR_REQ_HUMI "HUMI"
#define ASR_REQ_LUX  "LUX"
#define ASR_ACT_ENV   0U
#define ASR_ACT_ALARM 1U
#define ASR_ACT_NUM   2U
#define ASR_ALARM_TEMP_HI 1U
#define ASR_ALARM_TEMP_LO 2U
#define ASR_ALARM_HUMI_HI 3U
#define ASR_ALARM_HUMI_LO 4U
#define ASR_ALARM_LUX_LO  5U
#define ASR_ALARM_LUX_HI  6U
#define ASR_ALARM_OK      7U
#define ASR_LIGHT_DARK   0U
#define ASR_LIGHT_OK     1U
#define ASR_LIGHT_STRONG 2U
#define ASR_LINE_MAX 24
extern volatile char    Asr_Line[ASR_LINE_MAX];
extern volatile uint8_t Asr_LineLen;
extern volatile uint8_t Asr_LineReady;
extern volatile uint8_t g_xAsrSpeakReq;
void Asr_Init(void);
void Asr_SendString(const char *s);
void Asr_SendEnv(int temp, int humi, uint8_t light);
void Asr_SendAlarm(uint8_t code);
void Asr_SendNum(int value);
#endif
