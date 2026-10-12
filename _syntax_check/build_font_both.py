"""Put the reference project's Hzk1 back as the active table, keep both.

Layout produced in BSP/OLED/OLED_Font.h:

    #ifndef HZK1_USE_REFERENCE_FONT      <- 默认: 重新生成的表
    const uint8_t Hzk1[30][32] = { ...generated... };
    #endif
    #ifdef  HZK1_USE_REFERENCE_FONT      <- 取消注释即切回参考工程那张
    const uint8_t Hzk1[30][32] = { ...reference... };
    #endif

Both tables only exist inside OLED.c (the single translation unit allowed to
include this header), so no duplicate-symbol risk is introduced.
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

OUT = r"E:\EnvMon_WIFI\_syntax_check\oled_font_both.h"
REF = r"E:\39.STM32智能家居语音系统(ASRPRO版) (2)\1.源代码（面包板可用）\Hardware\OLED_Font.h"
GEN = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"

NAMES = ["WEN", "SHI", "DU", "GUANG", "ZHAO", "QIANG", "DU2", "YAN", "WU", "NONG",
         "DU3", "XI", "TONG", "MO", "SHI2", "SHOU", "DONG", "ZI", "DONG2", "XI2",
         "TONG2", "YU", "ZHI", "SHE", "ZHI2", "JIE", "MIAN", "HUO", "YAN2", "JINGGAO"]
CHARS = ["温", "湿", "度", "光", "照", "强", "度", "烟", "雾", "浓",
         "度", "系", "统", "模", "式", "手", "动", "自", "动", "系",
         "统", "阈", "值", "设", "置", "界", "面", "火", "焰", "警"]


def parse(path):
    src = open(path, "r", encoding="utf-8").read()
    m = re.search(r"Hzk1\s*(\[[^\]]*\])+\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit("no Hzk1 array in %s" % path)
    rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?\s*){32})\}", m.group(2))
    return [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", r)] for r in rows]


def emit(rows, tag):
    lines = []
    for i, g in enumerate(rows):
        lines.append("    {%s},   /* [%2d] %-7s %s */"
                     % (",".join("0x%02X" % b for b in g), i, NAMES[i], CHARS[i]))
    return "\n".join(lines)


ref = parse(REF)
# hzk1_generated.txt 是裸的行 (没有数组声明), 所以直接按行解析
gen = [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", ln)]
       for ln in open(GEN, encoding="utf-8").read().split("\n") if ln.strip()]
assert len(ref) == 30, len(ref)
assert len(gen) == 30, len(gen)
for g in gen:
    assert len(g) == 32, len(g)
if ref == gen:
    sys.exit("reference and generated tables are identical")

HEADER = '''/* ============================================================================
 * Hzk1 —— 16x16 汉字字模, 共 30 个
 *
 * 这里同时放了**两张表**, 用编译开关二选一:
 *
 *   默认 (不定义 HZK1_USE_REFERENCE_FONT)  ->  Hzk1_generated, 重新生成的
 *   定义   HZK1_USE_REFERENCE_FONT        ->  Hzk1_reference, 参考工程原表
 *
 *   切换方法: 在 MDK 的 Options for Target -> C/C++ -> Define 里加上
 *             HZK1_USE_REFERENCE_FONT
 *             (或者在本文件开头 #define HZK1_USE_REFERENCE_FONT)
 *
 * 为什么留两张: 参考工程那张的笔画偏细、还有几组下标逐字节重复
 *   (2/6/10 都是"度", 11/19 都是"系", 12/20 都是"统", 16/18 都是"动");
 *   重新生成的那张密度均匀。但实测两者对"度"这个字都是 16 行 / 76 墨点,
 *   没有差别 —— 所以到底哪张在板上更好看, 得你自己上板比。
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
 *    也就是 SSD1306 的页格式: 每个字节是"某一列的 8 个竖像素", 低位在上。
 *
 * ⚠️⚠️ 本文件里的 Hzk1 / OLED_F8x16 是**定义**, 不是 extern 声明。
 *    所以**只允许 BSP/OLED/OLED.c 一个 .c 包含本文件**。
 *    要是第二个 .c 也 include 进来, 链接就会报:
 *        Error: L6200E: Symbol Hzk1 multiply defined (by oled.o and xxx.o)
 *    需要字形下标的地方(比如 APP/task_oled.c)请**自己写一份 enum**,
 *    不要为了拿几个下标就把整张表复制一份进目标文件。
 * ========================================================================== */
'''

src = open(r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h", "r", encoding="utf-8").read()

# swap the provenance comment
cstart = src.index("/* ============================================================================\n * Hzk1")
cend = src.index(" * ========================================================================== */", cstart)
cend = src.index("\n", cend) + 1
src = src[:cstart] + HEADER + src[cend:]

# replace the single table with two, behind switches
m = re.search(r"(const uint8_t Hzk1\[30\]\[32\]=\s*\{\s*\n)(.*?)(\n\};)", src, re.S)
if not m:
    sys.exit("could not locate the active Hzk1 array")

two = (
    "#ifndef HZK1_USE_REFERENCE_FONT      /* 默认: 重新生成的表 */\n"
    "const uint8_t Hzk1[30][32]=\n{\n" + emit(gen, "gen") + "\n};\n"
    "#endif\n"
    "\n"
    "#ifdef  HZK1_USE_REFERENCE_FONT      /* 参考工程原表 */\n"
    "const uint8_t Hzk1[30][32]=\n{\n" + emit(ref, "ref") + "\n};\n"
    "#endif\n"
)
src = src[:m.start()] + two + src[m.end():]

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write(src)

print("wrote", OUT)
print("  size        :", len(src.encode("utf-8")))
print("  Hzk1 tables :", len(re.findall(r"const uint8_t Hzk1\[30\]\[32\]", src)))
print("  glyph rows  :", len(re.findall(r"^\s+\{0x", src, re.M)))
print("  switches    :", "#ifndef HZK1_USE_REFERENCE_FONT" in src,
      "/", "#ifdef  HZK1_USE_REFERENCE_FONT" in src)
