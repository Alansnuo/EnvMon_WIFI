/* ============================================================================
 * 语音命令执行任务: 天问模块识别出指令 → 这里翻译成动作
 * 帧解析在 BSP/ASR/asr.c, 本文件只管"命令码 → 干什么"
 *
 * 现在处于**验证阶段**: 只打原始 6 字节, 不打命令码翻译.
 * 这是 04 篇验证步骤的"第二段" —— 先确认模块发得对不对, 再接动作.
 * ========================================================================== */

#include "task_asr.h"
#include "app_main.h"
#include <stdio.h>

#include "asr.h"

/* 打印模式:
 *   0 = 只打原始 6 字节  ← 现在用这个, 验证模块, 输出最干净
 *   1 = 额外打印命令码翻译(cmd/data → 动作)
 * 等核对完模块的真实字节、把动作接到实际硬件时, 改回 1.
 * (也可以在 Keil 的 C/C++ → Define 里加 ASR_SHOW_DECODED=1 临时打开, 不用改代码.) */
#ifndef ASR_SHOW_DECODED
#define ASR_SHOW_DECODED    0
#endif

static void TaskAsr(void *argument);

/* 命令码 → 动作. 默认关着, 见 ASR_SHOW_DECODED */
#if ASR_SHOW_DECODED
static void Asr_Decode(void)
{
    uint8_t cmd  = Asr_RxPacket[3];
    uint8_t data = Asr_RxPacket[4];

    switch (cmd)
    {
        case ASR_CMD_LED:                           /* "开灯" / "关灯" */
            printf("[ASR] cmd=%u data=%u -> LED %s\r\n", (unsigned)cmd, (unsigned)data,
                   data ? "ON" : "OFF");
            break;

        case ASR_CMD_FAN:                           /* 板上没风扇, 预留 */
            printf("[ASR] cmd=%u data=%u -> LED2 %s\r\n", (unsigned)cmd, (unsigned)data,
                   data ? "ON" : "OFF");
            break;

        case ASR_CMD_MODE:                          /* "自动模式" / "手动模式" */
            printf("[ASR] cmd=%u data=%u -> MODE %s\r\n", (unsigned)cmd, (unsigned)data,
                   data ? "AUTO" : "MANUAL");
            break;

        case ASR_CMD_HUMID:                         /* 板上没加湿器, 预留蜂鸣器 */
            printf("[ASR] cmd=%u data=%u -> BUZZER %s\r\n", (unsigned)cmd, (unsigned)data,
                   data ? "ON" : "OFF");
            break;

        case ASR_CMD_MOTOR:
        case ASR_CMD_LED_LEVEL:
        case ASR_CMD_FAN_LEVEL:                     /* 板上没这些硬件, 只记录 */
            printf("[ASR] cmd=%u data=%u -> (no hardware on this board)\r\n",
                   (unsigned)cmd, (unsigned)data);
            break;

        default:
        
            printf("[ASR] cmd=%u data=%u -> unknown, check the ASR firmware's command table\r\n",
                   (unsigned)cmd, (unsigned)data);
            break;
    }
}
#endif

/* 收到一整帧(校验已通过)时调用 */
static void Asr_Handle(void)
{
    /* 原始 6 字节: [0][1]帧头 0x55 0xAA, [2]长度, [3]cmd, [4]data, [5]校验.
     * ⚠️ 能走到这里, 说明 XOR 校验已经通过了 —— 也就是说帧结构合法,
     *    波特率基本可以确定是对的(波特率错的时候, 恰好凑出合法帧的概率极低). */
    printf("[ASR] raw: %02X %02X %02X %02X %02X %02X\r\n",
           (unsigned)Asr_RxPacket[0], (unsigned)Asr_RxPacket[1], (unsigned)Asr_RxPacket[2],
           (unsigned)Asr_RxPacket[3], (unsigned)Asr_RxPacket[4], (unsigned)Asr_RxPacket[5]);

#if ASR_SHOW_DECODED
    Asr_Decode();
#endif
}

/* ========================= 建任务 ========================= */
void TaskAsrCreate(void)
{
    if (xTaskCreate(TaskAsr, "Asr", APP_STACK_ASR, NULL,
                    APP_PRIO_ASR, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(Asr) failed (heap too small?)\r\n");
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskAsr(void *argument)
{
    (void)argument;                     /* 未使用, 显式忽略 */

    printf("[ASR] started, poll %d ms\r\n", APP_ASR_PERIOD_MS);

    for (;;)
    {
        /* 标志位 + 短轮询(04 篇的"写法 A"): 中断里完全不碰 FreeRTOS API,
         * 省掉"中断优先级必须 >= 5"那一整类坑. 最坏延迟 50ms, 人耳感觉不到.
         * ⚠️ APP_ASR_PERIOD_MS 不能设成 0 —— vTaskDelay(0) 等于不延时,
         *    任务会变成忙等把 CPU 吃光. */
        if (Asr_RxFlag != 0U)
        {
            Asr_RxFlag = 0U;
            Asr_Handle();
        }

        vTaskDelay(pdMS_TO_TICKS(APP_ASR_PERIOD_MS));
    }
}
