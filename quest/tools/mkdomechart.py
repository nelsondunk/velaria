#!/usr/bin/env python3
"""Draws a domemaster test chart: square, equidistant fisheye, zenith at the centre, bottom of frame = front of dome
(IMERSA orientation). Rings every 15 degrees of elevation above the springline, spokes every 30 degrees of azimuth,
FRONT / REAR / LEFT / RIGHT lettered the way a viewer inside the dome should read them.
  python3 quest/tools/mkdomechart.py [size] [out.png]     (default 4096, docs/test-media/dome-chart-4096.png)
"""
import math, sys
from PIL import Image, ImageDraw, ImageFont

N = int(sys.argv[1]) if len(sys.argv) > 1 else 4096
out = sys.argv[2] if len(sys.argv) > 2 else f"docs/test-media/dome-chart-{N}.png"
im = Image.new("RGB", (N, N), (0, 0, 0))
d = ImageDraw.Draw(im)
c, R = N / 2, N / 2
def font(px):
    for p in ("quest/fonts/RedHatText-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        try: return ImageFont.truetype(p, int(px))
        except OSError: pass
    return ImageFont.load_default()
d.ellipse([c - R, c - R, c + R - 1, c + R - 1], fill=(28, 32, 38))
# elevation rings: radius = (90 - elevation) / 90 of the circle
for el in range(0, 90, 15):
    r = R * (90 - el) / 90
    col = (226, 168, 120) if el == 0 else (150, 200, 230) if el == 45 else (110, 118, 130)
    d.ellipse([c - r, c - r, c + r, c + r], outline=col, width=max(2, N // (340 if el in (0, 45) else 800)))
# azimuth spokes; 0 = front = straight down the frame, positive to the viewer's right
for az in range(0, 360, 30):
    a = math.radians(az)
    x, y = c + R * math.sin(a), c + R * math.cos(a)
    d.line([c, c, x, y], fill=(110, 118, 130), width=max(1, N // 1000))
f_big, f_mid, f_small = font(N / 22), font(N / 40), font(N / 64)
def label(text, az, el, f, fill):
    # text is drawn upright for a viewer facing that azimuth: its top points at the zenith (the frame centre)
    a = math.radians(az); r = R * (90 - el) / 90
    x, y = c + r * math.sin(a), c + r * math.cos(a)
    w, h = d.textbbox((0, 0), text, font=f)[2:]
    t = Image.new("RGBA", (w + 8, h + 8), (0, 0, 0, 0))
    ImageDraw.Draw(t).text((4, 0), text, font=f, fill=fill)
    t = t.rotate(az, expand=True, resample=Image.BICUBIC)
    im.paste(t, (int(x - t.width / 2), int(y - t.height / 2)), t)
for name, az in (("FRONT", 0), ("RIGHT", 90), ("REAR", 180), ("LEFT", 270)):
    label(name, az, 22, f_big, (245, 245, 245))
for az in range(0, 360, 30):
    for el in (0, 15, 30, 45, 60, 75):
        if el == 0: label(f"{az if az <= 180 else az - 360:+d}", az, 4, f_small, (226, 168, 120))
        elif az % 90 == 0: label(f"{el}", az + 4, el + 2, f_small, (200, 205, 212))
label("SWEET SPOT 45", 0, 49, f_mid, (150, 200, 230))
d.ellipse([c - N / 300, c - N / 300, c + N / 300, c + N / 300], fill=(218, 77, 67))
label("ZENITH", 0, 86, f_small, (218, 77, 67))
im.save(out, optimize=True)
print("wrote", out)
