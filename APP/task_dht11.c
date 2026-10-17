/* ============================================================================
 * DHT11 采集任务: 周期读温湿度, 投递给通信任务
 * 一个数据源一个任务文件, 不并入第二个传感器
 * ========================================================================== */

#include "task_dht11.h"
#include "app_main.h"

#include "dht11.h"

static void TaskDht11(void *argument);

/* ========================= 建任务 ========================= */
void TaskDht11Create(void)
{
    if (xTaskCreate(TaskDht11, "DHT11", APP_STACK_DHT11, NULL,
                    APP_PRIO_DHT11, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskDht11(void *argument)
{
    DHT11_Data dht;

    (void)argument;                     /* 未使用, 显式忽略 */

    for (;;)
    {
        if (DHT11_Read(&dht))
        {
            g_xLatestDht11 = dht;               /* 共享最新值给 OLED 显示 */

            /* 投给通信任务; 队列满则丢弃, 生产者不阻塞 */
            (void)xQueueSend(g_xDht11Queue, &dht, 0);
        }

        /* 用 vTaskDelay 挂起让出 CPU, 不用 HAL_Delay 忙等 */
        vTaskDelay(pdMS_TO_TICKS(APP_DHT11_PERIOD_MS));
    }
}
