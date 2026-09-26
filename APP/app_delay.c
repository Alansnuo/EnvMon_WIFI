/* ============================================================================
 * BSP_DelayMs() 的 FreeRTOS 版 —— 强定义, 覆盖 BSP 里的弱定义
 * 调度器已跑→vTaskDelay 让出 CPU; 未跑→HAL_Delay 忙等
 * ========================================================================== */

#include "bsp_delay.h"
#include "main.h"                       /* HAL_Delay */

#include "FreeRTOS.h"
#include "task.h"

void BSP_DelayMs(uint32_t ms)
{
    /* 先判断调度器状态, 这一步不能省:
     * vTaskDelay() 在调度器启动之前调用是非法操作, 会掉进 configASSERT 死循环.
     *
     * 本项目确实有这种调用 —— App_Init() 里的 DHT11_Init() 跑在
     * vTaskStartScheduler() 之前, 那里必须退回忙等.
     *
     * 前提: FreeRTOSConfig.h 里 INCLUDE_xTaskGetSchedulerState = 1.
     *       本工程已开(FreeRTOSConfig.h:685), 不用改配置. */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        /* 任务里: 挂起自己, CPU 让给别的任务.
         * configTICK_RATE_HZ = 1000, 所以 pdMS_TO_TICKS(ms) 在这里就等于 ms 本身.
         * 实际时长会比 ms 略长(最多多一个 tick), 不影响驱动时序 ——
         * DHT11 要的是"至少 18ms", ESP8266 要的是"足够它启动", 都只多不少. */
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        /* 调度器启动前(或裸机): 没有任务可让, 只能忙等 */
        HAL_Delay(ms);
    }
}
