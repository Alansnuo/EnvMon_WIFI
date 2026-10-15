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
#include "MyI2C.h"
#include "bsp_delay.h"

#define BH1750_CONV_MS   180U           /* H 分辨率一次转换最长 180ms */

/* 软件 I2C 原语已抽到 BSP/MyI2C: MyI2C_Start/Stop/SendByte/RecvByte */



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
    /* 总线引脚 PB6/PB7 已由 CubeMX 配好(开漏输出), 这里不再重配 */

    (void)BH1750_WriteCmd(0x01);        /* 上电, 做一次即可 */
    BSP_DelayMs(10);
}

uint8_t BH1750_ReadLuxChecked(float *lux)
{
    uint16_t raw;
    uint8_t  ok = 0U;

    if (lux == NULL) { return 0U; }

    *lux = 0.0f;

    ok = BH1750_WriteCmd(0x10);         /* 连续 H 分辨率模式, 同时启动转换 */

    /* 等转换完成. 用 BSP_DelayMs 而不是 HAL_Delay: 任务里会挂起让出 CPU.
     * 不要加临界区: I2C 允许 SCL 低电平任意长, 被任务切换打断是安全的. */
    BSP_DelayMs(BH1750_CONV_MS);

    MyI2C_Start();
    /* 返回值约定相反, 别写混: WriteCmd 返回 1 = 成功, SendByte 返回 0 = ACK */
    if (MyI2C_SendByte(BH1750_ADDR_W | 0x01U) != 0U) { ok = 0U; }
    raw  = (uint16_t)((uint16_t)MyI2C_RecvByte(1U) << 8);  /* 高字节 */
    raw |= (uint16_t)MyI2C_RecvByte(0U);                   /* 低字节, 末字节发 NACK */
    MyI2C_Stop();

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
