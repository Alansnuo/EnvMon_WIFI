/* 天问语音模块 (ASRPRO) —— 字符串收行 + 发送.
 * 收: 中断逐字节攒到 Asr_Line, 遇 \r/\n 置 Asr_LineReady (中断只攒不算).
 * 发: 阻塞式 HAL_UART_Transmit, 每条仅十几字节. */

#include "asr.h"
#include <string.h>
#include <stdio.h>

volatile char    Asr_Line[ASR_LINE_MAX];
volatile uint8_t Asr_LineLen   = 0U;
volatile uint8_t Asr_LineReady = 0U;

/* 按键任务置 1, 语音任务播报完清 0 */
volatile uint8_t g_xAsrSpeakReq = 0U;

/* USART1 专职本模块, 工程已无 printf 重定向, 不会有调试文本混入数据帧 */

/* 逐字节收行. Len/Ready 在中断与任务间共享: 任务取走后再清, 中断仅在 Ready==0 时追加 */
void Asr_RxByteFromISR(uint8_t byte)
{
    if (byte == (uint8_t)'\r' || byte == (uint8_t)'\n')
    {
        if (Asr_LineLen > 0U)
        {
            Asr_Line[Asr_LineLen] = '\0';       /* 补字符串结尾, 方便 strcmp */
            Asr_LineReady = 1U;
        }
        return;                                 /* 收尾字符本身不入缓冲 */
    }

    if (Asr_LineReady != 0U)
    {
        return;                                 /* 上一行还没被任务取走, 丢弃 */
    }

    if (Asr_LineLen < (ASR_LINE_MAX - 1U))
    {
        Asr_Line[Asr_LineLen] = (char)byte;
        Asr_LineLen++;
    }
    else
    {
        Asr_LineLen = 0U;                       /* 超长: 直接丢掉重来, 防错位 */
    }
}

void Asr_Init(void)
{
    /* 直接开 RXNE 中断, 中断里读 DR; 不用 HAL_UART_Receive_IT (需每字节重挂) */
    __HAL_UART_ENABLE_IT(&ASR_UART, UART_IT_RXNE);
}

/* ------------------------------- 发送 ------------------------------- */

static void Asr_Write(const char *s)
{
    /* 阻塞轮询, 每条十几字节, 窗口很短 */
    (void)HAL_UART_Transmit(&ASR_UART, (uint8_t *)s, (uint16_t)strlen(s), 100U);
}

void Asr_SendString(const char *s)
{
    Asr_Write(s);
    Asr_Write("\r\n");
}

void Asr_SendEnv(int temp, int humi, uint8_t light)
{
    char buf[24];
    /* 格式: "0 <温度> <湿度> <光线等级>" */
    (void)snprintf(buf, sizeof(buf), "%u %d %d %u\r\n",
                   (unsigned)ASR_ACT_ENV, temp, humi, (unsigned)light);
    Asr_Write(buf);
}

void Asr_SendAlarm(uint8_t code)
{
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%u %u\r\n",
                   (unsigned)ASR_ACT_ALARM, (unsigned)code);
    Asr_Write(buf);
}

void Asr_SendNum(int value)
{
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%u %d\r\n",
                   (unsigned)ASR_ACT_NUM, value);
    Asr_Write(buf);
}
