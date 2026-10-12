/* ============================================================================
 * 语音任务 —— 三件事
 *   1) 应答: 模块发 "TEMP"/"HUMI"/"LUX" → 回一个数值(动作码 2)
 *   2) 定时播报 (必做3 的"定时"那一半): 每 APP_ASR_REPORT_PERIOD_MS 主动让模块
 *      播报"当前温度X摄氏度，湿度百分之Y，光线…"
 *   3) 阈值告警 (必做4): 每秒查一次, **只在告警状态变化时**播报, 免得反复念
 *
 * ⚠️ 按键相关的两条("按按键触发播报"和"按键设置阈值")暂时没做 —— 板上没有按键。
 *    阈值先用 app_config.h 里的固定值; 以后加了按键, 改成运行时变量即可。
 *
 * 帧解析/收行在 BSP/ASR/asr.c, 本文件只管"什么时候发什么"。
 * ========================================================================== */

#include "task_asr.h"
#include "app_main.h"
#include "app_config.h"
#include <string.h>

#include "asr.h"

static void TaskAsr(void *argument);

/* 光线分档: 01 = 过暗, 1 = 适宜, 2 = 过强。模块按这个等级决定播报哪一句 */
static uint8_t Asr_LightLevel(int *lux_out)
{
    int lux = (int)g_xLatestLux;

    *lux_out = lux;

    if (lux < CFG_TH_LUX_DN) { return ASR_LIGHT_DARK;   }
    if (lux > CFG_TH_LUX_UP) { return ASR_LIGHT_STRONG; }
    return ASR_LIGHT_OK;
}

/* DHT11 到底读到过数据没有?
 * ⚠️ 关键: DHT11_Read() 失败时 g_xLatestDht11 **保持上一次的值, 从没成功过就一直是 0**。
 *    直接拿 0 去比阈值, 传感器一坏就会永久播报"温度过低"。
 *    所以 温度/湿度 全 0 一律当作"没数据", 不参与告警判断。 */
static uint8_t Asr_DhtValid(void)
{
    return ((g_xLatestDht11.temperature != 0U) ||
            (g_xLatestDht11.humidity    != 0U)) ? 1U : 0U;
}

/* 阈值检查: 返回告警码, 0 = 一切正常 */
static uint8_t Asr_CheckAlarm(void)
{
    int temp = (int)g_xLatestDht11.temperature;
    int humi = (int)g_xLatestDht11.humidity;
    int lux  = (int)g_xLatestLux;

    if (Asr_DhtValid() != 0U)
    {
        if (temp > CFG_TH_TEMP_UP) { return ASR_ALARM_TEMP_HI; }
        if (temp < CFG_TH_TEMP_DN) { return ASR_ALARM_TEMP_LO; }
        if (humi > CFG_TH_HUMI_UP) { return ASR_ALARM_HUMI_HI; }
        if (humi < CFG_TH_HUMI_DN) { return ASR_ALARM_HUMI_LO; }
    }
    /* 光照不套这个判断: BH1750 一直好使, 而且 0 lx 是合法读数(全黑) */
    if (lux < CFG_TH_LUX_DN) { return ASR_ALARM_LUX_LO; }
    if (lux > CFG_TH_LUX_UP) { return ASR_ALARM_LUX_HI; }

    return 0U;
}

/* 模块问某一项数据 → 回一个数值 */
static void Asr_Answer(const char *req)
{
    if (strcmp(req, ASR_REQ_TEMP) == 0)
    {
        Asr_SendNum((int)g_xLatestDht11.temperature);
    }
    else if (strcmp(req, ASR_REQ_HUMI) == 0)
    {
        Asr_SendNum((int)g_xLatestDht11.humidity);
    }
    else if (strcmp(req, ASR_REQ_LUX) == 0)
    {
        Asr_SendNum((int)g_xLatestLux);
    }
    /* 别的字符串一律忽略 —— 防线上杂波 / 模块的其它输出被当成数据请求 */
}

/* ========================= 建任务 ========================= */
void TaskAsrCreate(void)
{
    if (xTaskCreate(TaskAsr, "Asr", APP_STACK_ASR, NULL,
                    APP_PRIO_ASR, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskAsr(void *argument)
{
    uint32_t tick_report = 0U;      /* 定时播报计数 */
    uint32_t tick_alarm  = 0U;      /* 告警检查计数 */
    uint8_t  last_alarm  = 0U;      /* 上次的告警码, 只在变化时播报 */
    char     line[ASR_LINE_MAX];

    (void)argument;

    for (;;)
    {
        /* ---------- 1. 应答模块的数据请求 ---------- */
        if (Asr_LineReady != 0U)
        {
            /* 先把内容拷出来, 再清标志 —— 顺序反了会和中断抢缓冲 */
            (void)strncpy(line, (const char *)Asr_Line, ASR_LINE_MAX);
            line[ASR_LINE_MAX - 1U] = '\0';
            Asr_LineLen   = 0U;
            Asr_LineReady = 0U;

            Asr_Answer(line);
        }

        /* ---------- 2. 定时播报 (必做3) ---------- */
        tick_report++;
        if (tick_report >= (APP_ASR_REPORT_PERIOD_MS / APP_ASR_PERIOD_MS))
        {
            int     lux   = 0;
            uint8_t light = Asr_LightLevel(&lux);

            tick_report = 0U;
            Asr_SendEnv((int)g_xLatestDht11.temperature,
                        (int)g_xLatestDht11.humidity,
                        light);
        }

        /* ---------- 3. 阈值告警 (必做4) ---------- */
        tick_alarm++;
        if (tick_alarm >= (1000U / APP_ASR_PERIOD_MS))
        {
            uint8_t code = Asr_CheckAlarm();

            tick_alarm = 0U;
            if (code != last_alarm)     /* 只在状态变化时播报 */
            {
                last_alarm = code;
                Asr_SendAlarm((code == 0U) ? ASR_ALARM_OK : code);
            }
        }

        /* ⚠️ APP_ASR_PERIOD_MS 绝不能设成 0: vTaskDelay(0) 不延时, 任务会忙等吃光 CPU */
        vTaskDelay(pdMS_TO_TICKS(APP_ASR_PERIOD_MS));
    }
}
