/* ============================================================================
 * DHT11 采集任务: 周期读温湿度, 投递给通信任务
 * 一个数据源一个任务文件, 不要往这里塞第二个传感器
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
            /* 同时更新共享最新值给 OLED 显示. 两个 uint8_t 字段, 直接写:
             * DHT11_Data 没位域, 写时不会被任务切换打断成"半新半旧".
             * (OLED 那边进临界区再拷, 是为了一致性, 也为了volatile读不被合并.) */
            g_xLatestDht11 = dht;

            /* 投给通信任务. 超时给 0: 队列满了就丢掉本次数据, 绝不在这里等 ——
             * 采集任务是生产者, 被消费者拖住就本末倒置了. */
            (void)xQueueSend(g_xDht11Queue, &dht, 0);
        }

        /* 必须 vTaskDelay 而不是 HAL_Delay:
         *   vTaskDelay 会把本任务挂起, CPU 让给别人;
         *   HAL_Delay  是忙等, 这 2 秒里别的任务全被饿死. */
        vTaskDelay(pdMS_TO_TICKS(APP_DHT11_PERIOD_MS));
    }
}
