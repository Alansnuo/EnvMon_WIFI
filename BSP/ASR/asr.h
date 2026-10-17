#ifndef __ASR_H
#define __ASR_H

#include "main.h"
#include <stdint.h>

/* 天问语音模块 (ASRPRO) —— 字符串协议, 接在 USART1 (PA9/PA10), 专职语音.
 * 协议:
 *   MCU → 模块  "<动作码> <数值...>\r\n"
 *               3 <温度> <湿度> <光线等级>  播报环境
 *               1 <告警码>  语音告警
 *               2 <数值>    只播报一个数字
 *   模块 → MCU  "TEMP\r\n" / "HUMI\r\n" / "LUX\r\n"  请求某项数据
 * USART1 不可恢复 printf; 调试助手 TX 不可接 PA10 (与模块 TX 冲突). */

extern UART_HandleTypeDef huart1;           /* 定义在 Core/Src/usart.c */
#define ASR_UART        huart1
#define ASR_UART_IRQn   USART1_IRQn

/* 模块发来的请求字符串 */
#define ASR_REQ_TEMP    "TEMP"
#define ASR_REQ_HUMI    "HUMI"
#define ASR_REQ_LUX     "LUX"

/* 动作码. ENV 不能用 0: 模块 parseInt 超时返回 0, 会误触发播报 */
#define ASR_ACT_ENV     3U          /* 播报环境数据 */
#define ASR_ACT_ALARM   1U          /* 播报语音告警 */
#define ASR_ACT_NUM     2U          /* 只播报一个数字 */

/* 告警码, 须与模块端 case 一一对应 */
#define ASR_ALARM_TEMP_HI   1U      /* 温度过高 */
#define ASR_ALARM_TEMP_LO   2U      /* 温度过低 */
#define ASR_ALARM_HUMI_HI   3U      /* 湿度过高 */
#define ASR_ALARM_HUMI_LO   4U      /* 湿度过低 */
#define ASR_ALARM_LUX_LO    5U      /* 光照不足 */
#define ASR_ALARM_LUX_HI    6U      /* 光照过强 */
#define ASR_ALARM_OK        7U      /* 已恢复正常 */

/* 光线等级 (发给模块, 决定播报"光线充足/偏暗/很暗") */
#define ASR_LIGHT_DARK      0U
#define ASR_LIGHT_OK        1U
#define ASR_LIGHT_STRONG    2U

/* 收到一整行(遇到 \r 或 \n 收尾) */
#define ASR_LINE_MAX    24
extern volatile char    Asr_Line[ASR_LINE_MAX];
extern volatile uint8_t Asr_LineLen;
extern volatile uint8_t Asr_LineReady;      /* 中断置 1, 任务取走后清 0 */

/* 按键触发的立刻播报请求: 按键任务置 1, 语音任务取走清 0 */
extern volatile uint8_t g_xAsrSpeakReq;

/* 打开 USART1 的 RXNE 接收中断, App_Init() 里调一次 */
void Asr_Init(void);

/* 逐字节喂给收行器, 只允许在 USART1 中断里调 */
void Asr_RxByteFromISR(uint8_t byte);

/* ============================ STM32 → 模块 ============================ */
void Asr_SendString(const char *s);                 /* 原样发一段 + 换行 */
void Asr_SendEnv(int temp, int humi, uint8_t light);/* 播报环境 (动作码 3) */
void Asr_SendAlarm(uint8_t code);                   /* 语音告警 (动作码 1) */
void Asr_SendNum(int value);                        /* 只播报数字 (动作码 2) */

#endif /* __ASR_H */
