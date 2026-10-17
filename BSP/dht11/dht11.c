/* DHT11 单总线驱动 (STM32F1 + HAL).
 * 时序: 拉低>=18ms → 应答80us×2 → 40bit 数据(高电平宽度判 0/1).
 * 微秒时基用 BSP_GetUs()/BSP_DelayUs() (TIM2), 本文件不另占定时器. */

#include "dht11.h"
#include "bsp_delay.h"

/* ========================= 总线电平操作 ========================= */
#define DHT11_HIGH()          HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET)    /* 释放总线 */
#define DHT11_LOW()           HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET)  /* 拉低总线 */
#define DHT11_LEVEL()         HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN)

/* ========================= 超时 (us) ========================= */
#define DHT11_TIMEOUT_RESP    200U                  /* 等从机应答 */
#define DHT11_TIMEOUT_LOW     100U                  /* 等每 bit 的低电平结束 */
#define DHT11_TIMEOUT_HIGH    150U                  /* 量高电平宽度的上限 */
#define DHT11_ONE_BIT_TH_US   45U                   /* >45us 判为 1 (0=26~28us, 1=70us) */

/* ========================= 引脚方向切换 ========================= */
/* 输出开漏 10MHz / 输入上拉 (DHT11 需外部 4.7k~10k 上拉) */
static void DHT11_PinOutput(void)
{
    GPIO_InitTypeDef s = {0};
    s.Pin   = DHT11_PIN;
    s.Mode  = GPIO_MODE_OUTPUT_OD;
    s.Pull  = GPIO_NOPULL;
    s.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(DHT11_PORT, &s);
}

static void DHT11_PinInput(void)
{
    GPIO_InitTypeDef s = {0};
    s.Pin   = DHT11_PIN;
    s.Mode  = GPIO_MODE_INPUT;
    s.Pull  = GPIO_PULLUP;                          /* HAL 内部先置 ODR=1 再切输入 */
    HAL_GPIO_Init(DHT11_PORT, &s);
}

/* ===== 等电平 / 测电平宽度 ===== 用 BSP_GetUs() 读 TIM2, 16 位回绕由无符号相减处理 */

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
    /* 用 BSP_DelayMs: 任务中挂起 20ms 让出 CPU, 电平由硬件保持; 须在关中断之外调用 */
    BSP_DelayMs(20);
    DHT11_PinInput();                               /* 先切输入+上拉 */
    BSP_DelayUs(30);

    /* --- 2. 从机应答 (整段位流约 4ms 不可打断) --- */
    __disable_irq(); /* 关中断, 避免被任务切换打断, 导致后续时序判错 */

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
    __enable_irq();/* 开中断 */
    DHT11_PinOutput();
    DHT11_HIGH();                                   /* 释放总线, 回到空闲 */

    if (ok && (buf[4] == (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]))) {
        /* 湿度 0 或温度 >50 判为脏数据 (受干扰时可能全 0 且校验和恰好通过), 丢弃 */
        if (buf[0] == 0U || buf[2] > 50U) { return 0; }

        /* DHT11 分辨率 1%RH / 1℃, buf[1]/buf[3] 小数位恒为 0, 不入结构体 */
        data->humidity    = buf[0];
        data->temperature = buf[2];
        return 1;
    }
    return 0;
}
