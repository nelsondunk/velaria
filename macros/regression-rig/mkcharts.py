#!/usr/bin/env python3
"""Test charts for the 180 / 360 macro checks: labelled eyes, 10-degree graticule, centre cross, direction words.
  chart_180_sbs.png   3840 x 1920: two square half-equirect eyes side by side (L | R), each 180 x 180 degrees
  chart_360_tb.png    3840 x 3840: two 2:1 equirect eyes over/under (L on top), each 360 x 180 degrees
Colours differ per eye (L = warm, R = cool) so a swapped or doubled eye is obvious in a render."""
from PIL import Image, ImageDraw, ImageFont
import sys, os

out = sys.argv[1] if len(sys.argv) > 1 else "."
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 120)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 60)
except Exception:
    font = small = ImageFont.load_default()

def eye(w, h, hfov, label, tint):
    im = Image.new("RGB", (w, h), tint)
    d = ImageDraw.Draw(im)
    # graticule every 10 degrees of yaw / pitch; a brighter line at 0 and at +-90
    for deg in range(-hfov // 2, hfov // 2 + 1, 10):
        x = int((deg + hfov / 2) / hfov * (w - 1))
        col = (255, 255, 255) if deg % 90 == 0 else (140, 140, 140)
        d.line([(x, 0), (x, h)], fill=col, width=4 if deg % 90 == 0 else 1)
    for deg in range(-90, 91, 10):
        y = int((90 - deg) / 180 * (h - 1))
        col = (255, 255, 255) if deg == 0 else (140, 140, 140)
        d.line([(0, y), (w, y)], fill=col, width=4 if deg == 0 else 1)
    cx, cy = w // 2, h // 2
    d.ellipse([cx - 40, cy - 40, cx + 40, cy + 40], outline=(255, 60, 60), width=8)
    d.text((cx + 60, cy - 150), label, font=font, fill=(255, 255, 255))
    d.text((cx - 220, 40), "UP", font=small, fill=(255, 255, 0))
    d.text((cx - 260, h - 110), "DOWN", font=small, fill=(255, 255, 0))
    d.text((40, cy - 30), "LEFT", font=small, fill=(0, 255, 255))
    d.text((w - 260, cy - 30), "RIGHT", font=small, fill=(0, 255, 255))
    if hfov == 360:
        d.text((40, cy - 120), "BACK", font=small, fill=(255, 128, 0))
        d.text((w - 260, cy - 120), "BACK", font=small, fill=(255, 128, 0))
    # a square marker 45 degrees right, 20 up: shows mirror / flip at a glance
    mx = int((45 + hfov / 2) / hfov * (w - 1)); my = int((90 - 20) / 180 * (h - 1))
    d.rectangle([mx - 50, my - 50, mx + 50, my + 50], fill=(0, 200, 0))
    return im

L = eye(1920, 1920, 180, "L 180", (60, 30, 20))
R = eye(1920, 1920, 180, "R 180", (20, 30, 60))
sbs = Image.new("RGB", (3840, 1920)); sbs.paste(L, (0, 0)); sbs.paste(R, (1920, 0)); sbs.save(os.path.join(out, "chart_180_sbs.png"))
L = eye(3840, 1920, 360, "L 360", (60, 30, 20))
R = eye(3840, 1920, 360, "R 360", (20, 30, 60))
tb = Image.new("RGB", (3840, 3840)); tb.paste(L, (0, 0)); tb.paste(R, (0, 1920)); tb.save(os.path.join(out, "chart_360_tb.png"))
print("wrote charts in", out)
