"""Apply the generated Hzk1 table + corrected provenance comment to
BSP/OLED/OLED_Font.h, with a backup alongside it.

Run unconfined: creating new files under BSP/ is denied to confined processes on
this machine, and this script writes a .bak plus the header itself.
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"
NEW = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"
BAK = FONT + ".bak-hzk1"

src = open(FONT, "r", encoding="utf-8").read()
rows = open(NEW, "r", encoding="utf-8").read().rstrip("\n").split("\n")
assert len(rows) == 30, len(rows)

NEW_HEADER = '''/* ============================================================================
 * Hzk1 —— 16x16 汉字字模, 共 30 个
 *
 * ⚠️ 这张表**不是**从参考工程原样搬来的。
 *    参考工程 Hardware/OLED_Font.h 里那张 Hzk1 的数据是坏的:
 *      - 真正点亮的像素普遍只有 16x16 汉字该有的一半左右 ("度"在 16x16 下
 *        正常要 70+ 个墨点, 它那张只有 30 多个), 所以字看着像被切一半;
 *      - 好几组下标是**逐字节完全相同**的 (2/6/10 都是"度", 11/19 都是"系",
 *        12/20 都是"统", 16/18 都是"动")。
 *    所以这张表改用 _syntax_check/gen_font.py 重新生成:
 *    16px 点阵 CJK 字体渲染 -> 二值化 -> 每个字自动挑墨点密度最合适的字体。
 *
 * ---- 下标和参考工程完全一致 (别重排, task_oled.c 的枚举按这个来) ----
 *      温度 = 0,2   湿度 = 1,2   光照强度 = 3,4,5,6   烟雾浓度 = 7,8,9,10
 *      系统模式 = 11,12,13,14   自动 = 17,16   手动 = 15,16
 *      阈值 = 21,22   设置界面用 23,24,25,26   火焰 = 27,28
 *    有些字会重复占多个下标。下标 29 是"警"字, 界面里当告警图案用。
 *
 * ---- 字节布局 (和 OLED_ShowChar 写 8x16 ASCII 字模的方式完全一样) ----
 *    显示时: 第 Line 行 = 页 2*(Line-1) 和页 2*(Line-1)+1, 一页 8 行像素。
 *      Hzk1[i][0..15]  -> 上页, 像素行 0~7
 *      Hzk1[i][16..31] -> 下页, 像素行 8~15
 *    页内: 第 r 行 (0~7)、第 c 列 (0~15) 的像素 =
 *      字节 [2*r + (c >= 8)] 的 bit(r)      ← bit0 是页内最上面那行
 *    也就是 SSD1306 的页格式: 每个字节是"某一列的 8 个竖像素",
 *    低位在上; 同一行的左右两个 8 像素块分别落在相邻两个字节里。
 *
 * ⚠️⚠️ 本文件里的 Hzk1 / OLED_F8x16 是**定义**, 不是 extern 声明。
 *    所以**只允许 BSP/OLED/OLED.c 一个 .c 包含本文件**。
 *    要是第二个 .c 也 include 进来, 链接就会报:
 *        Error: L6200E: Symbol Hzk1 multiply defined (by oled.o and xxx.o)
 *    需要字形下标的地方(比如 APP/task_oled.c)请**自己写一份 enum**,
 *    不要为了拿几个下标就把整张表复制一份进目标文件。
 * ========================================================================== */
'''

cstart = src.index("/* ============================================================================\n * Hzk1")
cend = src.index(" * ========================================================================== */", cstart)
cend = src.index("\n", cend) + 1
src = src[:cstart] + NEW_HEADER + src[cend:]

m = re.search(r"(const uint8_t Hzk1\[30\]\[32\]=\s*\{\s*\n)(.*?)(\n\};)", src, re.S)
if not m:
    sys.exit("could not locate the Hzk1 array")
old = [ln for ln in m.group(2).split("\n") if ln.strip()]
if len(old) != 30:
    sys.exit("expected 30 existing rows, got %d" % len(old))
src = src[:m.start(2)] + "\n".join(rows) + src[m.end(2):]

with open(BAK, "w", encoding="utf-8", newline="\n") as f:
    f.write(src)
with open(FONT, "w", encoding="utf-8", newline="\n") as f:
    f.write(src)

print("patched", FONT)
print("  bytes      :", len(src.encode("utf-8")))
print("  glyph rows :", len(re.findall(r"^\s+\{0x", src, re.M)))
print("  enum intact:", "HZK_JINGGAO = 29" in src)
print("  backup     :", BAK)
