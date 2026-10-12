/* ============================================================================
 * BH1750 光照传感器 (GY-302) 软件 I2C 驱动 —— STM32F1 + HAL
 *
 * 时序: 上电(0x01) → H 分辨率模式(0x10) → 等 180ms → 读 2 字节 → /1.2
 *
 * 不用硬件 I2C: F1 的硬件 I2C 有死锁 errata, 用两个 GPIO 打时序更稳.
 * 两个 IO 都配成开漏: 写 1 = 放开总线(靠 GY-302 上的 4.7k 拉高), 写 0 = 拉低.
 * 开漏下从机应答不会和主机对顶; 且 IDR 读到的就是总线真实电平,
 * 所以读总线不用像参考工程那样在输入/输出模式之间来回切.
 * ========================================================================== */

#include "BH1750.h"
#include "bsp_delay.h"

#define BH1750_HALF_US   5U             /* I2C 半周期, 约 100kHz */
#define BH1750_CONV_MS   180U           /* H 分辨率一次转换最长 180ms */

#define SCL_H()   HAL_GPIO_WritePin(BH1750_SCL_PORT, BH1750_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(BH1750_SCL_PORT, BH1750_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(BH1750_SDA_PORT, BH1750_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(BH1750_SDA_PORT, BH1750_SDA_PIN, GPIO_PIN_RESET)
#define SDA_IN()  ((BH1750_SDA_PORT->IDR & BH1750_SDA_PIN) ? 1U : 0U)

/* 起始/停止由"SCL 为高时 SDA 跳变"定义, 不是靠电平高低. */

/* 起始: SCL 高时 SDA 下降沿 */
static void BH1750_Start(void)
{
    SDA_H();
    SCL_H();
    BSP_DelayUs(BH1750_HALF_US);

    SDA_L();
    BSP_DelayUs(BH1750_HALF_US);

    SCL_L();
    BSP_DelayUs(BH1750_HALF_US);
}

/* 停止: SCL 高时 SDA 上升沿 */
static void BH1750_Stop(void)
{
    SDA_L();
    BSP_DelayUs(BH1750_HALF_US);

    SCL_H();
    BSP_DelayUs(BH1750_HALF_US);

    SDA_H();
    BSP_DelayUs(BH1750_HALF_US);
}

/* 发一字节. 返回从机应答: 0 = ACK, 1 = NACK */
static uint8_t BH1750_SendByte(uint8_t dat)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++)
    {
        if (dat & 0x80U) { SDA_H(); } else { SDA_L(); }     /* 高位先发 */
        dat = (uint8_t)(dat << 1U);
        BSP_DelayUs(BH1750_HALF_US);
        SCL_H();
        BSP_DelayUs(BH1750_HALF_US);
        SCL_L();
    }

    /* 第 9 个时钟: 放开 SDA, 从机拉低表示应答 */
    SDA_H();
    BSP_DelayUs(BH1750_HALF_US);
    SCL_H();
    BSP_DelayUs(BH1750_HALF_US);
    ack = (uint8_t)SDA_IN();
    SCL_L();
    BSP_DelayUs(BH1750_HALF_US);

    return ack;
}

/* 收一字节. sendAck: 1 = 主机应答, 0 = 非应答(末字节) */
static uint8_t BH1750_RecvByte(uint8_t sendAck)
{
    uint8_t i;
    uint8_t dat = 0U;

    SDA_H();                            /* 读之前先放开 SDA */

    for (i = 0U; i < 8U; i++)
    {
        BSP_DelayUs(BH1750_HALF_US);
        SCL_H();
        BSP_DelayUs(BH1750_HALF_US);    /* 高电平中点采样, 数据已稳定 */
        dat = (uint8_t)((dat << 1U) | SDA_IN());
        SCL_L();
    }

    if (sendAck) { SDA_L(); } else { SDA_H(); }
    BSP_DelayUs(BH1750_HALF_US);
    SCL_H();
    BSP_DelayUs(BH1750_HALF_US);
    SCL_L();
    SDA_H();
    BSP_DelayUs(BH1750_HALF_US);

    return dat;
}

/* 发一条单字节命令. 返回 1 = 地址和命令都被应答 */
static uint8_t BH1750_WriteCmd(uint8_t cmd)
{
    uint8_t ack;

    BH1750_Start();
    ack  = BH1750_SendByte(BH1750_ADDR_W);
    ack |= BH1750_SendByte(cmd);
    BH1750_Stop();

    return (uint8_t)(ack == 0U);
}

void BH1750_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();       /* 幂等, 不依赖 MX_GPIO_Init 的调用顺序 */

    gpio.Pin   = BH1750_SCL_PIN | BH1750_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏, 上拉由 GY-302 提供 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;  /* 软件 I2C 靠翻脚速度 */
    HAL_GPIO_Init(BH1750_SCL_PORT, &gpio);

    SCL_H();
    SDA_H();                            /* 空闲态: 两根线都放开 */

    (void)BH1750_WriteCmd(0x01);        /* 上电, 做一次即可 */
    BSP_DelayMs(10);
}

uint8_t BH1750_ReadLuxChecked(float *lux)
{
    uint16_t raw;
    uint8_t  ok;

    if (lux == NULL) { return 0U; }

    *lux = 0.0f;

    ok = BH1750_WriteCmd(0x10);         /* 连续 H 分辨率模式, 同时启动转换 */

    /* 等转换完成. 用 BSP_DelayMs 而不是 HAL_Delay: 任务里会挂起让出 CPU.
     * 不要加临界区: I2C 允许 SCL 低电平任意长, 被任务切换打断是安全的. */
    BSP_DelayMs(BH1750_CONV_MS);

    BH1750_Start();
    /* 返回值约定相反, 别写混: WriteCmd 返回 1 = 成功, SendByte 返回 0 = ACK */
    if (BH1750_SendByte(BH1750_ADDR_W | 0x01U) != 0U) { ok = 0U; }
    raw  = (uint16_t)((uint16_t)BH1750_RecvByte(1U) << 8);  /* 高字节 */
    raw |= (uint16_t)BH1750_RecvByte(0U);                   /* 低字节, 末字节发 NACK */
    BH1750_Stop();

    *lux = (float)raw / 1.2f;

    /* raw 全 1 = 器件没应答, 换算出来刚好 54612, 看着像正常值, 必须单独挑出来 */
    if (raw == 0xFFFFU) { ok = 0U; }

    return ok;
}

float BH1750_ReadLux(void)
{
    float lux = 0.0f;

    (void)BH1750_ReadLuxChecked(&lux);
    return lux;
}
