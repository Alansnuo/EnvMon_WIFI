/* ============================================================================
 * DHT11 单总线驱动 (STM32F1 + HAL)
 * 时序: 拉低>=18ms → 应答80us×2 → 40bit数据(高电平宽度判0/1)
 * 微秒时基用 DWT 周期计数器, 不占 SysTick/TIM, 与 FreeRTOS 零冲突
 * ========================================================================== */

#include "dht11.h"
#include "bsp_delay.h"

/* ========================= 时基 (DWT) ========================= */
/* 用 Cortex-M3 内核自带的 DWT 周期计数器:
 *   - 32bit 向上计数, 每个 CPU 周期 +1, 72MHz 下分辨率约 13.9ns;
 *   - 完全独立于 SysTick 和任何 TIM, 上 FreeRTOS 后与调度器零冲突;
 *   - 59.6s 才回绕一次, 而本驱动最长只测 200us, 永远碰不到回绕.
 *
 * 历史: 原来用 SysTick 当计数器(不使能中断), 但 FreeRTOS 要独占 SysTick
 * 产生 1ms tick, 两者对 LOAD 的要求直接冲突 —— DHT11 的 Elapsed() 公式
 * 依赖计数器在 0xFFFFFF 回绕, 而 FreeRTOS 把 LOAD 设成 71999, 公式失效.
 * 详见《FreeRTOS驱动适配笔记》第四节. */
#define DHT11_CYC_PER_US_INIT 72UL                  /* 兜底值, Init 时按实际主频重算 */

static uint32_t s_cycles_per_us = DHT11_CYC_PER_US_INIT;    /* DWT 跑 HCLK */

/* ========================= 总线电平操作 ========================= */
/* F1 的 GPIO: MODE=01 输出10MHz, CNF=01 开漏; MODE=00 输入, CNF=10 上/下拉 */
#define DHT11_CONF_OUT_OD     0x5UL                 /* 通用开漏输出 */
#define DHT11_CONF_IN_PU      0x8UL                 /* 输入 + 上拉 */
#define DHT11_CR_SHIFT        ((DHT11_PIN_NUM & 7UL) * 4UL)

#define DHT11_HIGH()          (DHT11_PORT->BSRR = DHT11_PIN)    /* ODR=1: 释放总线 / 上拉 */
#define DHT11_LOW()           (DHT11_PORT->BRR  = DHT11_PIN)    /* ODR=0: 拉低总线 */
#define DHT11_LEVEL()         ((DHT11_PORT->IDR & DHT11_PIN) ? 1U : 0U)

/* ========================= 超时 (us) ========================= */
#define DHT11_TIMEOUT_RESP    200UL                 /* 等从机应答 */
#define DHT11_TIMEOUT_LOW     100UL                 /* 等每 bit 的低电平结束 */
#define DHT11_TIMEOUT_HIGH    150UL                 /* 量高电平宽度的上限 */
#define DHT11_ONE_BIT_TH_US   45UL                  /* >45us 判为 1 (0=26~28us, 1=70us) */

/* ========================= 引脚方向切换 ========================= */
static void DHT11_SetConf(uint32_t conf)
{
    /* CRL 管 0~7 脚, CRH 管 8~15 脚, 每个脚占 4bit */
    volatile uint32_t *cr = (DHT11_PIN_NUM < 8UL) ? &DHT11_PORT->CRL : &DHT11_PORT->CRH;

    *cr = (*cr & ~(0xFUL << DHT11_CR_SHIFT)) | (conf << DHT11_CR_SHIFT);
}

static void DHT11_PinOutput(void)
{
    DHT11_SetConf(DHT11_CONF_OUT_OD);
}

static void DHT11_PinInput(void)
{
    DHT11_HIGH();                                   /* ODR=1 先选上拉, 再切输入模式 */
    DHT11_SetConf(DHT11_CONF_IN_PU);
}

/* ========================= 微秒延时 ========================= */
static void DHT11_TimebaseInit(void)
{
    s_cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000UL;

    /* TRCENA 是 DWT 的总开关, 不开的话 CYCCNT 恒为 0 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0UL;                          /* 计数清零 */
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;       /* 启动周期计数器 */
}

__STATIC_INLINE uint32_t DHT11_Ticks(void)
{
    return DWT->CYCCNT;                         /* 向上计数, 直接读即可 */
}

