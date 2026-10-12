/* ============================================================================
 * 光照采集任务: 每 2 秒读一次 BH1750, 串口打印
 * 一个数据源一个任务文件, 不要往这里塞第二个传感器
 * ========================================================================== */

#include "task_light.h"
#include "app_main.h"
#include <stdio.h>

#include "BH1750.h"

static void TaskLight(void *argument);

/* ========================= 建任务 ========================= */
void TaskLightCreate(void)
{
    if (xTaskCreate(TaskLight, "Light", APP_STACK_LIGHT, NULL,
                    APP_PRIO_LIGHT, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(Light) failed (heap too small?)\r\n");
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskLight(void *argument)
{
    uint32_t     count = 0;
    float        lux;
    unsigned int lux10;                 /* lux x10 取整, 用来拼出一位小数 */

    (void)argument;                     /* 未使用, 显式忽略 */

    printf("[LIGHT] started, period %d ms\r\n", APP_LIGHT_PERIOD_MS);

    for (;;)
    {
        /* ⚠️ 不用 printf 的 %f: 本工程开了 microlib, 它的 printf 不保证支持浮点
         * 格式化. 所以先把 lux 放大 10 倍取整, 再用整数拼出 "x.y" 的样子.
         * (BH1750 的分辨率是 1/1.2 = 0.833 lx, 小数位是有信息量的.) */
        if (BH1750_ReadLuxChecked(&lux))
        {
            lux10 = (unsigned int)((lux * 10.0f) + 0.5f);
            count++;
            printf("[LIGHT] #%lu  %u.%u lx\r\n",
                   (unsigned long)count, lux10 / 10U, lux10 % 10U);

            /* 同时更新共享最新值给 OLED 显示. float 写非原子, 但 OLED 那边
             * 进临界区再拷, 即便读到"旧高 4 字节 + 新低 4 字节"也只是数字跳一下. */
            g_xLatestLux = lux;
        }
        else
        {
            /* 器件没应答: 查 U6 供电 / PB6(SCL) / PB7(SDA) / 上拉 */
            count++;
            printf("[LIGHT] #%lu  Read Error! (check U6, PB6=SCL, PB7=SDA)\r\n",
                   (unsigned long)count);
        }

        /* 这里用 vTaskDelay 而不是忙等: 2 秒里必须让出 CPU,
         * 否则会把 WiFi / DHT11 全饿死. (BH1750 转换那 180ms 在驱动里
         * 已经用 BSP_DelayMs 让出过一次了.) */
        vTaskDelay(pdMS_TO_TICKS(APP_LIGHT_PERIOD_MS));
    }
}
