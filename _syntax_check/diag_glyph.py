"""Render glyphs exactly the way the driver sends them to the SSD1306.

Driver (OLED_ShowChinese, same as the reference project):
    page A  = 2*(Line-1)     <- Hzk1[i][0:16]
    page B  = 2*(Line-1)+1   <- Hzk1[i][16:32]
In each page, byte k = column k (of 16), and within that byte bit0 is the TOP
row of the page and bit7 the bottom row. So a page is 16 columns x 8 rows.

If both pages carry real ink, the cell is a genuine 16x16 glyph.
If only the first page's ink is meaningful, the cell is really 16x8.
"""
import re
import sys
sys.stdout.reconfigure(encoding="utf-8")

SRC = r"E:\39.STM32智能家居语音系统(ASRPRO版) (2)\1.源代码（面包板可用）\Hardware\OLED_Font.h"
raw = open(SRC, "rb").read().decode("latin-1")

seg = raw[raw.index("Hzk1"):]
eq = seg.index("=")
o = seg.index("{", eq)
d, c = 0, None
for p in range(o, len(seg)):
    if seg[p] == "{":
        d += 1
    elif seg[p] == "}":
        d -= 1
        if d == 0:
            c = p
            break
body = seg[o + 1:c]
blocks, d, s = [], 0, None
for p, ch in enumerate(body):
    if ch == "{":
        if d == 0:
            s = p + 1
        d += 1
    elif ch == "}":
        d -= 1
        if d == 0:
            blocks.append(body[s:p])
glyphs = [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", b)] for b in blocks]

NAMES = ["WEN", "SHI", "DU", "GUANG", "ZHAO", "QIANG", "DU2", "YAN", "WU", "NONG",
         "DU3", "XI", "TONG", "MO", "SHI2", "SHOU", "DONG", "ZI", "DONG2", "XI2",
         "TONG2", "YU", "ZHI", "SHE", "ZHI2", "JIE", "MIAN", "HUO", "YAN2", "ICON"]


def page_rows(page):
    """16 columns x 8 rows, bit0 = top row of the page"""
    return ["".join("#" if (page[col] >> bit) & 1 else "." for col in range(16))
            for bit in range(8)]


def show(idx):
    g = glyphs[idx]
    print("=== [%d] %s ===" % (idx, NAMES[idx]))
    a = page_rows(g[0:16])
    b = page_rows(g[16:32])
    for r in range(8):
        print("  %-16s | %s" % (a[r], b[r]))
    print("  page A ink=%d  page B ink=%d" % (
        sum(bin(x).count("1") for x in g[0:16]),
        sum(bin(x).count("1") for x in g[16:32])))
    print()


for i in (0, 2, 3):
    show(i)

print("=== ink per page, all 30 glyphs ===")
print("  idx name     pageA pageB")
allzero = 0
for i, g in enumerate(glyphs):
    a = sum(bin(x).count("1") for x in g[0:16])
    b = sum(bin(x).count("1") for x in g[16:32])
    if a == 0 or b == 0:
        allzero += 1
    print("  %2d  %-7s %5d %5d %s" % (i, NAMES[i], a, b,
                                      "  <-- one page EMPTY" if (a == 0 or b == 0) else ""))
print("\n  glyphs with an empty page:", allzero, "of", len(glyphs))
