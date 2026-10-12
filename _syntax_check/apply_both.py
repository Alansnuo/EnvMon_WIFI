"""Install the dual-table OLED_Font.h and verify BOTH switch positions compile."""
import os
import re
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\EnvMon_WIFI"
FONT = os.path.join(ROOT, "BSP", "OLED", "OLED_Font.h")
SRC = os.path.join(ROOT, "_syntax_check", "oled_font_both.h")
GCC = r"D:\Tool\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gcc.exe"
TMP = os.path.join(ROOT, "BSP", "OLED", "_font_switch_test.c")

shutil.copyfile(FONT, FONT + ".bak-before-dual")
shutil.copyfile(SRC, FONT)
print("installed dual-table header into", FONT)
print("backup:", FONT + ".bak-before-dual")

TEST = '''/* 只用于验证: 两种开关位置各编译一次
 * ⚠️ 必须像 OLED.c 那样先备好 uint8_t —— 真实工程里它是 OLED.h 带进来的 */
#include <stdint.h>
#include "OLED_Font.h"
int probe(void) { return (int)(sizeof(Hzk1) / sizeof(Hzk1[0])); }
'''

def compile_with(defines, tag):
    args = [GCC, "-fsyntax-only", "-std=c99", "-w",
            "-I", "BSP/OLED", "-I", "_syntax_check/stub",
            "-I", "Core/Inc"] + defines + [TMP]
    cp = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    ok = cp.returncode == 0
    print("  %-34s %s" % (tag, "OK" if ok else "FAIL"))
    if not ok:
        print(cp.stderr[:800])
    return ok

with open(TMP, "w", encoding="utf-8", newline="\n") as f:
    f.write(TEST)

print("\ncompile the font header in both switch positions:")
a = compile_with([], "default (generated table)")
b = compile_with(["-DHZK1_USE_REFERENCE_FONT"], "-DHZK1_USE_REFERENCE_FONT (reference)")

os.remove(TMP)
if not (a and b):
    sys.exit("one of the switch positions does not compile")

src = open(FONT, encoding="utf-8").read()
print("\nstructure:")
print("  Hzk1 definitions :", len(re.findall(r"const uint8_t Hzk1\[30\]\[32\]", src)))
print("  glyph rows       :", len(re.findall(r"^\s+\{0x", src, re.M)))
print("  enum intact      :", "HZK_JINGGAO = 29" in src)
print("\nboth switch positions compile")
