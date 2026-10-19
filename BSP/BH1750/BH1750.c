/* BH1750 光照传感器 (GY-302) 软件 I2C 驱动 (STM32F1 + HAL).
 * 时序: 上电(0x01) → H 分辨率模式(0x10) → 等 180ms → 读 2 字节 → 除1.2.  */

#include "BH1750.h"
#include "MyI2C.h"
#include "bsp_delay.h"

#define BH1750_CONV_MS   180U           /* H 分辨率一次转换最长 180ms */

/* 发一条单字节命令. 返回 1 = 地址和命令都被应答 */
static uint8_t BH1750_WriteCmd(uint8_t cmd)
{
    uint8_t ack;

    MyI2C_Start();
    ack  = MyI2C_SendByte(BH1750_ADDR_W);
    ack |= MyI2C_SendByte(cmd);
    MyI2C_Stop();

    return (uint8_t)(ack == 0U);
}

void BH1750_Init(void)
{
    (void)BH1750_WriteCmd(0x01);        /* 上电, 做一次即可 */
    BSP_DelayMs(10);
}

uint8_t BH1750_ReadLux(float *lux)
{
    uint16_t raw;
    uint8_t  ok = 0U;

    if (lux == NULL) { return 0U; }

    *lux = 0.0f;

    ok = BH1750_WriteCmd(0x10);         /* 连续 H 分辨率模式, 同时启动转换 */

    /* 等转换完成; 用 BSP_DelayMs 让出 CPU, 无需临界区 (SCL 可长时间拉低) */
    BSP_DelayMs(BH1750_CONV_MS);

    MyI2C_Start();
    /* WriteCmd 返回 1=成功, SendByte 返回 0=ACK */
    if (MyI2C_SendByte(BH1750_ADDR_R) != 0U) { ok = 0U; }
    raw  = (uint16_t)((uint16_t)MyI2C_RecvByte(1U) << 8);  /* 高字节 */
    raw |= (uint16_t)MyI2C_RecvByte(0U);                   /* 低字节, 末字节发 NACK */
    MyI2C_Stop();

    *lux = (float)raw / 1.2f;

    /* raw 全 1 ，0xFFFF = 65535，65535 / 1.2 ≈ 54612 lx = 器件无应答，*/
    if (raw == 0xFFFFU) { ok = 0U; }  

    return ok;
}
