/* DHT11 单总线驱动 (STM32F1 + HAL).
 * 时序: 拉低>=18ms → 应答80us×2 → 40bit 数据(高电平宽度判 0/1).
 * 微秒时基用 BSP_GetUs()/BSP_DelayUs() (TIM2), 本文件不另占定时器. */

#include "dht11.h"
#include "bsp_delay.h"

/* ========================= 总线电平操作 ========================= */
/* F1 的 GPIO: MODE=01 输出10MHz, CNF=01 开漏; MODE=00 输入, CNF=10 上/下拉 */
#define DHT11_CONF_OUT_OD     0x5UL                 /* 通用开漏输出 */
#define DHT11_CONF_IN_PU      0x8UL                 /* 输入 + 上拉 */
#define DHT11_CR_SHIFT        ((DHT11_PIN_NUM & 7UL) * 4UL)

#define DHT11_HIGH()          (DHT11_PORT->BSRR = DHT11_PIN)    /* ODR=1: 释放总线 / 上拉 */
#define DHT11_LOW()           (DHT11_PORT->BRR  = DHT11_PIN)    /* ODR=0: 拉低总线 */
#define DHT11_LEVEL()         ((DHT11_PORT->IDR & DHT11_PIN) ? 1U : 0U)

/* ========================= 超时 (us) ========================= */
#define DHT11_TIMEOUT_RESP    200U                  /* 等从机应答 */
#define DHT11_TIMEOUT_LOW     100U                  /* 等每 bit 的低电平结束 */
#define DHT11_TIMEOUT_HIGH    150U                  /* 量高电平宽度的上限 */
#define DHT11_ONE_BIT_TH_US   45U                   /* >45us 判为 1 (0=26~28us, 1=70us) */

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

/* ===== 等电平 / 测电平宽度 ===== 用 BSP_GetUs() 读 TIM2, 16 位回绕靠无符号相减兜住 */

/* 等总线变成 level 电平, 超时返回 0 */
static uint8_t DHT11_WaitLevel(uint8_t level, uint16_t timeout_us)
{
    uint16_t start = BSP_GetUs();

    do {
        if (DHT11_LEVEL() == level) { return 1; }
    } while ((uint16_t)(BSP_GetUs() - start) < timeout_us);

    return 0;
}

/* 测高电平持续 us, 超时按 timeout_us 返回 */
static uint16_t DHT11_MeasureHighUs(uint16_t timeout_us)
{
    uint16_t start = BSP_GetUs();

    while (DHT11_LEVEL()) {
        if ((uint16_t)(BSP_GetUs() - start) >= timeout_us) { return timeout_us; }
    }

    return (uint16_t)(BSP_GetUs() - start);
}

/* ========================= 对外接口 ========================= */
uint8_t DHT11_Init(void)
{
    /* 先读一次拉起 us 时基, 避免在关中断位流里才初始化 */
    (void)BSP_GetUs();

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
    uint16_t width;

    if (data == NULL) { return 0; }

    /* --- 1. 起始信号: 拉低 20ms, 再释放 --- */
    DHT11_PinOutput();
    DHT11_LOW();
    /* 用 BSP_DelayMs: 任务里挂起 20ms 让出 CPU, 电平由硬件保持; 必须在关中断之外调用 */
    BSP_DelayMs(20);
    DHT11_PinInput();                               /* 先切输入+上拉 */
    BSP_DelayUs(30);

    /* --- 2. 从机应答 (整段位流约 4ms 不可打断) --- */
    __disable_irq();

    if (!DHT11_WaitLevel(0, DHT11_TIMEOUT_RESP)) { goto exit; }  /* 等 80us 低 */
    if (!DHT11_WaitLevel(1, DHT11_TIMEOUT_RESP)) { goto exit; }  /* 等 80us 低结束 */
    if (!DHT11_WaitLevel(0, DHT11_TIMEOUT_RESP)) { goto exit; }  /* 等 80us 高结束 */

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
        /* 湿度 0 或温度 >50 判为脏数据 (受干扰时可能全 0 且校验和碰巧对上), 丢弃 */
        if (buf[0] == 0U || buf[2] > 50U) { return 0; }

        data->humidity        = buf[0];
        data->temperature     = buf[2];
        /* 小数位: DHT11 恒为 0, 兼容品可能塞垃圾, 夹到 0~9 避免显示异常 */
        data->humidity_dec    = (buf[1] > 9U) ? 0U : buf[1];
        data->temperature_dec = (buf[3] > 9U) ? 0U : buf[3];
        return 1;
    }
    return 0;
}
