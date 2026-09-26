/* ============================================================================
 * DHT11 采集任务: 周期读温湿度, 投递给通信任务
 * 一个数据源一个任务文件, 不要往这里塞第二个传感器
 * ========================================================================== */

#include "task_dht11.h"
#include "app_main.h"
#include <stdio.h>

#include "dht11.h"

static void TaskDht11(void *argument);

/* ========================= 建任务 ========================= */
void TaskDht11Create(void)
{
    if (xTaskCreate(TaskDht11, "DHT11", APP_STACK_DHT11, NULL,
                    APP_PRIO_DHT11, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(DHT11) failed (heap too small?)\r\n");
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskDht11(void *argument)
{
    DHT11_Data dht;
    uint32_t   count = 0;

    (void)argument;                     /* 未使用, 显式忽略 */

    printf("[DHT11] started, period %d ms\r\n", APP_DHT11_PERIOD_MS);

    for (;;)
    {
        if (DHT11_Read(&dht))
        {
            count++;
            printf("[DHT11] #%lu  Temp: %d C  Humi: %d%%\r\n",
                   (unsigned long)count, dht.temperature, dht.humidity);

            /* 投给通信任务. 超时给 0: 队列满了就丢掉本次数据, 绝不在这里等 ——
             * 采集任务是生产者, 被消费者拖住就本末倒置了. */
            if (xQueueSend(g_xDht11Queue, &dht, 0) != pdPASS)
            {
                printf("[DHT11] queue full, sample dropped\r\n");
            }
        }
        else
        {
            printf("[DHT11] Read Error!\r\n");
        }

        /* 必须 vTaskDelay 而不是 HAL_Delay:
         *   vTaskDelay 会把本任务挂起, CPU 让给别人;
         *   HAL_Delay  是忙等, 这 2 秒里别的任务全被饿死. */
        vTaskDelay(pdMS_TO_TICKS(APP_DHT11_PERIOD_MS));
    }
}
