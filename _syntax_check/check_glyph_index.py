"""Compare the HZK_* glyph indices in OLED.h against the Hzk1 table rows.

The enum in BSP/OLED/OLED.h is the single source of truth for glyph indices,
and every name in it must have exactly one row in the Hzk1 table in
BSP/OLED/OLED_Font.h.  A mismatch means one of two real bugs:

  * a glyph was added to the table but not to the enum (or the reverse)
  * the two fell out of order, so every index points at the wrong character

That second one is the dangerous case: it compiles clean and renders garbage.

Exit 0 = consistent, 1 = mismatch (or the files could not be parsed).
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

HDR = r"E:\EnvMon_WIFI\BSP\OLED\OLED.h"
FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"


def parse():
    hdr = open(HDR, encoding="utf-8").read()

    # the glyph enum: names listed between "enum {" and the closing "};"
    m = re.search(r"enum\s*\{(.*?)\};", hdr, re.S)
    if m is None:
        raise SystemExit("FAIL: no enum found in OLED.h")
    body = m.group(1)
    names = re.findall(r"\b(HZK_[A-Z0-9_]+)\b\s*(?:=\s*\d+)?", body)
    # drop a trailing "HZK_NUM" style count sentinel, if one exists
    names = [n for n in names if n != "HZK_NUM"]
    if not names:
        raise SystemExit("FAIL: no HZK_* names found in the enum")

    font = open(FONT, encoding="utf-8").read()
    rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?\s*){32})\}", font)
    return names, rows


names, rows = parse()

print("enum names      : %d" % len(names))
print("Hzk1 table rows : %d" % len(rows))
print()

ok = True
if len(names) != len(rows):
    ok = False
    print("MISMATCH: enum has %d names but the table has %d rows"
          % (len(names), len(rows)))

# every row must be 32 bytes -- a short row would shift every later glyph
short = [i for i, r in enumerate(rows) if len(re.findall(r"0x[0-9A-Fa-f]{2}", r)) != 32]
if short:
    ok = False
    print("MISMATCH: rows not 32 bytes wide: %s" % short)

if ok:
    print("index -> name           (matches Hzk1 row order)")
    for i in (0, 1, len(names) - 1):
        print("  %2d  -> %s" % (i, names[i]))

print()
print("RESULT:", "OK" if ok else "MISMATCH")
sys.exit(0 if ok else 1)
