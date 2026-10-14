#ifndef __STUB_OLED_H
#define __STUB_OLED_H

/* ============================================================================
 * 桩: 只暴露 BSP/OLED/OLED.h 的**公开接口**, 不引 main.h (避开 HAL)。
 *
 * ⚠️ 这个桩必须和真头文件保持同步!
 *    它排在 -I 列表最前面 (FreeRTOS 的 portmacro.h 是 ARMCC 专用语法,
 *    gcc 编不了, 所以必须用桩), 因此它会**遮住**真 OLED.h。
 *    桩一旦落后于真头文件, 检查就会误报 —— 真实发生过的例子:
 *    桩里没有 HZK_* 枚举, task_oled.c 用 HZK_WEN 就被报成 undeclared,
 *    而实际代码是好的。
 *
 * 改真实 OLED.h 的公开接口时, 记得同步这里。
 * ========================================================================== */

#include <stdint.h>

/* 引脚/地址那组宏 (OLED_SCL_PIN 等) 没有搬过来: 它们的值是 GPIOB/GPIO_PIN_8,
 * 要引 main.h 才认得, 而本桩是故意避开 HAL 的。检查也用不到它们。 */

/* 汉字字形下标 —— 必须和真 OLED.h 的 enum 逐字一致 (顺序 = Hzk1[] 表序) */
enum {
    HZK_WEN = 0, HZK_SHI, HZK_DU, HZK_GUANG, HZK_ZHAO, HZK_QIANG, HZK_DU2,
    HZK_YAN, HZK_WU, HZK_NONG, HZK_DU3, HZK_XI, HZK_TONG, HZK_MO, HZK_SHI2,
    HZK_SHOU, HZK_DONG, HZK_ZI, HZK_DONG2, HZK_XI2, HZK_TONG2, HZK_YU,
    HZK_ZHI, HZK_SHE, HZK_ZHI2, HZK_JIE, HZK_MIAN, HZK_HUO, HZK_YAN2,
    HZK_JINGGAO = 29
};

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t num);
void OLED_ShowChineseString(uint8_t Line, uint8_t Column, const char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif /* __STUB_OLED_H */
