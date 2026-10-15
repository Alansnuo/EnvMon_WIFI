/* ============================================================================
 * BSP_DelayMs() 的裸机兜底实现 —— 弱定义, 上 RTOS 后被 APP/app_delay.c 覆盖
 * ========================================================================== */

#include "bsp_delay.h"
#include "main.h"                       /* HAL_Delay */

/* __weak 是 AC5 关键字; 链接器遇同名强定义时优先用强定义, 不报错不警告. */
__weak void BSP_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ============================================================================
 * BSP_DelayUs() —— 微秒忙等, 用 TIM2 自由运行计数器
 * 预分频 1MHz, ARR=0xFFFF, 不使能中断; 1 计数 = 1us, 只读 CNT 不占引脚.
 * 16 位每 65536us 回绕, 无符号相减比较抗回绕.
 * 上电后本文件初始化在 MX_TIM2_Init() 之后, 以本文件为准; 不要开 TIM2 中断.
 * ========================================================================== */

#define BSP_US_TIM           TIM2
#define BSP_US_TIM_CLK_EN()  __HAL_RCC_TIM2_CLK_ENABLE()    /* 换定时器改这两行即可 */
#define BSP_US_TIM_MAX       60000UL    /* 单次最多数这么多 us, 离回绕留点余量 */

static uint8_t s_us_tim_ready = 0U;

static void BSP_DelayUsInit(void)
{
    RCC_ClkInitTypeDef clk;
    TIM_HandleTypeDef  htim = {0};      /* 必须清零: HAL 会读 htim.Lock, 垃圾值会让 Init 直接返回 BUSY */
    uint32_t           lat;
    uint32_t           timclk;

    BSP_US_TIM_CLK_EN();

    /* APB1 分频非 1 时定时器时钟 = 2 x PCLK1 */
    HAL_RCC_GetClockConfig(&clk, &lat);
    timclk = (clk.APB1CLKDivider == RCC_HCLK_DIV1) ? HAL_RCC_GetPCLK1Freq()
                                                   : (2UL * HAL_RCC_GetPCLK1Freq());

    htim.Instance               = BSP_US_TIM;
    htim.Init.Prescaler         = (timclk / 1000000UL) - 1UL;   /* 计数频率 1MHz */
    htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim.Init.Period            = 0xFFFFUL;
    htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim) == HAL_OK)
    {
        (void)HAL_TIM_Base_Start(&htim);    /* 只让 CNT 跑起来, 不要中断 */
        s_us_tim_ready = 1U;
    }
}

static void BSP_DelayUsOnce(uint16_t us)
{
    uint16_t start = (uint16_t)BSP_US_TIM->CNT;
    uint32_t guard = ((uint32_t)us * 128UL) + 2000UL;

    while ((uint16_t)(BSP_US_TIM->CNT - start) < us)
    {
        /* guard 兜底: 计数器未启动时不死循环 */
        if (--guard == 0UL) { break; }
    }
}

uint16_t BSP_GetUs(void)
{
    if (s_us_tim_ready == 0U) { BSP_DelayUsInit(); }
    return (uint16_t)BSP_US_TIM->CNT;
}

void BSP_DelayUs(uint32_t us)
{
    if (us == 0U) { return; }

    if (s_us_tim_ready == 0U) { BSP_DelayUsInit(); }
    if (s_us_tim_ready == 0U) { return; }   /* 计数器未启动, 放弃本次延时 */

    /* 16 位计数器一次数不了那么久, 拆成几段 */
    while (us > BSP_US_TIM_MAX)
    {
        BSP_DelayUsOnce((uint16_t)BSP_US_TIM_MAX);
        us -= BSP_US_TIM_MAX;
    }
    BSP_DelayUsOnce((uint16_t)us);
}
