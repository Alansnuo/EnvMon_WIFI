/* ============================================================================
 * 光照采集任务: 每 2 秒读一次 BH1750, 更新共享最新值 (给 OLED / 语音任务用)
 * 一个数据源一个任务文件, 不要往这里塞第二个传感器
 * ========================================================================== */

#include "task_light.h"
#include "app_main.h"

#include "BH1750.h"
#include "pwm.h"
#include "app_ui.h"

static void TaskLight(void *argument);

/* 亮度平滑过渡: 把 2 秒一次的"突变"拆成每 20ms 一档的渐变,
 * 一整周期走完 (100 档), 视觉上连续不跳变 */
#define LIGHT_FADE_STEP_MS   20U
#define LIGHT_FADE_STEPS     (APP_LIGHT_PERIOD_MS / LIGHT_FADE_STEP_MS)

/**
 * @brief 光控联动映射: 光照越暗, 灯越亮.
 * @param lux 当前照度 (lx).
 * @retval 亮度档位 (0~PWM_MAX_BRIGHT).
 * @note  低于下限(TH_LUX_DN)全亮, 高于上限(TH_LUX_UP)全灭, 中间线性过渡.
 */
static uint16_t Light_ToBright(float lux)
{
    int lo = (int)Thresh_Get(TH_LUX_DN);
    int hi = (int)Thresh_Get(TH_LUX_UP);
    int v  = (int)lux;

    if (hi <= lo) { hi = lo + 1; }              /* 阈值相等/改反时防除零 */
    if (v <= lo) { return PWM_MAX_BRIGHT; }     /* 暗 → 全亮 */
    if (v >= hi) { return 0U; }                 /* 亮 → 全灭 */

    return (uint16_t)((uint32_t)(hi - v) * PWM_MAX_BRIGHT / (uint32_t)(hi - lo));
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
        uint16_t start, target;
        int32_t  diff;
        uint32_t i;

        /* 读一次照度, 更新共享值 (读失败就沿用上次的 g_xLatestLux) */
        if (BH1750_ReadLuxChecked(&lux))
        {
            g_xLatestLux = lux;
        }

        /* 光控联动: 算出目标亮度, 再分步把当前亮度平滑逼近目标 */
        start  = bright;
        target = Light_ToBright(g_xLatestLux);
        diff   = (int32_t)target - (int32_t)start;

        for (i = 1U; i <= LIGHT_FADE_STEPS; i++)
        {
            bright = (uint16_t)((int32_t)start + diff * (int32_t)i / (int32_t)LIGHT_FADE_STEPS);
            PWM_SetBright(bright);
            /* 每 20ms 让出一次 CPU, 2 秒里不忙等, 不会饿死 WiFi / DHT11 */
            vTaskDelay(pdMS_TO_TICKS(LIGHT_FADE_STEP_MS));
        }
    }
}
