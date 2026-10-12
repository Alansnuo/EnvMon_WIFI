#ifndef __STUB_OLED_FONT_H
#define __STUB_OLED_FONT_H
#include <stdint.h>

/* 桩: 只保留 HZK_xxx 下标枚举。
 *
 * ⚠️ 真实 OLED_Font.h 里 Hzk1 / OLED_F8x16 是**定义**, 不是 extern 声明。
 *    这里**故意不声明它们** —— 谁要是 include 了这个桩还用那两个数组,
 *    编译就会报未声明; 而真实工程里"多个 .c 都 include 真头文件"会在**链接**
 *    阶段报 L6200E 重复定义 (oled.o 和 task_oled.o 都产出一份)。
 *    所以规则是: 字模表只让 BSP/OLED/OLED.c 一个人 include。 */
enum {
    HZK_WEN = 0, HZK_SHI, HZK_DU, HZK_GUANG, HZK_ZHAO, HZK_QIANG, HZK_DU2,
    HZK_YAN, HZK_WU, HZK_NONG, HZK_DU3, HZK_XI, HZK_TONG, HZK_MO, HZK_SHI2,
    HZK_SHOU, HZK_DONG, HZK_ZI, HZK_DONG2, HZK_XI2, HZK_TONG2, HZK_YU,
    HZK_ZHI, HZK_SHE, HZK_ZHI2, HZK_JIE, HZK_MIAN, HZK_HUO, HZK_YAN2,
    HZK_JINGGAO = 29
};

#endif
