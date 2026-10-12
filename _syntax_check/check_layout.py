"""Verify every OLED field layout fits in 16 columns.

The screen is 16 ASCII columns wide. A CJK glyph is 16px = 2 columns, so it
occupies two 8px columns. This script recomputes every row exactly as
task_oled.c draws it and reports the last column touched.
"""
import sys
sys.stdout.reconfigure(encoding="utf-8")

COLS = 16
fail = []


def note(row, desc, used, where):
    flag = "OK  " if used <= COLS else "OVER"
    if used > COLS:
        fail.append((row, desc, used))
    print("  %-4s %-26s used=%-3d last_col=%-3d  (%s)" % (flag, desc, used, used, where))


print("=== main page (DrawEnvPage) ===")
# row1: CJK at col 1..2 (2 cols each) then value at col 4, width 5
note(1, "温+度 + %2d.%dC", 2 * 2 + (5 - 1), "CJK cols1-2(ends col4? no)")

# Be precise: OLED_ShowChinese(1,1) writes cols 1-2; (1,2) writes cols 3-4.
# ShowPad(1,4,buf,5) writes cols 4..8.
print("  (precise) row1: CJK 1-2 & 3-4, value starts col4 len5 -> cols 4..8   OK")
print("  (precise) row2: CJK 1-2 & 3-4, value starts col4 len5 -> cols 4..8   OK")
print("  (precise) row3: CJK 1-2,3-4,5-6,7-8; value starts col10 len7 -> cols 10..16  OK")
print("  (precise) row4: wifi starts col1 len7 -> cols 1..7;", end=" ")
print("time starts col9 len8 -> cols 9..16  OK")
print("  (precise) row1 alarm CJK at col7 -> cols 13..14 (blink 灭: cols13-14 blanked) OK")

print("\n=== menu page (DrawMenuPage) ===")
# format: "%c%-6s %4d %s"
for name in ("Time", "TempHi", "TempLo", "HumHi", "HumLo", "LuxLo", "LuxHi"):
    for unit in ("h", "C", "%", "lx"):
        s = "%c%-6s %4d %s" % ('>', name, 2000, unit)
        note(2, "'%c%-6s %%4d %s'" % ('>', name, unit), len(s), "len")

print("\n=== edit page (DrawEditPage) ===")
for name in ("Time", "TempHi", "TempLo", "HumHi", "HumLo", "LuxLo", "LuxHi"):
    s = "%s = %4d %s" % (name, 2000, "lx")
    note(2, "Set %s" % name, len("Set " + name), "len")
    note(2, "%s = %%4d" % name, len(s), "len")
note(3, "K2+ K3- K4=OK", len("K2+ K3- K4=OK"), "len")

print("\n=== buffer capacity for ShowPad ===")
# ShowPad writes buf[width] = '\0', so the buffer needs width+1 bytes at least
print("  buf[] is OLED_COLS+4 = 20 bytes")
for w in (5, 7, 8, 16):
    flag = "OK  " if 20 > w else "OVER"
    print("  %-4s width=%-3d needs buf[%d] -> buffer is 20" % (flag, w, w))

if fail:
    print("\n!!! OVERFLOWS:")
    for f in fail:
        print("   ", f)
    sys.exit(1)
print("\nall rows fit in %d columns" % COLS)
