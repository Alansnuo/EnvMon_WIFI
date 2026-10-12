/* ============================================================================
 * 天问语音模块 (ASRPRO) 帧解析
 *
 * 帧格式: 0x55 0xAA 0x02 cmd data xor     校验 = 前 5 字节逐个异或
 *
 * 三状态机:
 *   状态 0: 收到 0x55 → 存下, 进状态 1   (收到别的 → 留在状态 0)
 *   状态 1: 收到 0xAA → 存下, 进状态 2   (收到别的 → 回状态 0, 重新找帧头)
 *   状态 2: 任意字节都存下, 计数到 6 就校验
 *           校验通过 → 置 Asr_RxFlag; 无论对错都回状态 0
 *
 * 为什么用状态机而不是"收满 6 个再检查帧头": 中途掉一个字节时, 状态机会自动
 * 回到状态 0 重新找 0x55, **不会永久错位**. 后者一旦错位就再也对不上了.
 *
 * 中断里只收不算, 动作全部留给 APP/task_asr.c —— 中断要尽量短.
 * ========================================================================== */

#include "asr.h"

volatile uint8_t  Asr_RxFlag = 0U;
uint8_t           Asr_RxPacket[ASR_FRAME_LEN];
volatile uint32_t Asr_RxByteCount = 0U;     /* 【诊断用, 查完可删】 */

/* 前 len 个字节逐个异或 */
static uint8_t Asr_CalXor(const uint8_t *data, uint32_t len)
{
    uint8_t  sum = 0U;
    uint32_t i;

    for (i = 0U; i < len; i++)
    {
        sum ^= data[i];
    }
    return sum;
}

/* 逐字节状态机.
 * ⚠️ 那两个 static 变量在中断上下文里是安全的 —— USART1 的中断不可能重入
 *    (同一个中断源在 ISR 执行期间被屏蔽). 但**绝对不要**在任务里也调这个函数,
 *    那样 state/idx 会被两个上下文同时改. */
void Asr_RxByteFromISR(uint8_t byte)
{
    static uint8_t state = 0U;          /* 0=等 0x55, 1=等 0xAA, 2=攒数据 */
    static uint8_t idx   = 0U;

    Asr_RxByteCount++;                  /* 【诊断用, 查完可删】 */

    switch (state)
    {
        case 0U:
            if (byte == 0x55U)
            {
                Asr_RxPacket[idx] = byte;
                idx++;
                state = 1U;
            }
            break;

        case 1U:
            if (byte == 0xAAU)
            {
                Asr_RxPacket[idx] = byte;
                idx++;
                state = 2U;
            }
            else
            {
                idx   = 0U;             /* 帧头不完整, 重新找 */
                state = 0U;
            }
            break;

        case 2U:
            Asr_RxPacket[idx] = byte;
            idx++;
            if (idx >= ASR_FRAME_LEN)
            {
                if (Asr_CalXor(Asr_RxPacket, ASR_FRAME_LEN - 1U) ==
                    Asr_RxPacket[ASR_FRAME_LEN - 1U])
                {
                    Asr_RxFlag = 1U;    /* 交给任务去处理, 中断里不做动作 */
                }
                idx   = 0U;
                state = 0U;             /* 无论校验对错都回去找下一个帧头 */
            }
            break;

        default:
            idx   = 0U;
            state = 0U;
            break;
    }
}

void Asr_Init(void)
{
    /* 直接开 RXNE 中断, 中断里直接读 DR (04 篇第 1 步的"做法 A").
     * 不用 HAL_UART_Receive_IT: 那个是单次的, 必须每收一个字节重挂一次,
     * 忘了重挂就只收得到第一个字节. */
    __HAL_UART_ENABLE_IT(&ASR_UART, UART_IT_RXNE);
}
