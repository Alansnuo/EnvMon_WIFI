/* 只为验证字库表维度, 不参与固件 */
#include <stdint.h>
#include "OLED_Font.h"
#include <stddef.h>

/* 编译期断言: 表必须是 [30][32] */
typedef char assert_glyph_count[(sizeof(Hzk1) / sizeof(Hzk1[0]) == 30) ? 1 : -1];
typedef char assert_glyph_size[(sizeof(Hzk1[0]) == 32) ? 1 : -1];
typedef char assert_ascii_count[(sizeof(OLED_F8x16) / sizeof(OLED_F8x16[0]) == 95) ? 1 : -1];
typedef char assert_ascii_size[(sizeof(OLED_F8x16[0]) == 16) ? 1 : -1];

int font_table_dims_ok(void)
{
    return (int)(sizeof(Hzk1) / sizeof(Hzk1[0]));
}
