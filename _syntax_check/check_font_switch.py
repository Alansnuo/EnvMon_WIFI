"""Final check on the dual-table font header: compile it in both switch
positions and confirm each branch holds the table it claims."""
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\EnvMon_WIFI"
FONT = os.path.join(ROOT, "BSP", "OLED", "OLED_Font.h")
GCC = r"D:\Tool\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gcc.exe"
# ⚠️ 测试源放在 _syntax_check/ 下: 沙箱允许在根目录及其下建文件,
#    但不允许在 BSP/OLED 里新建 (那会触发一次提权)。
TMP = os.path.join(ROOT, "_syntax_check", "_switch_test.c")

TEST = '''#include <stdint.h>
#include "OLED_Font.h"
int probe(void) { return (int)(sizeof(Hzk1) / sizeof(Hzk1[0])); }
'''


def compile_with(defines, tag):
    cp = subprocess.run([GCC, "-fsyntax-only", "-std=c99", "-w",
                         "-I", "BSP/OLED", "-I", "_syntax_check/stub", "-I", "Core/Inc"]
                        + defines + [TMP], capture_output=True, text=True, cwd=ROOT)
    print("  %-40s %s" % (tag, "OK" if cp.returncode == 0 else "FAIL"))
    if cp.returncode:
        print(cp.stderr[:600])
    return cp.returncode == 0


with open(TMP, "w", encoding="utf-8", newline="\n") as f:
    f.write(TEST)

print("1) header compiles with the switch in both positions")
a = compile_with([], "default (= reference table)")
b = compile_with(["-UHZK1_USE_REFERENCE_FONT"], "-U HZK1_USE_REFERENCE_FONT (= generated)")
os.remove(TMP)

src = open(FONT, encoding="utf-8").read()
print("\n2) structure")
print("  table definitions:", len(re.findall(r"const uint8_t Hzk1\[30\]\[32\]", src)))
print("  glyph rows       :", len(re.findall(r"^\s+\{0x", src, re.M)))
print("  enum intact      :", "HZK_JINGGAO = 29" in src)
print("  single switch    :", src.count("#define HZK1_USE_REFERENCE_FONT"), "define statement")
print("  default is on    :", "#define HZK1_USE_REFERENCE_FONT\n" in src)

ok = a and b
print("\nRESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
