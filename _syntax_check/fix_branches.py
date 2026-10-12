"""Rebuild the two Hzk1 branches from their authoritative sources.

Branch 1 (#ifdef  HZK1_USE_REFERENCE_FONT, default ON) must hold the reference
project's table. Branch 2 (#ifndef, used when the switch is commented out) must
hold the generated table. The earlier edits left the two bodies swapped.
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"
REF = r"E:\39.STM32智能家居语音系统(ASRPRO版) (2)\1.源代码（面包板可用）\Hardware\OLED_Font.h"
GEN = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"

NAMES = ["WEN", "SHI", "DU", "GUANG", "ZHAO", "QIANG", "DU2", "YAN", "WU", "NONG",
         "DU3", "XI", "TONG", "MO", "SHI2", "SHOU", "DONG", "ZI", "DONG2", "XI2",
         "TONG2", "YU", "ZHI", "SHE", "ZHI2", "JIE", "MIAN", "HUO", "YAN2", "JINGGAO"]
CHARS = ["温", "湿", "度", "光", "照", "强", "度", "烟", "雾", "浓",
         "度", "系", "统", "模", "式", "手", "动", "自", "动", "系",
         "统", "阈", "值", "设", "置", "界", "面", "火", "焰", "警"]


def rows_of(text):
    return [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", r)]
            for r in re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?\s*){32})\}", text)]


ref = rows_of(open(REF, encoding="utf-8").read())
assert len(ref) == 30, len(ref)
gen = [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", ln)]
       for ln in open(GEN, encoding="utf-8").read().split("\n") if ln.strip()]
assert len(gen) == 30 and all(len(g) == 32 for g in gen)
assert ref != gen


def body(rows):
    out = []
    for i, g in enumerate(rows):
        out.append("    {%s},   /* [%2d] %-7s %s */"
                   % (",".join("0x%02X" % b for b in g), i, NAMES[i], CHARS[i]))
    return "\n".join(out)


src = open(FONT, encoding="utf-8").read()

# Replace the *bodies* only, keyed on the guard each one follows.
pat = re.compile(
    r"(#ifdef\s+HZK1_USE_REFERENCE_FONT[^\n]*\nconst uint8_t Hzk1\[30\]\[32\]=\s*\{\s*\n)"
    r"(.*?)"
    r"(\n\};)",
    re.S)
m = pat.search(src)
if not m:
    sys.exit("could not find branch 1 (the #ifdef one)")
src = src[:m.start(2)] + body(ref) + src[m.end(2):]

pat2 = re.compile(
    r"(#ifndef\s+HZK1_USE_REFERENCE_FONT[^\n]*\nconst uint8_t Hzk1\[30\]\[32\]=\s*\{\s*\n)"
    r"(.*?)"
    r"(\n\};)",
    re.S)
m2 = pat2.search(src)
if not m2:
    sys.exit("could not find branch 2 (the #ifndef one)")
src = src[:m2.start(2)] + body(gen) + src[m2.end(2):]

with open(FONT, "w", encoding="utf-8", newline="\n") as f:
    f.write(src)

print("rewrote both branches in", FONT)
print("  bytes      :", len(src.encode("utf-8")))
print("  glyph rows :", len(re.findall(r"^\s+\{0x", src, re.M)))
