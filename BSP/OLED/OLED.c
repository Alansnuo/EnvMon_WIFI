/* ============================================================================
 * OLED (SSD1306, 0.96寸 128x64) 软件 I2C 驱动 —— STM32F1 + HAL
 *
 * 时序: 标准 I2C 起始/停止 + 单字节发送, SSD1306 不做应答采样
 *      (主机单向推数据, 第 9 个时钟主机自己放开 SDA 即可)
 *
 * 不用硬件 I2C: F1 的硬件 I2C 有死锁 errata, 两个 GPIO 打时序更稳.
 * 两个 IO 都配开漏: 写 1 = 放开总线(靠模块上的 4.7k 拉高), 写 0 = 拉低.
 * ========================================================================== */

#include "OLED.h"
#include "OLED_Font.h"
#include "bsp_delay.h"

#define OLED_HALF_US   5U             /* I2C 半周期, 约 100kHz */

#define SCL_H()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_RESET)

/* 起始/停止由"SCL 为高时 SDA 跳变"定义, 不是靠电平高低. */

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

/* 发一字节. SSD1306 单向收, 不读应答: 第 9 个时钟主机放开 SDA 即可. */
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

/* 引脚初始化. 幂等, 重调无害: 每次都重新配 GPIO + 复位总线. */
static void OLED_I2C_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();       /* 幂等, 不依赖 MX_GPIO_Init 的调用顺序 */

    gpio.Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏, 上拉由 OLED 模块提供 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;  /* 软件 I2C 靠翻脚速度 */
    HAL_GPIO_Init(OLED_SCL_PORT, &gpio);

    SCL_H();
    SDA_H();                            /* 空闲态: 两根线都放开 */
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
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
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
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
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);                    /* 设置Y位置 */
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));    /* 设置X位置高4位 */
    OLED_WriteCommand(0x00 | (X & 0x0F));            /* 设置X位置低4位 */
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
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
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
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
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
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
  * @brief  OLED显示一个汉字（16x16）
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~8 —— 汉字 16 像素宽，占两列
  * @param  num 字形下标，用 OLED_Font.h 里的 HZK_xxx 枚举
  * @retval 无
  * @note   参考工程同名函数的位置算法一致：上半页写 Hzk1[num][0..15]，
  *         下半页写 Hzk1[num][16..31]
  */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t num)
{
    uint8_t i;

    if ((Line < 1) || (Line > 4) || (Column < 1) || (Column > 8) || (num >= 30))
    {
        return;
    }

    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 16);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(Hzk1[num][i]);           /* 上半页 */
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 16);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(Hzk1[num][i + 16]);      /* 下半页 */
    }
}

/**
  * @brief  按 UTF-8 字符查字形下标，查不到返回 0xFF
  * @note   为什么不用"汉字在字符串里直接写"：
  *         OLED 的行是 16 列 x 8 像素，汉字是 16 像素宽 = 占 2 列，
  *         而 ASCII 是 8 像素宽 = 占 1 列。两者混在一行里没法用一个
  *         列号同时表示，所以行内位置由调用方按"汉字占几列"自己算。
  *         这个函数只负责把 UTF-8 汉字映射到 Hzk1 的下标。
  */
static uint8_t OLED_GlyphOfUtf8(const char *s)
{
    /* 界面用到的汉字。(UTF-8 三字节) -> Hzk1 下标 */
    static const struct { char c[4]; uint8_t g; } map[] = {
        { "\xE6\xB8\xA9", HZK_WEN  },   /* 温 */
        { "\xE6\xB9\xBF", HZK_SHI  },   /* 湿 */
        { "\xE5\xBA\xA6", HZK_DU   },   /* 度 */
        { "\xE5\x85\x89", HZK_GUANG},   /* 光 */
        { "\xE7\x85\xA7", HZK_ZHAO },   /* 照 */
        { "\xE5\xBC\xBA", HZK_QIANG},   /* 强 */
        { "\xE7\xB3\xBB", HZK_XI   },   /* 系 */
        { "\xE7\xBB\x9F", HZK_TONG },   /* 统 */
        { "\xE6\xA8\xA1", HZK_MO   },   /* 模 */
        { "\xE5\xBC\x8F", HZK_SHI2 },   /* 式 */
        { "\xE6\x89\x8B", HZK_SHOU },   /* 手 */
        { "\xE5\x8A\xA8", HZK_DONG },   /* 动 */
        { "\xE8\x87\xAA", HZK_ZI   },   /* 自 */
        { "\xE9\x98\x88", HZK_YU   },   /* 阈 */
        { "\xE5\x80\xBC", HZK_ZHI  },   /* 值 */
        { "\xE8\xAE\xBE", HZK_SHE  },   /* 设 */
        { "\xE7\xBD\xAE", HZK_ZHI2 },   /* 置 */
        { "\xE7\x95\x8C", HZK_JIE  },   /* 界 */
        { "\xE9\x9D\xA2", HZK_MIAN },   /* 面 */
        { "\xE7\x81\xAB", HZK_HUO  },   /* 火 */
        { "\xE7\x84\xB0", HZK_YAN2 },   /* 焰 */
    };
    uint8_t i;

    for (i = 0; i < (uint8_t)(sizeof(map) / sizeof(map[0])); i++)
    {
        if ((s[0] == map[i].c[0]) && (s[1] == map[i].c[1]) && (s[2] == map[i].c[2]))
        {
            return map[i].g;
        }
    }
    return 0xFF;
}

/**
  * @brief  显示一串汉字（UTF-8）
  * @param  Line 行位置 1~4
  * @param  Column 起始列 1~8，每个汉字占 2 列
  * @param  String 以 '\0' 结尾的 UTF-8 汉字串
  * @retval 无
  */
void OLED_ShowChineseString(uint8_t Line, uint8_t Column, const char *String)
{
    uint8_t i = 0;
    uint8_t g;

    while (String[i] != '\0')
    {
        g = OLED_GlyphOfUtf8(&String[i]);
        if (g == 0xFF)
        {
            i++;                                    /* 表里没有这个字: 跳过一字节 */
            continue;
        }

        OLED_ShowChinese(Line, Column, g);

        Column += 2;                                /* 一个汉字占两列 */
        if (Column > 8)
        {
            break;                                  /* 超出这一行就不再写 */
        }
        i += 3;                                     /* UTF-8 三字节 */
    }
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
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
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
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
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
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
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
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
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
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
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
    uint32_t i, j;

    for (i = 0; i < 1000; i++)           /* 上电延时 */
    {
        for (j = 0; j < 1000; j++);
    }

    OLED_I2C_Init();                     /* 端口初始化 */

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