/* 自 start 起经过的周期数 (向上计数 + 无符号相减, 回绕安全) */
__STATIC_INLINE uint32_t DHT11_Elapsed(uint32_t start)
{
    return (DHT11_Ticks() - start);
}

static void DHT11_DelayUs(uint32_t us)
{
    uint32_t wait  = us * s_cycles_per_us;
    uint32_t start = DHT11_Ticks();

    /* 32bit 计数器只在连续约 59s 后才回绕, 而本驱动最长延时 30us, 无需钳位 */
    while (DHT11_Elapsed(start) < wait) { }
}

/* 等总线变成 level 电平, 超时返回 0 */
static uint8_t DHT11_WaitLevel(uint8_t level, uint32_t timeout_us)
{
    uint32_t start = DHT11_Ticks();
    uint32_t limit = timeout_us * s_cycles_per_us;

    do {
        if (DHT11_LEVEL() == level) { return 1; }
    } while (DHT11_Elapsed(start) < limit);

    return 0;
}

/* 测量当前高电平持续了多少 us, 超过 timeout_us 就按 timeout_us 返回 */
static uint32_t DHT11_MeasureHighUs(uint32_t timeout_us)
{
    uint32_t start = DHT11_Ticks();
    uint32_t limit = timeout_us * s_cycles_per_us;

    while (DHT11_LEVEL()) {
        if (DHT11_Elapsed(start) >= limit) { return timeout_us; }
    }

    return DHT11_Elapsed(start) / s_cycles_per_us;
}

/* ========================= 对外接口 ========================= */
uint8_t DHT11_Init(void)
{
    DHT11_TimebaseInit();
    DHT11_PinOutput();
    DHT11_HIGH();                                   /* 空闲时释放总线 */
    BSP_DelayMs(1000);                              /* 上电后传感器需要约 1s 稳定 */
    return 1;
}

uint8_t DHT11_Read(DHT11_Data *data)
{
    uint8_t  buf[5] = {0};
    uint8_t  ok = 0;
    uint8_t  i, j;
    uint32_t width;

    if (data == NULL) { return 0; }

    /* --- 1. 起始信号: 拉低 20ms, 再释放 --- */
    DHT11_PinOutput();
    DHT11_LOW();
    /* 这里用 BSP_DelayMs 而不是 HAL_Delay: 在任务里它会挂起本任务 20ms,
     * 引脚电平由硬件保持, 不需要 CPU 守着 —— 这 20ms 别的任务照跑.
     * 必须在 __disable_irq() 之外调用: 关着中断是没法阻塞的. */
    BSP_DelayMs(20);
    DHT11_PinInput();                               /* 先切输入+上拉, 释放瞬间就有上拉顶着 */
    DHT11_DelayUs(30);

    /* --- 2. 从机应答: 80us 低 + 80us 高, 整段位流约 4ms 不能被打断 --- */
    __disable_irq();

    /* 等 80us 低 (传感器可能在我们切输入前就已经拉低了) */
    if (!DHT11_WaitLevel(0, DHT11_TIMEOUT_RESP)) { goto exit; }
    /* 等 80us 低结束 */
    if (!DHT11_WaitLevel(1, DHT11_TIMEOUT_RESP)) { goto exit; }
    /* 等 80us 高结束, 之后就是 40bit 数据 */
    if (!DHT11_WaitLevel(0, DHT11_TIMEOUT_RESP)) { goto exit; }

    /* --- 3. 收 40bit: 每 bit 先 50us 低, 再拉高, 按高电平宽度判 0/1 --- */
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 8; j++) {
            if (!DHT11_WaitLevel(1, DHT11_TIMEOUT_LOW)) { goto exit; }   /* 等上升沿 */
            width = DHT11_MeasureHighUs(DHT11_TIMEOUT_HIGH);
            buf[i] = (uint8_t)((buf[i] << 1) | (width > DHT11_ONE_BIT_TH_US ? 1U : 0U));
        }
    }
    ok = 1;

exit:
    __enable_irq();
    DHT11_PinOutput();
    DHT11_HIGH();                                   /* 释放总线, 回到空闲 */

    if (ok && (buf[4] == (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]))) {
        data->humidity    = buf[0];
        data->temperature = buf[2];
        return 1;
    }
    return 0;
}
