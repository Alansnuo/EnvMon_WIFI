/* ============================================================================
 * BSP_DelayMs() 的裸机兜底实现 —— 弱定义, 上 RTOS 后被 APP/app_delay.c 覆盖
 * ========================================================================== */

#include "bsp_delay.h"
#include "main.h"                       /* HAL_Delay */

/* __weak 是 AC5 的编译器关键字, 不是 C 标准的东西.
 * ST 的 HAL 自己也是这么干的 —— 见 stm32f1xx_hal.c:371
 *     __weak void HAL_Delay(uint32_t Delay)
 * 链接器看到同名强定义时优先用强的, 既不报错也不警告. */
__weak void BSP_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}
