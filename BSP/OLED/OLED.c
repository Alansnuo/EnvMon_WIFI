/* OLED (SSD1306 128x64) 软件 I2C 驱动; 开漏, 主机单向推数据不应答. */

#include "OLED.h"
#include "OLED_Font.h"
#include "bsp_delay.h"

#define OLED_HALF_US   5U             /* I2C 半周期, 约 100kHz */

#define SCL_H()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_RESET)

/* 起始: SCL 高时 SDA 下降沿 */
static void OLED_I2C_Start(void)
{
    SDA_H();
    SCL_H();
    BSP_DelayUs(OLED_HALF_US);

    SDA_L();
    BSP_DelayUs(OLED_HALF_US);

    SCL_L();
    BSP_DelayUs(OLED_HALF_US);
}

/* 停止: SCL 高时 SDA 上升沿 */
static void OLED_I2C_Stop(void)
{
    SDA_L();
    BSP_DelayUs(OLED_HALF_US);

    SCL_H();
    BSP_DelayUs(OLED_HALF_US);

    SDA_H();
    BSP_DelayUs(OLED_HALF_US);
}

/* 发一字节, 不读应答 */
static void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (Byte & 0x80) { SDA_H(); } else { SDA_L(); }   /* 高位先发 */
        Byte <<= 1U;
        BSP_DelayUs(OLED_HALF_US);
        SCL_H();
        BSP_DelayUs(OLED_HALF_US);
        SCL_L();
    }
    /* 第 9 个时钟: 放开 SDA, 不采样应答 */
    SDA_H();
    BSP_DelayUs(OLED_HALF_US);
    SCL_H();
    BSP_DelayUs(OLED_HALF_US);
    SCL_L();
    BSP_DelayUs(OLED_HALF_US);
}

/**
  * @brief  OLED 写命令
  * @param  Command 命令
  */
static void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_ADDR_W);     /* 从机地址 + 写 */
    OLED_I2C_SendByte(0x00);            /* 控制字节: Co=0, D/C=0, 命令 */
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}

/**
  * @brief  OLED 写数据
  * @param  Data 数据
  */
static void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_ADDR_W);     /* 从机地址 + 写 */
    OLED_I2C_SendByte(0x40);            /* 控制字节: Co=0, D/C=1, 数据 */
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
}

/**
  * @brief  OLED 设置光标位置
  * @param  Y 页, 0~7
  * @param  X 列, 0~127
  */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);                    /* 设置Y位置 */
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));    /* 设置X位置高4位 */
    OLED_WriteCommand(0x00 | (X & 0x0F));            /* 设置X位置低4位 */
}

/**
  * @brief  OLED 清屏
  */
void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for(i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

/**
  * @brief  OLED 显示一个字符
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  Char ASCII 可见字符
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);        /* 设置光标位置在上半部分 */
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);          /* 显示上半部分内容 */
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);   /* 设置光标位置在下半部分 */
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);      /* 显示下半部分内容 */
    }
}

/**
  * @brief  OLED 显示字符串
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  String ASCII 字符串
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}

/**
  * @brief  X 的 Y 次方
  */
static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--)
    {
        Result *= X;
    }
    return Result;
}

/**
  * @brief  OLED 显示十进制无符号数
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  Number 数值
  * @param  Length 位数, 1~10
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

/**
  * @brief  OLED 显示十进制带符号数
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  Number 数值
  * @param  Length 位数, 1~10
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t Number1;
    if (Number >= 0)
    {
        OLED_ShowChar(Line, Column, '+');
        Number1 = Number;
    }
    else
    {
        OLED_ShowChar(Line, Column, '-');
        Number1 = -Number;
    }
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

/**
  * @brief  OLED 显示十六进制数
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  Number 数值
  * @param  Length 位数, 1~8
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i, SingleNumber;
    for (i = 0; i < Length; i++)
    {
        SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
        if (SingleNumber < 10)
        {
            OLED_ShowChar(Line, Column + i, SingleNumber + '0');
        }
        else
        {
            OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
        }
    }
}

/**
  * @brief  OLED 显示二进制数
  * @param  Line 行, 1~4
  * @param  Column 列, 1~16
  * @param  Number 数值
  * @param  Length 位数, 1~16
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
    }
}

/**
  * @brief  OLED 初始化
  */
void OLED_Init(void)
{
    uint32_t i, j;

    for (i = 0; i < 1000; i++)           /* 上电延时 */
    {
        for (j = 0; j < 1000; j++);
    }

    OLED_WriteCommand(0xAE);    /* 关闭显示 */

    OLED_WriteCommand(0xD5);    /* 设置显示时钟分频比/振荡器频率 */
    OLED_WriteCommand(0x80);

    OLED_WriteCommand(0xA8);    /* 设置多路复用率 */
    OLED_WriteCommand(0x3F);

    OLED_WriteCommand(0xD3);    /* 设置显示偏移 */
    OLED_WriteCommand(0x00);

    OLED_WriteCommand(0x40);    /* 设置显示开始行 */

    OLED_WriteCommand(0xA1);    /* 设置左右方向，0xA1正常 0xA0左右反置 */

    OLED_WriteCommand(0xC8);    /* 设置上下方向，0xC8正常 0xC0上下反置 */

    OLED_WriteCommand(0xDA);    /* 设置COM引脚硬件配置 */
    OLED_WriteCommand(0x12);

    OLED_WriteCommand(0x81);    /* 设置对比度控制 */
    OLED_WriteCommand(0xCF);

    OLED_WriteCommand(0xD9);    /* 设置预充电周期 */
    OLED_WriteCommand(0xF1);

    OLED_WriteCommand(0xDB);    /* 设置VCOMH取消选择级别 */
    OLED_WriteCommand(0x30);

    OLED_WriteCommand(0xA4);    /* 设置整个显示打开/关闭 */

    OLED_WriteCommand(0xA6);    /* 设置正常/倒转显示 */

    OLED_WriteCommand(0x8D);    /* 设置充电泵 */
    OLED_WriteCommand(0x14);

    OLED_WriteCommand(0xAF);    /* 开启显示 */

    OLED_Clear();                /* OLED清屏 */
}
