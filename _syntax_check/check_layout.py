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
# A CJK glyph is 16px wide, ASCII 8px. Both are addressed by a 1-based column,
# so their pixel spans differ: CJK at col c -> [(c-1)*16, +15], ASCII -> [(c-1)*8, +7].
# Overlap between a glyph and the text next to it is a real bug (the 温度 row
# started its value at col 4, which overwrote the right half of 度 at px 24-31).
PX = 128


def span(col, pxw, cjk):
    """col is 1-based; CJK columns step 16px, ASCII columns 8px."""
    step = 16 if cjk else 8
    start = (col - 1) * step
    return (start, start + pxw - 1)


def row_ok(row, desc, items):
    """items: (label, col, px_width, is_cjk)"""
    spans = [(label, span(col, pxw, cjk)) for (label, col, pxw, cjk) in items]
    used = max(s[1][1] for s in spans) + 1
    over = used > PX
    clash = []
    for i in range(len(spans)):
        for j in range(i + 1, len(spans)):
            a, b = spans[i][1], spans[j][1]
            if a[0] <= b[1] and b[0] <= a[1]:
                clash.append("%s x %s" % (spans[i][0], spans[j][0]))
    ok = (not over) and (not clash)
    if not ok:
        fail.append((row, desc, used))
    print("  %-4s %-30s px %s" % ("OK" if ok else "OVER", desc,
          " ".join("%s=%d..%d" % (l, s[0], s[1]) for l, s in spans)))
    if clash:
        print("        OVERLAP: " + ", ".join(clash))
    if over:
        print("        OVERFLOW: %d px (screen is %d)" % (used, PX))


# row1/2: 温(col1) 度(col2), value ASCII at col5 width5 (=40px). No touch.
row_ok(1, "温 度 + %2d.%dC", [("温", 1, 16, True), ("度", 2, 16, True), ("val", 5, 40, False)])
row_ok(2, "湿 度 + %2d.%d%%", [("湿", 1, 16, True), ("度", 2, 16, True), ("val", 5, 40, False)])
# row3: 光照强度 at col1..4, value ASCII at col10 width7 (=56px) -> px 72..127
row_ok(3, "光照强度 + %4u lx",
       [("光", 1, 16, True), ("照", 2, 16, True), ("强", 3, 16, True), ("度", 4, 16, True),
        ("val", 10, 56, False)])
# row4: WiFi:OK at col1 width7 (=56px), time at col9 width8 (=64px) -> px 64..127
row_ok(4, "WiFi:%s + hh:mm:ss", [("wifi", 1, 56, False), ("time", 9, 64, False)])
# row1 right side: warning glyph at CJK col 7 -> px 96..111
row_ok(1, "warning glyph (col 13-14)", [("val", 5, 40, False), ("warn", 7, 16, True)])

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
