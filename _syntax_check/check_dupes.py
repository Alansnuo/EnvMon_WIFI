"""Detect the L6200E class of bug: the same global symbol defined in two objects.

Reproduces what the Keil linker reported:
    project.axf: Error: L6200E: Symbol Hzk1 multiply defined (by oled.o and task_oled.o)

For every project .c file: compile to .o, list defined global symbols, and fail
if any symbol is defined by more than one object. That is exactly the condition
armlink rejects -- and it is invisible to a per-file syntax check.
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\EnvMon_WIFI"
GCC = r"D:\Tool\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gcc.exe"
NM = r"D:\Tool\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-nm.exe"
OUT = os.path.join(ROOT, "_syntax_check", "obj")

INCLUDES = [
    os.path.join(ROOT, "_syntax_check", "stub"),
    os.path.join(ROOT, "APP"),
    os.path.join(ROOT, "BSP", "OLED"),
    os.path.join(ROOT, "BSP", "dht11"),
    os.path.join(ROOT, "BSP", "ESP8266"),
    os.path.join(ROOT, "BSP", "ASR"),
    os.path.join(ROOT, "BSP", "KEY"),
    os.path.join(ROOT, "BSP", "BH1750"),
    os.path.join(ROOT, "BSP", "bsp_delay"),
    os.path.join(ROOT, "Core", "Inc"),
]

# 只检查能独立编译的文件（其余的桩不全，编译不过，与本次问题无关）。
# ⚠️ BSP/OLED/OLED.c 用**真**字库头文件（不放进 stub 路径之前），
#    这样它的 Hzk1 / OLED_F8x16 定义才会出现在符号表里 —— 正是要抓的东西。
TARGETS = [
    ("APP/app_ui.c", False),
    ("APP/task_oled.c", False),
    ("APP/task_wifi.c", False),
    ("APP/task_key.c", False),
    ("APP/task_asr.c", False),
    ("APP/app_main.c", False),
    ("BSP/OLED/OLED.c", True),      # 真字库头
]

os.makedirs(OUT, exist_ok=True)
defs = defaultdict(list)
errors = 0

for rel, real_font in TARGETS:
    src = os.path.join(ROOT, rel.replace("/", os.sep))
    obj = os.path.join(OUT, rel.replace("/", "_").replace(".c", ".o"))

    inc = []
    if real_font:
        # 真字库目录放最前面, 其余用 stub
        inc += ["-I", os.path.join(ROOT, "BSP", "OLED")]
    for d in INCLUDES:
        inc += ["-I", d]

    cp = subprocess.run([GCC, "-c", "-std=c99", "-Wall", "-Wextra",
                         "-Wno-unused-parameter", "-O0", "-o", obj, src] + inc,
                        capture_output=True, text=True)
    if cp.returncode != 0:
        print("COMPILE FAIL %s" % rel)
        print(cp.stderr[:600])
        errors += 1
        continue

    nm = subprocess.run([NM, "--defined-only", "-g", obj],
                        capture_output=True, text=True)
    syms = []
    for line in nm.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("D", "B", "R", "T", "G", "S"):
            syms.append(parts[2])
    print("%-22s defines: %s" % (rel, ", ".join(syms) if syms else "(none)"))
    for s in syms:
        defs[s].append(rel)

print("\n=== symbols defined by more than one object (would be L6200E) ===")
dupes = {s: v for s, v in defs.items() if len(v) > 1}
if dupes:
    for s, v in sorted(dupes.items()):
        print("  %-16s <- %s" % (s, ", ".join(v)))
    errors += 1
else:
    print("  (none)  -- no duplicate global definitions")

print("\nRESULT:", "FAIL" if errors else "PASS")
sys.exit(1 if errors else 0)
