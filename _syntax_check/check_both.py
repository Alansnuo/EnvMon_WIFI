"""Verify which table ended up in each branch of OLED_Font.h."""
import re
import sys
sys.stdout.reconfigure(encoding="utf-8")

FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"
REF = r"E:\39.STM32智能家居语音系统(ASRPRO版) (2)\1.源代码（面包板可用）\Hardware\OLED_Font.h"
GEN = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"


def rows_of(text):
    return [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", r)]
            for r in re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?\s*){32})\}", text)]


src = open(FONT, encoding="utf-8").read()
# split at the second table's guard
i = src.index("#ifndef HZK1_USE_REFERENCE_FONT")
first = src[:i]
second = src[i:]
t1 = rows_of(first)[-30:]        # last 30 rows of the first branch
t2 = rows_of(second)[:30]        # first 30 rows of the second branch

ref = rows_of(open(REF, encoding="utf-8").read())
gen = [[int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", ln)]
       for ln in open(GEN, encoding="utf-8").read().split("\n") if ln.strip()]

print("first branch  == reference table ? ", t1 == ref)
print("first branch  == generated table ? ", t1 == gen)
print("second branch == reference table ? ", t2 == ref)
print("second branch == generated table ? ", t2 == gen)
print()
print("first branch  row0:", " ".join("%02X" % b for b in t1[0][:8]), "...")
print("reference     row0:", " ".join("%02X" % b for b in ref[0][:8]), "...")
print("second branch row0:", " ".join("%02X" % b for b in t2[0][:8]), "...")
print("generated     row0:", " ".join("%02X" % b for b in gen[0][:8]), "...")
print()

ok = (t1 == ref and t2 == gen)
print("EXPECTED: branch1 = reference (default), branch2 = generated")
print("RESULT:", "OK" if ok else "WRONG - the two tables are swapped or mixed up")
sys.exit(0 if ok else 1)
