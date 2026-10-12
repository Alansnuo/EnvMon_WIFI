import os
import sys
sys.stdout.reconfigure(encoding="utf-8")

cands = [
    r"C:\Windows\Fonts\simsun.ttc",
    r"C:\Windows\Fonts\simsun.ttf",
    r"C:\Windows\Fonts\SIMSUN.TTC",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyh.ttf",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simkai.ttf",
    r"C:\Windows\Fonts\simfang.ttf",
    r"C:\Windows\Fonts\Deng.ttf",
    r"C:\Windows\Fonts\mingliu.ttc",
]
for c in cands:
    print("%-40s %s" % (c, "OK" if os.path.exists(c) else "-"))

print()
try:
    from PIL import ImageFont, Image, ImageDraw
    print("Pillow import OK")
    for c in cands:
        if os.path.exists(c):
            try:
                f = ImageFont.truetype(c, 16)
                img = Image.new("L", (16, 16), 0)
                ImageDraw.Draw(img).text((0, 0), "度", font=f, fill=255)
                px = img.load()
                ink = sum(1 for y in range(16) for x in range(16) if px[x, y] > 128)
                print("  %-40s renders, ink=%d" % (os.path.basename(c), ink))
            except Exception as e:
                print("  %-40s FAILED: %s" % (os.path.basename(c), e))
except Exception as e:
    print("Pillow import FAILED:", e)
