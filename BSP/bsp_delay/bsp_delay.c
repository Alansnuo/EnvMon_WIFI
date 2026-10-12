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

/* ============================================================================
 * BSP_DelayUs() —— 微秒忙等, 用 TIM2 自由运行计数器
 *
 * 为什么选 TIM2:
 *   TIM4 被 HAL 时基占用(Core/Src/stm32f1xx_hal_timebase_tim.c), 动不得;
 *   TIM3 留给软件 PWM、TIM1 留给硬件 PWM(见 06 篇第六节).
 *   TIM2 在本工程里没人用, 而且这里只读 CNT —— 不开中断、不配通道,
 *   所以 PA0~PA3 照样是普通 GPIO, 一个引脚都不占.
 *
 * 配置: 预分频到 1MHz, ARR = 0xFFFF, 不使能中断, 让 CNT 自己跑.
 *   1 个计数 = 1us, 所以延时值直接就是计数差, 不用再换算.
 *   16 位计数器每 65536us 回绕一次; 用无符号相减比较, 回绕安全.
 *
 * 和 CubeMX 的关系(重要):
 *   project.ioc 里 TIM2 也登记了, 所以 CubeMX 会生成 Core/Src/tim.c 的
 *   MX_TIM2_Init(). 上电后 TIM2 因此会被初始化两次:
 *       1) main() 里 MX_TIM2_Init()          —— 用 .ioc 里写死的 PSC=71
 *       2) 之后 DHT11_Init()/BH1750_Init() 触发这里的 BSP_DelayUsInit()
 *   第 2 次在后, 会覆盖第 1 次, **实际以这里为准**.
 *   两次的值一样(都是 71), 重复初始化没有副作用 —— HAL 只是照写一遍寄存器.
 *   这么设计是为了: 以后在 CubeMX 里改时钟树时, .ioc 里那个 71 会失准,
 *   而这里是从 HAL_RCC_GetClockConfig() 实测时钟重算的, 永远保持 1 计数 = 1us.
 *
 *   ⚠️ 用了 CubeMX 的 TIM2 就**不要**再在 NVIC 里勾 TIM2 global interrupt:
 *      本文件只用 HAL_TIM_Base_Start() 轮询, 从不置 UIE 位, 勾了也不会触发;
 *      而且 startup 里 TIM2_IRQHandler 是空的死循环弱定义, 一旦哪天真触发就是死机.
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

    /* APB1 分频系数不是 1 时, 定时器时钟 = 2 x PCLK1
     * (本工程 APB1 = HCLK/2 = 36MHz, 所以 TIM2 的计数时钟是 72MHz) */
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
        /* guard 是兜底: 万一计数器没跑起来, 宁可延时短一点,
         * 也不要死在这儿 —— 那种死机在现场极难定位. */
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
    if (s_us_tim_ready == 0U) { return; }   /* 计数器没起来, 只能放弃这次延时 */

    /* 16 位计数器一次数不了那么久, 拆成几段 */
    while (us > BSP_US_TIM_MAX)
    {
        BSP_DelayUsOnce((uint16_t)BSP_US_TIM_MAX);
        us -= BSP_US_TIM_MAX;
    }
    BSP_DelayUsOnce((uint16_t)us);
}
