/* ============================================================================
 * 通用软件 I2C 总线驱动 —— STM32F1 + HAL
 *
 * 从 BH1750 驱动里抽出的 Start/Stop/SendByte/RecvByte 原语.
 * 时序: 起始/停止由"SCL 为高时 SDA 跳变"定义, 不是靠电平高低.
 * 两个 IO 都配开漏: 写 1 = 放开总线, 写 0 = 拉低.
 * 引脚 PB6/PB7 由 CubeMX 配好(开漏输出), 本文件不再重配 GPIO.
 * ========================================================================== */

#include "MyI2C.h"
#include "bsp_delay.h"

#define MYI2C_HALF_US   5U              /* I2C 半周期, 约 100kHz */

#define SCL_H()   HAL_GPIO_WritePin(MYI2C_SCL_PORT, MYI2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(MYI2C_SCL_PORT, MYI2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(MYI2C_SDA_PORT, MYI2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(MYI2C_SDA_PORT, MYI2C_SDA_PIN, GPIO_PIN_RESET)
#define SDA_IN()  ((MYI2C_SDA_PORT->IDR & MYI2C_SDA_PIN) ? 1U : 0U)

/* 起始: SCL 高时 SDA 下降沿 */
void MyI2C_Start(void)
{
    SDA_H();
    SCL_H();
    BSP_DelayUs(MYI2C_HALF_US);

    SDA_L();
    BSP_DelayUs(MYI2C_HALF_US);

    SCL_L();
    BSP_DelayUs(MYI2C_HALF_US);
}

/* 停止: SCL 高时 SDA 上升沿 */
void MyI2C_Stop(void)
{
    SDA_L();
    BSP_DelayUs(MYI2C_HALF_US);

    SCL_H();
    BSP_DelayUs(MYI2C_HALF_US);

    SDA_H();
    BSP_DelayUs(MYI2C_HALF_US);
}

/* 发一字节. 返回从机应答: 0 = ACK, 1 = NACK */
uint8_t MyI2C_SendByte(uint8_t dat)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++)
    {
        if (dat & 0x80U) { SDA_H(); } else { SDA_L(); }     /* 高位先发 */
        dat = (uint8_t)(dat << 1U);
        BSP_DelayUs(MYI2C_HALF_US);
        SCL_H();
        BSP_DelayUs(MYI2C_HALF_US);
        SCL_L();
    }

    /* 第 9 个时钟: 放开 SDA, 从机拉低表示应答 */
    SDA_H();
    BSP_DelayUs(MYI2C_HALF_US);
    SCL_H();
    BSP_DelayUs(MYI2C_HALF_US);
    ack = (uint8_t)SDA_IN();
    SCL_L();
    BSP_DelayUs(MYI2C_HALF_US);

    return ack;
}

/* 收一字节. sendAck: 1 = 主机应答, 0 = 非应答(末字节) */
uint8_t MyI2C_RecvByte(uint8_t sendAck)
{
    uint8_t i;
    uint8_t dat = 0U;

    SDA_H();                            /* 读之前先放开 SDA */

    for (i = 0U; i < 8U; i++)
    {
        BSP_DelayUs(MYI2C_HALF_US);
        SCL_H();
        BSP_DelayUs(MYI2C_HALF_US);     /* 高电平中点采样, 数据已稳定 */
        dat = (uint8_t)((dat << 1U) | SDA_IN());
        SCL_L();
    }

    if (sendAck) { SDA_L(); } else { SDA_H(); }
    BSP_DelayUs(MYI2C_HALF_US);
    SCL_H();
    BSP_DelayUs(MYI2C_HALF_US);
    SCL_L();
    SDA_H();
    BSP_DelayUs(MYI2C_HALF_US);

    return dat;
}
