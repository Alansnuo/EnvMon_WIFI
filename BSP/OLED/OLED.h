#ifndef __OLED_H
#define __OLED_H

#include "main.h"
#include <stdint.h>

/* ============================================================================
 * OLED (SSD1306, 0.96寸 128x64) 软件 I2C 驱动 —— STM32F1 + HAL
 *
 * 与 BH1750 一样不用硬件 I2C: F1 的硬件 I2C 有死锁 errata, 用两个 GPIO
 * 打时序更稳. 两个 IO 都配开漏: 写 1 = 放开总线(靠模块上的 4.7k 拉高),
 * 写 0 = 拉低. SSD1306 不做应答采样, 单纯主机推数据, 时序要求宽松.
 *
 * 实际连线: PB8 = SCL, PB9 = SDA
 * ========================================================================== */

/* 引脚定义. 与板上实际接线一致, 改线只改这里. */
#define OLED_SCL_PORT    GPIOB
#define OLED_SCL_PIN     GPIO_PIN_8
#define OLED_SDA_PORT    GPIOB
#define OLED_SDA_PIN     GPIO_PIN_9

/* SSD1306 从机地址: 7 位地址 0x3C 左移一位 + 写方向 = 0x78.
 * 模块 SA0 脚接地时就是 0x3C. 若模块把 SA0 接 VCC, 改成 0x7A (7 位 0x3D). */
#define OLED_ADDR_W      0x78

/* 汉字字形下标 —— 和 OLED_Font.h 里 Hzk1[] 表的顺序一一对应, 不要重排。
 * 放在这里 (而不是 OLED_Font.h) 是因为 task_oled.c 不能 include OLED_Font.h:
 * Hzk1 在头文件里是**定义**不是声明, 两个 .c 都 include 会导致链接报
 * L6200E multiply defined。OLED.h 两边都 include, 枚举放这里大家都看得到。 */
enum {
    HZK_WEN = 0, HZK_SHI, HZK_DU, HZK_GUANG, HZK_ZHAO, HZK_QIANG, HZK_DU2,
    HZK_YAN, HZK_WU, HZK_NONG, HZK_DU3, HZK_XI, HZK_TONG, HZK_MO, HZK_SHI2,
    HZK_SHOU, HZK_DONG, HZK_ZI, HZK_DONG2, HZK_XI2, HZK_TONG2, HZK_YU,
    HZK_ZHI, HZK_SHE, HZK_ZHI2, HZK_JIE, HZK_MIAN, HZK_HUO, HZK_YAN2,
    HZK_JINGGAO = 29
};

/* 必须在 MX_GPIO_Init() 之后调用. 内部有 BSP_DelayMs, 调度器启动前调用也安全. */
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

/* ---- 汉字 (16x16), 字模在 OLED_Font.h 的 Hzk1[30][32] ----
 * 一个汉字 16 像素宽 = 占 2 列, 一行最多 8 个;
 * ASCII 是 8 像素宽 = 占 1 列, 一行 16 个。
 * 所以汉字和 ASCII 混排在同一行时, 两者的列号不能混着累加 —— 要分开算列。
 * 具体画法见 task_oled.c 各页面。 */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t num);
void OLED_ShowChineseString(uint8_t Line, uint8_t Column, const char *String);

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif /* __OLED_H */
