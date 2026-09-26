/* ============================================================================
 * 应用层总入口: 建数据通道 + 建任务
 * 只负责"建"不负责"跑" —— 以后加传感器, 在这里加一句 TaskXxxCreate() 即可
 * ========================================================================== */

#include "app_main.h"
#include <stdio.h>

#include "dht11.h"
#include "task_dht11.h"
#include "task_wifi.h"

/* ========================= 任务间数据通道 ========================= */
QueueHandle_t g_xDht11Queue = NULL;

/* ========================= 应用层总入口 ========================= */
void App_Init(void)
{
    /* --- 1. 板级驱动初始化 ---
     * 放在调度器之前: 这里还没有别的任务, DHT11_Init() 内部那个 1 秒延时
     * 不会影响谁. 等调度器起来后, 驱动里同样这句 BSP_DelayMs 会自动变成
     * "挂起本任务", 驱动不用改 —— 机制见 BSP/bsp_delay.h. */
    DHT11_Init();
    printf("DHT11 Sensor Ready!\r\n");

    /* --- 2. 建 DHT11 → 通信的数据通道 --- */
    g_xDht11Queue = xQueueCreate(APP_DHT11_QUEUE_LEN, sizeof(DHT11_Data));
    if (g_xDht11Queue == NULL)
    {
        printf("ERROR: xQueueCreate failed (heap too small?)\r\n");
        Error_Handler();
    }

    /* --- 3. 建任务 ---
     * 这两句只是把任务挂到就绪表上, 它们要等 main() 里那句
     * vTaskStartScheduler() 之后才真正开始跑. 谁先建不影响谁先跑,
     * 跑的顺序由优先级决定.
     * 下面两个函数内部会打印错误并进 Error_Handler, 不用查返回值. */
    TaskWifiCreate();
    TaskDht11Create();
}
