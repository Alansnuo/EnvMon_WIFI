#ifndef __ASR_H
#define __ASR_H

#include "main.h"
#include <stdint.h>

/* ============================================================================
 * 天问语音模块 (ASRPRO) —— **字符串**协议
 *
 * 模块接在 USART1 (PA9/PA10), **这一路串口专职语音**:
 * 工程里已经没有 printf 重定向 (fputc 已删), USART1 上只有本模块的数据帧。
 *
 * ⚠️ 模块收字符串是"按时间间隔判断一帧结束"的。若以后想恢复调试打印,
 *    请把 printf 重定向到别的串口(如 USART3 PB10/PB11), 不要回到 USART1 ——
 *    调试文本会和真正的数据帧糊在一起; 更糟的是不含数字的文本会让模块端
 *    parseInt 超时返回 0, 而 0 正好是"播报环境"的动作码。
 *
 * 协议:
 *   MCU → 模块   "<动作码> <数值...>\r\n"
 *                  0 <温度> <湿度> <光线等级>    播报环境
 *                  1 <告警码>                    语音告警
 *                  2 <数值>                      只播报一个数字(语音问答)
 *   模块 → MCU   "TEMP\r\n" / "HUMI\r\n" / "LUX\r\n"   请求某一项数据
 *
 * ⚠️ 调试助手的 TX **绝对不能**接到 PA10: 那根线上已经有模块的 TX 在驱动,
 *    两个输出顶在一起会互相干扰(严重时可损坏 IO)。调试助手只能单向"听"。
 * ========================================================================== */

extern UART_HandleTypeDef huart1;           /* 定义在 Core/Src/usart.c */
#define ASR_UART        huart1
#define ASR_UART_IRQn   USART1_IRQn

/* 模块发来的请求字符串 */
#define ASR_REQ_TEMP    "TEMP"
#define ASR_REQ_HUMI    "HUMI"
#define ASR_REQ_LUX     "LUX"

/* 动作码 */
#define ASR_ACT_ENV     0U          /* 播报环境数据 */
#define ASR_ACT_ALARM   1U          /* 播报语音告警 */
#define ASR_ACT_NUM     2U          /* 只播报一个数字 */

/* 告警码 —— 必须和模块端积木里的 case 一一对应 */
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

/* 打开 USART1 的 RXNE 接收中断。在 App_Init() 里调一次 */
void Asr_Init(void);

/* 逐字节喂给收行器。**只允许在 USART1 中断里调** */
void Asr_RxByteFromISR(uint8_t byte);

/* ============================ STM32 → 模块 ============================ */
void Asr_SendString(const char *s);                 /* 原样发一段 + 换行 */
void Asr_SendEnv(int temp, int humi, uint8_t light);/* 动作码 0 */
void Asr_SendAlarm(uint8_t code);                   /* 动作码 1 */
void Asr_SendNum(int value);                        /* 动作码 2 */

#endif /* __ASR_H */
