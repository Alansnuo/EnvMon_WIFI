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
    float lux;

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        if (BH1750_ReadLuxChecked(&lux))
        {
            /* 更新共享最新值给 OLED 显示 (float 写非原子, OLED 侧进临界区再拷) */
            g_xLatestLux = lux;

            /* 光控联动: 按当前照度实时调整 PA8 调光灯亮度 */
            PWM_SetBright(Light_ToBright(lux));
        }
        /* 读失败不处理: 多半是器件没应答 (查 U6 供电 / PB6=SCL / PB7=SDA / 上拉),
         * 下个周期再试 */

        /* 这里用 vTaskDelay 而不是忙等: 2 秒里必须让出 CPU,
         * 否则会把 WiFi / DHT11 全饿死. (BH1750 转换那 180ms 在驱动里
         * 已经用 BSP_DelayMs 让出过一次了.) */
        vTaskDelay(pdMS_TO_TICKS(APP_LIGHT_PERIOD_MS));
    }
}
