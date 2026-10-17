/* ============================================================================
 * BSP_DelayMs() 的裸机兜底实现: 弱定义, 上 RTOS 后由 APP/app_delay.c 的强定义覆盖
 * ========================================================================== */

#include "bsp_delay.h"
#include "main.h"                       /* HAL_Delay */
#include "tim.h"                        /* htim2: 微秒时基 */

/* __weak 为 AC5 关键字; 链接器遇同名强定义时优先采用, 不报错不警告. */
__weak void BSP_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ============================================================================
 * BSP_DelayUs() —— 微秒忙等, 读 TIM2 自由运行计数器
 * TIM2 由 MX_TIM2_Init() 配置并启动 (1MHz, ARR=0xFFFF), 不使能中断.
 * 16 位计数器每 65536us 回绕, 用无符号相减比较抗回绕.
 * ========================================================================== */

#define BSP_US_TIM           (&htim2)
#define BSP_US_TIM_MAX       60000UL    /* 单次延时上限, 距回绕留有余量 */

static void BSP_DelayUsOnce(uint16_t us)
{
    uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(BSP_US_TIM);

    while ((uint16_t)(__HAL_TIM_GET_COUNTER(BSP_US_TIM) - start) < us) { }
}

uint16_t BSP_GetUs(void)
{
    return (uint16_t)__HAL_TIM_GET_COUNTER(BSP_US_TIM);
}

void BSP_DelayUs(uint32_t us)
{
    /* 16 位计数器单次计不到那么久, 分段延时 */
    while (us > BSP_US_TIM_MAX)
    {
        BSP_DelayUsOnce((uint16_t)BSP_US_TIM_MAX);
        us -= BSP_US_TIM_MAX;
    }
    BSP_DelayUsOnce((uint16_t)us);
}
