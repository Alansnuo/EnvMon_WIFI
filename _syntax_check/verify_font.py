"""End-to-end check of the patched OLED_Font.h.

Reads the table straight out of the header, replays exactly what
OLED_ShowChinese sends to the SSD1306, and reconstructs the 16x16 bitmap that
the panel will therefore show. Then checks ink density and, above all, that
every glyph uses the full 16-row height - the reported symptom was a glyph
rendering as only its top half.
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"
# 真正的"旧表"是参考工程那张, 从原项目读。
# ⚠️ 两个都不要用:
#    - BSP/OLED/OLED_Font.h.bak-hzk1 —— 落盘时新表已生成, 里面其实是新表;
#    - git HEAD:BSP/OLED/OLED_Font.h —— 那版还没有 Hzk1 表(只有 ASCII 字模)。
OLD = r"E:\39.STM32智能家居语音系统(ASRPRO版) (2)\1.源代码（面包板可用）\Hardware\OLED_Font.h"

NAMES = ["WEN", "SHI", "DU", "GUANG", "ZHAO", "QIANG", "DU2", "YAN", "WU", "NONG",
         "DU3", "XI", "TONG", "MO", "SHI2", "SHOU", "DONG", "ZI", "DONG2", "XI2",
         "TONG2", "YU", "ZHI", "SHE", "ZHI2", "JIE", "MIAN", "HUO", "YAN2", "JINGGAO"]
CHARS = ["温", "湿", "度", "光", "照", "强", "度", "烟", "雾", "浓",
         "度", "系", "统", "模", "式", "手", "动", "自", "动", "系",
         "统", "阈", "值", "设", "置", "界", "面", "火", "焰", "警"]


def parse(path):
    """Read a Hzk1 table out of either our header (const uint8_t Hzk1[30][32])
    or the reference project's (const unsigned char Hzk1[][32])."""
    src = open(path, "r", encoding="utf-8").read()
    m = re.search(r"Hzk1\s*(\[[^\]]*\])+\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit("no Hzk1 array in %s" % path)
    rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?\s*){32})\}", m.group(2))
    out = []
    for r in rows:
        out.append([int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", r)])
    return out


def panel_bitmap(g):
    """Replay the driver: two pages of 8 rows; within a page byte c is column c
    and bit r is row r. Returns 16 rows of 16 bools."""
    rows = []
    for page in range(2):
        for r in range(8):
            rows.append([bool((g[page * 16 + c] >> r) & 1) for c in range(16)])
    return rows


def rows_used(rows):
    return sum(1 for r in rows if any(r))


new = parse(FONT)
old = parse(OLD)
print("parsed %d glyphs from the new header, %d from the reference project" % (len(new), len(old)))
assert len(new) == 30 and len(old) == 30
if new == old:
    sys.exit("new table is identical to the reference table - nothing was replaced!")

print("\n%-4s %-4s | %-22s | %-22s" % ("idx", "chr", "OLD rows/ink", "NEW rows/ink"))
print("-" * 66)
problems = []
for i in range(30):
    o_rows, n_rows = panel_bitmap(old[i]), panel_bitmap(new[i])
    o_used, n_used = rows_used(o_rows), rows_used(n_rows)
    o_ink = sum(1 for r in o_rows for c in r if c)
    n_ink = sum(1 for r in n_rows for c in r if c)
    flag = ""
    if n_used < 12:
        flag = "  <-- NEW glyph does not use the full height"
        problems.append((i, NAMES[i], n_used))
    if n_ink < 45:
        flag += "  <-- NEW glyph too thin"
        problems.append((i, NAMES[i], n_ink))
    print("%3d  %-4s | rows=%-2d ink=%-4d      | rows=%-2d ink=%-4d  %s"
          % (i, CHARS[i], o_used, o_ink, n_used, n_ink, flag))

print()
if problems:
    print("PROBLEMS:", len(problems))
    for p in problems:
        print("   ", p)
    sys.exit(1)

print("every glyph now uses >=12 of the 16 rows and carries >=45 ink pixels")

# show the reported offender
print("\n度 (index 2) as the panel will draw it:")
for r in panel_bitmap(new[2]):
    print("   " + "".join("#" if c else "." for c in r))
print("\n度 (index 2) from the OLD table, same reconstruction:")
for r in panel_bitmap(old[2]):
    print("   " + "".join("#" if c else "." for c in r))
