/* ============================================================================
 * 光照采集任务: 每 2 秒读一次 BH1750, 更新共享最新值 (给 OLED / 语音任务用)
 * 一个数据源一个任务文件, 不要往这里塞第二个传感器
 * ========================================================================== */

#include "task_light.h"
#include "app_main.h"

#include "BH1750.h"

static void TaskLight(void *argument);

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
            /* 更新共享最新值给 OLED 显示. float 写非原子, 但 OLED 那边
             * 进临界区再拷, 即便读到"旧高 4 字节 + 新低 4 字节"也只是数字跳一下.
             * (读失败就保持上一次的值, OLED 上不会有跳变.) */
            g_xLatestLux = lux;
        }
        /* 读失败不处理: 多半是器件没应答 (查 U6 供电 / PB6=SCL / PB7=SDA / 上拉),
         * 下个周期再试 */

        /* 这里用 vTaskDelay 而不是忙等: 2 秒里必须让出 CPU,
         * 否则会把 WiFi / DHT11 全饿死. (BH1750 转换那 180ms 在驱动里
         * 已经用 BSP_DelayMs 让出过一次了.) */
        vTaskDelay(pdMS_TO_TICKS(APP_LIGHT_PERIOD_MS));
    }
}
