/* ============================================================================
 * 语音任务 —— 四件事
 *   1) 应答: 模块发 "TEMP"/"HUMI"/"LUX" → 回数值
 *   2) 定时播报: 每 APP_ASR_REPORT_PERIOD_MS 主动播一次环境数据
 *   3) 按键触发: 环境页按 KEY2 立刻播一次
 *   4) 阈值告警: 每秒查一次, 只在状态变化时播报; 告警码写 g_xAlarmCode 供 OLED
 *
 * 阈值读 app_ui.c 运行时变量 (按键改的那份), 不读 app_config.h 宏。
 * 帧解析在 BSP/ASR/asr.c, 本文件只管"什么时候发什么"。
 * ========================================================================== */

#include "task_asr.h"
#include "app_main.h"
#include "app_ui.h"
#include <string.h>

#include "asr.h"

static void TaskAsr(void *argument);

/* 光线分档: 0 = 过暗, 1 = 适宜, 2 = 过强。模块按这个等级决定播报哪一句 */
static uint8_t Asr_LightLevel(int lux)
{
    if (lux < (int)Thresh_Get(TH_LUX_DN)) { return ASR_LIGHT_DARK;   }
    if (lux > (int)Thresh_Get(TH_LUX_UP)) { return ASR_LIGHT_STRONG; }
    return ASR_LIGHT_OK;
}

/* 阈值检查: 返回告警码, 0 = 一切正常 */
static uint8_t Asr_CheckAlarm(void)
{
    int temp, humi, lux;

    /* 拷一份共享数据再判断; 温湿度全 0 = DHT11 从未成功, 不参与告警 */
    taskENTER_CRITICAL();
    temp = (int)g_xLatestDht11.temperature;
    humi = (int)g_xLatestDht11.humidity;
    lux  = (int)g_xLatestLux;
    taskEXIT_CRITICAL();

    if (temp != 0 || humi != 0)
    {
        if (temp > (int)Thresh_Get(TH_TEMP_UP)) { return ASR_ALARM_TEMP_HI; }
        if (temp < (int)Thresh_Get(TH_TEMP_DN)) { return ASR_ALARM_TEMP_LO; }
        if (humi > (int)Thresh_Get(TH_HUMI_UP)) { return ASR_ALARM_HUMI_HI; }
        if (humi < (int)Thresh_Get(TH_HUMI_DN)) { return ASR_ALARM_HUMI_LO; }
    }
    /* 光照不套此判断: 0 lx 是合法读数 (全黑) */
    if (lux < (int)Thresh_Get(TH_LUX_DN)) { return ASR_ALARM_LUX_LO; }
    if (lux > (int)Thresh_Get(TH_LUX_UP)) { return ASR_ALARM_LUX_HI; }

    return 0U;
}

/* 模块问某一项数据 → 回一个数值 */
static void Asr_Answer(const char *req)
{
    int temp, humi, lux;

    taskENTER_CRITICAL();
    temp = (int)g_xLatestDht11.temperature;
    humi = (int)g_xLatestDht11.humidity;
    lux  = (int)g_xLatestLux;
    taskEXIT_CRITICAL();

    if (strcmp(req, ASR_REQ_TEMP) == 0)
    {
        Asr_SendNum(temp);
    }
    else if (strcmp(req, ASR_REQ_HUMI) == 0)
    {
        Asr_SendNum(humi);
    }
    else if (strcmp(req, ASR_REQ_LUX) == 0)
    {
        Asr_SendNum(lux);
    }
    /* 其它字符串一律忽略 (防杂波误触发) */
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
    uint32_t tick_led    = 0U;      /* LED 闪烁节拍计数 */
    uint8_t  last_alarm  = 0U;      /* 上次的告警码, 只在变化时播报 */
    char     line[ASR_LINE_MAX];

    (void)argument;

    for (;;)
    {
        /* ---------- 1. 应答模块的数据请求 ---------- */
        if (Asr_LineReady != 0U)
        {
            /* 先拷内容再清标志, 顺序反了会和中断抢缓冲 */
            (void)strncpy(line, (const char *)Asr_Line, ASR_LINE_MAX);
            line[ASR_LINE_MAX - 1U] = '\0';
            Asr_LineLen   = 0U;
            Asr_LineReady = 0U;

            Asr_Answer(line);
        }

        /* ---------- 2. 按键触发的播报 ---------- */
        if (g_xAsrSpeakReq != 0U)
        {
            int temp, humi, lux;
            uint8_t light;

            taskENTER_CRITICAL();
            temp = (int)g_xLatestDht11.temperature;
            humi = (int)g_xLatestDht11.humidity;
            lux  = (int)g_xLatestLux;
            taskEXIT_CRITICAL();
            light = Asr_LightLevel(lux);

            g_xAsrSpeakReq = 0U;
            tick_report    = 0U;        /* 清定时计数, 免得刚播完又播 */
            Asr_SendEnv(temp, humi, light);
        }

        /* ---------- 3. 定时播报 ---------- */
        tick_report++;
        if (tick_report >= (APP_ASR_REPORT_PERIOD_MS / APP_ASR_PERIOD_MS))
        {
            int temp, humi, lux;
            uint8_t light;

            taskENTER_CRITICAL();
            temp = (int)g_xLatestDht11.temperature;
            humi = (int)g_xLatestDht11.humidity;
            lux  = (int)g_xLatestLux;
            taskEXIT_CRITICAL();
            light = Asr_LightLevel(lux);

            tick_report = 0U;
            Asr_SendEnv(temp, humi, light);
        }

        /* ---------- 4. 阈值告警 ---------- */
        tick_alarm++;
        if (tick_alarm >= (1000U / APP_ASR_PERIOD_MS))
        {
            uint8_t code = Asr_CheckAlarm();

            tick_alarm   = 0U;
            g_xAlarmCode = code;        /* 供 OLED 闪烁提示, 只在此算 */

            if (code != last_alarm)     /* 只在状态变化时播报 */
            {
                last_alarm = code;
                Asr_SendAlarm((code == 0U) ? ASR_ALARM_OK : code);
            }
        }

        /* ---------- 5. 告警 LED (PB5, 另一端接 3.3V) ----------
         * 正常: PA6=HIGH 灭; 超限: 每 500ms 翻转 → 1Hz 闪烁 */
        tick_led++;
        if (g_xAlarmCode != 0U)
        {
            if (tick_led >= (500U / APP_ASR_PERIOD_MS))    /* 500ms 翻一次 */
            {
                tick_led = 0U;
                HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
            }
        }
        else
        {
            tick_led = 0U;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);  /* 灭 */
        }

        /* ⚠️ APP_ASR_PERIOD_MS 不能为 0, 否则忙等吃光 CPU */
        vTaskDelay(pdMS_TO_TICKS(APP_ASR_PERIOD_MS));
    }
}
