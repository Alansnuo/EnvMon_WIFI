/* ============================================================================
 * 光照采集任务: 每 2 秒读一次 BH1750 更新共享值, 并驱动 PA8 调光灯联动
 * ========================================================================== */

#include "task_light.h"
#include "app_main.h"

#include "BH1750.h"
#include "pwm.h"
#include "app_ui.h"

static void TaskLight(void *argument);

/* 亮度平滑过渡: 2 秒周期内分 100 档渐变, 每 20ms 一档, 避免跳变 */
#define LIGHT_FADE_STEP_MS   20U
#define LIGHT_FADE_STEPS     (APP_LIGHT_PERIOD_MS / LIGHT_FADE_STEP_MS) // 100 档

/**
 * @brief 光控联动映射: 光照越暗, 灯越亮.
 * @param lux 当前照度 (lx).
 * @retval 亮度档位 (0~PWM_MAX_BRIGHT).
 * @note  低于下限(TH_LUX_DN)全亮, 高于上限(TH_LUX_UP)全灭, 中间线性过渡.
 */
static uint16_t Light_ToBright(float lux)
{
    int TH_low= (int)Thresh_Get(TH_LUX_DN);
    int TH_high = (int)Thresh_Get(TH_LUX_UP);
    int v  = (int)lux;

    if (TH_high <= TH_low) { TH_high = TH_low + 1; }              /* 阈值相等/改反时防除零 */
    if (v <= TH_low) { return PWM_MAX_BRIGHT; }     /* 暗 → 全亮 */
    if (v >= TH_high) { return 0U; }                 /* 亮 → 全灭 */

    return (uint16_t)((TH_high - v) * PWM_MAX_BRIGHT / (TH_high - TH_low)); //
}

/* ========================= 建任务 ========================= */
void TaskLightCreate(void)
{
    if (xTaskCreate(TaskLight, "Light", APP_STACK_LIGHT, NULL,
                    APP_PRIO_LIGHT, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskLight(void *argument)
{
    float    lux;
    uint16_t bright = 0U;       /* 当前亮度, 跨周期保留, 用于平滑过渡 */

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        uint16_t start, target;  //start: 当前亮度, target: 目标亮度
        int32_t  diff;
        uint32_t i;

        /* 读照度更新共享值 (失败沿用上次值) */
        if (BH1750_ReadLux(&lux))
        {
            g_xLatestLux = lux;
        }

        /* 光控联动: 目标亮度分步平滑逼近 */
        start  = bright;
        target = Light_ToBright(g_xLatestLux);
        diff   = (int32_t)target - (int32_t)start;

        for (i = 1U; i <= LIGHT_FADE_STEPS; i++)
        {
            bright = (uint16_t)((int32_t)start + diff * (int32_t)i / (int32_t)LIGHT_FADE_STEPS);
            PWM_SetBright(bright);
            /* 每档延时 20ms, 让出 CPU */
            vTaskDelay(pdMS_TO_TICKS(LIGHT_FADE_STEP_MS));
        }
    }
}
