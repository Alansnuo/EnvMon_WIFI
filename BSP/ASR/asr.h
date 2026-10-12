#ifndef __ASR_H
#define __ASR_H

#include "main.h"
#include <stdint.h>

/* ============================================================================
 * 天问语音模块 (ASRPRO) —— 6 字节定长帧的接收与解析
 *
 * 模块接在 USART1 (PA9/PA10) 上, 和调试 printf **共用一路**. 共用为什么安全:
 *   printf 输出全是 ASCII, 而帧头要 0x55 0xAA —— 0xAA 不是 ASCII 字符, 所以
 *   printf 刷得再欢也伪造不出一条指令; 反过来模块的字节流也形不成 printf 看到的
 *   有意义内容. 详见 04 篇.
 *
 * ⚠️ 调试助手的 TX **绝对不能**接到 PA10: 那根线上已经有模块的 TX 在驱动,
 *    两个输出顶在一起会互相干扰(严重时损坏 IO). 调试助手只能单向"听".
 *
 * ⚠️ 波特率: 模块的串口波特率是在**它自己的固件里配的**(参考工程用的是 9600),
 *    而这里的 USART1 是 115200. 两边必须一致 —— 推荐在天问 Block 里把模块改成
 *    115200, 这样 printf 不受影响.
 * ========================================================================== */

extern UART_HandleTypeDef huart1;           /* 定义在 Core/Src/usart.c */
#define ASR_UART      huart1

/* 帧格式: [0]0x55 [1]0xAA [2]长度(=2) [3]cmd [4]data [5]xor(前 5 字节逐个异或) */
#define ASR_FRAME_LEN   6U

/* 命令码 —— 对应"天问模块固件里配的识别项".
 * ⚠️ 这些编号**不是协议规定的**, 是天问 Block 图形化界面里给每条语音指令分配的.
 *    参考工程的 1~7 只对参考工程那份固件有效; 你的模块刷的是哪份固件, 就只有
 *    那份的命令表. 所以先用 task_asr.c 的原始打印把每条命令的真实字节抄下来核对. */
#define ASR_CMD_LED        1U       /* 控制 LED 开关      data: 1=开 0=关 */
#define ASR_CMD_FAN        2U       /* 控制风扇开关       参考工程用; 本板没风扇 */
#define ASR_CMD_MOTOR      3U       /* 控制步进电机(窗帘) 本板没有 */
#define ASR_CMD_MODE       4U       /* 切换系统模式       data: 1=自动 0=手动 */
#define ASR_CMD_LED_LEVEL  5U       /* 设置 LED 亮度档位  data: 0~10, 是"档位"不是百分比 */
#define ASR_CMD_FAN_LEVEL  6U       /* 设置风扇速度档位   本板没有 */
#define ASR_CMD_HUMID      7U       /* 控制加湿器继电器   本板没有 */

/* 收到一帧且校验通过. 中断里置 1, 任务里读到后清 0.
 * ⚠️ 必须 volatile: 它在中断里被改、在任务里读, 不加的话编译器可能把任务里的
 *    读操作提升成"永远不变"从而死循环. */
extern volatile uint8_t Asr_RxFlag;

/* 最近一帧的原始 6 字节: [0][1]帧头 [2]长度 [3]cmd [4]data [5]校验 */
extern uint8_t Asr_RxPacket[ASR_FRAME_LEN];

/* 累计收到过多少个字节(含帧头噪声/校验失败的).
 * 【诊断用】**不需要打印** —— 在 Keil 调试器里把它加到 watch 窗口就行:
 *   对着模块说一句指令, 这个数涨了 => USART1 中断和接线都是好的;
 *   一直是 0 => 中断没进来, 或模块压根没发. */
extern volatile uint32_t Asr_RxByteCount;

/* 打开 USART1 的 RXNE 接收中断. 在 App_Init() 里调一次 */
void Asr_Init(void);

/* 逐字节喂给帧状态机. **只允许在 USART1 中断里调** —— 任务里也调会竞态 */
void Asr_RxByteFromISR(uint8_t byte);

#endif /* __ASR_H */
