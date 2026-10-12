"""Build the new OLED_Font.h (old table swapped for the generated one) and save
it to the workspace root, which is writable without escalation.

The replacement itself is done here; moving the result over the original is left
to the file tools, since creating new files under BSP/ needs wider access.
"""
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

FONT = r"E:\EnvMon_WIFI\BSP\OLED\OLED_Font.h"
NEW = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"
OUT = r"E:\EnvMon_WIFI\_new_OLED_Font.h"

src = open(FONT, "r", encoding="utf-8").read()
rows = open(NEW, "r", encoding="utf-8").read().rstrip("\n").split("\n")
assert len(rows) == 30, "expected 30 rows, got %d" % len(rows)

m = re.search(r"(const uint8_t Hzk1\[30\]\[32\]=\s*\{\s*\n)(.*?)(\n\};)", src, re.S)
if not m:
    sys.exit("could not locate the Hzk1 array")
old_rows = [ln for ln in m.group(2).split("\n") if ln.strip()]
print("existing rows:", len(old_rows))
if len(old_rows) != 30:
    sys.exit("expected 30 existing rows")

new_src = src[:m.start(2)] + "\n".join(rows) + src[m.end(2):]
with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write(new_src)

print("wrote", OUT)
print("  size:", len(new_src.encode("utf-8")), "bytes")
print("  rows in output:", len(re.findall(r"^\s+\{0x", new_src, re.M)))
# sanity: the file must still be complete C
for token in ("#ifndef __OLED_FONT_H", "OLED_F8x16", "enum {", "HZK_JINGGAO = 29", "#endif"):
    if token not in new_src:
        sys.exit("output is missing %r" % token)
print("  structure check: OK")
