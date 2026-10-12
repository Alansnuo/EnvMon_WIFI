"""Generate a complete 16x16 CJK glyph table for Hzk1.

The reference project's table is malformed (most glyphs carry well under half
the ink a 16x16 CJK character needs, and several pairs of indices are
byte-identical duplicates), so the table is rendered fresh here.

Layout - identical to what OLED_ShowChinese already sends to the SSD1306:
    Hzk1[i][0:16]  -> page 2*(Line-1)     16 columns x 8 rows, bit0 = TOP row
    Hzk1[i][16:32] -> page 2*(Line-1)+1   16 columns x 8 rows, bit0 = TOP row
Glyph row r (0..15) lives in page r//8; within a page, column c (0..15) is
byte c, and that byte's bit (r%8) is set when the pixel is lit. Byte c is
therefore the vertical strip of 8 pixels for column c - the SSD1306 page
format. (OLED_ShowChar writes exactly this way for the 8x16 ASCII font.)

Each character is rendered with several hinted CJK fonts and the candidate
whose ink density is closest to ideal is kept; thin glyphs get a one-pixel
fatten pass so single-pixel strokes survive on the panel.
"""
import sys

sys.stdout.reconfigure(encoding="utf-8")

from PIL import Image, ImageDraw, ImageFont

OUT = r"E:\EnvMon_WIFI\_syntax_check\hzk1_generated.txt"

CHARS = [
    ("WEN", "温"), ("SHI", "湿"), ("DU", "度"), ("GUANG", "光"), ("ZHAO", "照"),
    ("QIANG", "强"), ("DU2", "度"), ("YAN", "烟"), ("WU", "雾"), ("NONG", "浓"),
    ("DU3", "度"), ("XI", "系"), ("TONG", "统"), ("MO", "模"), ("SHI2", "式"),
    ("SHOU", "手"), ("DONG", "动"), ("ZI", "自"), ("DONG2", "动"), ("XI2", "系"),
    ("TONG2", "统"), ("YU", "阈"), ("ZHI", "值"), ("SHE", "设"), ("ZHI2", "置"),
    ("JIE", "界"), ("MIAN", "面"), ("HUO", "火"), ("YAN2", "焰"), ("JINGGAO", "警"),
]
assert len(CHARS) == 30

SIZE = 16
THRESHOLD = 110
INK_MIN, INK_MAX = 45, 120      # readable band for 16x16 on a 128x64 mono panel
INK_IDEAL = 78

FONTS = [
    r"C:\Windows\Fonts\simsun.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\Deng.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
]


def render(ch, font_path, dx, dy):
    font = ImageFont.truetype(font_path, SIZE)
    img = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(img).text((dx, dy), ch, font=font, fill=255)
    px = img.load()
    return [[px[x, y] >= THRESHOLD for x in range(SIZE)] for y in range(SIZE)]


def ink(rows):
    return sum(1 for r in rows for c in r if c)


def fatten(rows):
    """Set any pixel that has a lit orthogonal neighbour - thickens hairlines."""
    out = [row[:] for row in rows]
    for y in range(SIZE):
        for x in range(SIZE):
            if rows[y][x]:
                continue
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < SIZE and 0 <= ny < SIZE and rows[ny][nx]:
                    out[y][x] = True
                    break
    return out


def rows_touching_border(rows):
    return (any(rows[0]) or any(rows[SIZE - 1]) or
            any(r[0] for r in rows) or any(r[SIZE - 1] for r in rows))


def pick(ch):
    """Try every font/offset, keep the best, fatten if too thin."""
    best, best_score = None, None
    for fp in FONTS:
        for dx, dy in ((0, 0), (0, 1), (1, 0), (1, 1)):
            try:
                rows = render(ch, fp, dx, dy)
            except Exception:
                continue
            n = ink(rows)
            if n == 0:
                continue
            score = abs(n - INK_IDEAL) + (60 if rows_touching_border(rows) else 0)
            if best_score is None or score < best_score:
                best, best_score = rows, score
    if best is None:
        raise RuntimeError("no font could render %r" % ch)

    if ink(best) < INK_MIN:
        best = fatten(best)
    return best


def pack(rows):
    """16x16 bits -> 32 bytes, SSD1306 page layout (see module docstring)."""
    out = []
    for page in range(2):
        for bit in range(8):                 # row within the page, bit0 = top
            r = page * 8 + bit
            for col_byte in range(2):        # columns 0..7 then 8..15
                b = 0
                for k in range(8):
                    x = col_byte * 8 + k
                    if rows[r][x]:
                        b |= (1 << (7 - k))  # MSB = leftmost column of the byte
                out.append(b)
    return out


def preview(rows):
    return ["".join("#" if c else "." for c in r) for r in rows]


lines = []
stats = []
for idx, (name, ch) in enumerate(CHARS):
    rows = pick(ch)
    n = ink(rows)
    stats.append((idx, name, ch, n))
    g = pack(rows)
    lines.append("    {%s},   /* [%2d] %-7s %s */"
                 % (",".join("0x%02X" % b for b in g), idx, name, ch))
    if idx in (0, 2, 27):
        print("=== [%d] %s %s  ink=%d ===" % (idx, name, ch, n))
        for r in preview(rows):
            print("   " + r)
        print()

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")

print("ink per glyph (readable band %d..%d, ideal %d):" % (INK_MIN, INK_MAX, INK_IDEAL))
bad = []
for idx, name, ch, n in stats:
    flag = ""
    if n < INK_MIN:
        flag = "  <-- TOO THIN"
    elif n > INK_MAX:
        flag = "  <-- TOO FAT"
    if flag:
        bad.append((idx, name, ch, n))
    print("  %2d %-7s %s  ink=%3d%s" % (idx, name, ch, n, flag))

print("\nwrote", OUT)
if bad:
    print("PROBLEM glyphs:", len(bad))
    for b in bad:
        print("   ", b)
    sys.exit(1)
print("all %d glyphs are in the readable band" % len(stats))
