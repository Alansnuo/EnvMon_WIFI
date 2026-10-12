/* ============================================================================
 * 天问语音模块 (ASRPRO) —— 字符串收行 + 发送
 *
 * 收: 中断里逐字节攒到 Asr_Line, 遇到 \r 或 \n 就置 Asr_LineReady。
 *     只攒不算, 动作全留给 APP/task_asr.c —— 中断要尽量短。
 * 发: 顺序阻塞的 HAL_UART_Transmit。每条就十几个字节, 阻塞时间很短。
 * ========================================================================== */

#include "asr.h"
#include <string.h>
#include <stdio.h>

volatile char    Asr_Line[ASR_LINE_MAX];
volatile uint8_t Asr_LineLen   = 0U;
volatile uint8_t Asr_LineReady = 0U;

/* 按键任务置 1, 语音任务播报完清 0 */
volatile uint8_t g_xAsrSpeakReq = 0U;

/* USART1 现在专职本模块: 工程里已没有 printf 重定向, 不存在调试文本混进
 * 数据帧的问题 (为什么要这样, 见 asr.h 的头部说明)。 */

/* 逐字节收行。
 * ⚠️ Asr_LineLen/Asr_LineReady 在中断和任务之间共享, 配合规则:
 *    任务看到 Asr_LineReady==1 后, 先把内容拷走、再清 Len 和 Ready;
 *    中断只在 Ready==0 时才往缓冲里追加。这样不会撕裂。 */
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
    /* 直接开 RXNE 中断, 中断里直接读 DR (04 篇的"做法 A")。
     * 不用 HAL_UART_Receive_IT: 那个是单次的, 必须每收一个字节重挂一次。 */
    __HAL_UART_ENABLE_IT(&ASR_UART, UART_IT_RXNE);
}

/* ------------------------------- 发送 ------------------------------- */

static void Asr_Write(const char *s)
{
    /* HAL_UART_Transmit 是阻塞轮询, 每条就十几个字节, 窗口很短,
     * 中途不会主动让出 CPU。 */
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
    /* 格式: "0 <温度> <湿度> <光线等级>" —— 模块端用 parseInt 连续取数 */
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
