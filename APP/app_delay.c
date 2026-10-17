/* ============================================================================
 * BSP_DelayMs() 的 FreeRTOS 版: 强定义, 覆盖 BSP 里的弱定义
 * 调度器已启动用 vTaskDelay 让出 CPU; 未启动用 HAL_Delay 忙等
 * ========================================================================== */

#include "bsp_delay.h"
#include "main.h"                       /* HAL_Delay */

#include "FreeRTOS.h"
#include "task.h"

void BSP_DelayMs(uint32_t ms)
{
    /* 须先判断调度器状态: vTaskDelay() 在调度器启动前调用非法, 会陷入 configASSERT 死循环.
     * 本项目 App_Init() 中的 DHT11_Init() 即在此阶段调用, 必须退回忙等.
     * 前提: FreeRTOSConfig.h 中 INCLUDE_xTaskGetSchedulerState = 1 (本工程已开). */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        /* 任务中: 挂起自身, 让出 CPU. configTICK_RATE_HZ = 1000, pdMS_TO_TICKS(ms) 即 ms.
         * 实际时长比 ms 多至多一个 tick, 不影响驱动时序 (DHT11 要求至少 18ms,
         * ESP8266 要求足够启动, 均只多不少). */
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        /* 调度器启动前(或裸机): 无任务可让, 只能忙等 */
        HAL_Delay(ms);
    }
}
