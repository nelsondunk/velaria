#!/usr/bin/env python3
"""Bakes the Velaria View icon set (UI v3, 30 Oct 2026: line icons on a 24 grid, 2.2 stroke, round caps and joins)
into anti-aliased alpha bitmaps for the headset HUD: quest/src/icons_ui.h.

The path data is the Components v3 board of the "Velaria View headset UI" design canvas. Each icon is rendered at the
sizes the panels use, 8x supersampled, so the stroke lands at 2.5-3.3 px and never shimmers.
usage: mkicons.py <out.h>
"""
import math
import re
import sys

from PIL import Image, ImageDraw

ICONS = [
    ("Overlays", "M5 9h11v11H5z M9 5h10v10"),
    ("Patches", "M4 9V4h5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5 M10 10h4v4h-4z"),
    ("Picture", "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M3 12h18 M12 3c3.5 3 3.5 15 0 18 M12 3c-3.5 3-3.5 15 0 18"),
    ("Lock", "M6 11h12v9H6z M8.5 11V8a3.5 3.5 0 0 1 7 0v3"),
    ("Unlock", "M6 11h12v9H6z M8.5 11V8a3.5 3.5 0 0 1 6.8-1.2"),
    ("Keyframe", "M12 3l7 9-7 9-7-9z"),
    ("Undo", "M8 5L3 10l5 5 M3 10h11a6 6 0 0 1 0 12H9"),
    ("Copy", "M8 8h12v12H8z M4 16V4h12"),
    ("Paste", "M7 5h10v16H7z M9.5 3h5v4h-5z"),
    ("Refresh", "M20 12a8 8 0 1 1-2.3-5.6 M20 4v5h-5"),
    ("Outline", "M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7z M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z"),
    ("LevelGrid", "M3 12h18 M6 9v6 M12 7v10 M18 9v6"),
    ("Placement", "M12 3v5 M12 16v5 M3 12h5 M16 12h5 M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z"),
    ("Look", "M4 20l4-1 11-11-3-3L5 16z M14 7l3 3"),
    ("Timeline", "M3 6h8 M14 6h7 M3 12h12 M18 12h3 M3 18h5 M11 18h10 M13 3v18"),
    ("Scopes", "M3 5h18v14H3z M6 15l3-5 3 3 3-6 3 5"),
    ("Rendering", "M7 3h10 M7 21h10 M8 3c0 5 8 5 8 9s-8 4-8 9 M16 3c0 5-8 5-8 9s8 4 8 9"),
    ("Usb", "M9 3h6v5H9z M7 8h10v5a5 5 0 0 1-10 0z M12 18v3"),
    ("Wifi", "M2 9a15 15 0 0 1 20 0 M5 13a10 10 0 0 1 14 0 M8.5 16.5a5 5 0 0 1 7 0 M12 20v.01"),
    ("Recentre", "M4 9V4h5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5 M12 10v4 M10 12h4"),
    ("Previous", "M15 5l-7 7 7 7"),
    ("Next", "M9 5l7 7-7 7"),
    ("RollUp", "M6 15l6-6 6 6"),
    ("Close", "M6 6l12 12 M18 6L6 18"),
    ("Plus", "M12 5v14 M5 12h14"),
    ("Minus", "M5 12h14"),
    ("Play", "M7 4l12 8-12 8z"),
    ("Pause", "M7 4h3v16H7z M14 4h3v16h-3z"),
    ("Stop", "M6 6h12v12H6z"),
    ("Warning", "M12 3L2 21h20z M12 10v5 M12 18v.01"),
    ("RollDown", "M6 9l6 6 6-6"),
]
SIZES = [20, 24, 28, 32, 36]
STROKE = 2.2
SS = 8   # supersampling


def tokens(d):
    return re.findall(r"[MmLlHhVvCcSsAaZz]|-?\d*\.?\d+(?:e-?\d+)?", d)


def arc_points(x0, y0, rx, ry, phi, large, sweep, x1, y1):
    """SVG elliptical arc -> polyline (centre parameterisation, W3C appendix B)."""
    if rx == 0 or ry == 0:
        return [(x1, y1)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    x1p = cp * dx + sp * dy
    y1p = -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        coef = -coef
    cxp = coef * rx * y1p / ry
    cyp = -coef * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x0 + x1) / 2
    cy = sp * cxp + cp * cyp + (y0 + y1) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(8, int(abs(dt) * 24))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * ex - sp * ey + cx, sp * ex + cp * ey + cy))
    return pts


def cubic_points(p0, p1, p2, p3, n=24):
    out = []
    for i in range(1, n + 1):
        t = i / n
        mt = 1 - t
        out.append((mt ** 3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t ** 3 * p3[0],
                    mt ** 3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def flatten(d):
    """SVG path -> list of polylines (each a list of points), closed ones repeat their first point."""
    tk = tokens(d)
    i = 0
    polys, cur = [], []
    x = y = sx = sy = 0.0
    cmd = None
    last_c2 = None

    def num():
        nonlocal i
        v = float(tk[i])
        i += 1
        return v

    while i < len(tk):
        if re.match(r"[A-Za-z]", tk[i]):
            cmd = tk[i]
            i += 1
        if cmd in "Mm":
            nx, ny = num(), num()
            if cmd == "m":
                nx, ny = x + nx, y + ny
            if cur:
                polys.append(cur)
            cur = [(nx, ny)]
            x, y, sx, sy = nx, ny, nx, ny
            cmd = "l" if cmd == "m" else "L"
            last_c2 = None
        elif cmd in "Ll":
            nx, ny = num(), num()
            if cmd == "l":
                nx, ny = x + nx, y + ny
            cur.append((nx, ny))
            x, y = nx, ny
            last_c2 = None
        elif cmd in "Hh":
            nx = num()
            if cmd == "h":
                nx += x
            cur.append((nx, y))
            x = nx
            last_c2 = None
        elif cmd in "Vv":
            ny = num()
            if cmd == "v":
                ny += y
            cur.append((x, ny))
            y = ny
            last_c2 = None
        elif cmd in "Cc":
            c1 = (num(), num())
            c2 = (num(), num())
            p = (num(), num())
            if cmd == "c":
                c1 = (x + c1[0], y + c1[1])
                c2 = (x + c2[0], y + c2[1])
                p = (x + p[0], y + p[1])
            cur += cubic_points((x, y), c1, c2, p)
            last_c2 = c2
            x, y = p
        elif cmd in "Ss":
            c2 = (num(), num())
            p = (num(), num())
            if cmd == "s":
                c2 = (x + c2[0], y + c2[1])
                p = (x + p[0], y + p[1])
            c1 = (2 * x - last_c2[0], 2 * y - last_c2[1]) if last_c2 else (x, y)
            cur += cubic_points((x, y), c1, c2, p)
            last_c2 = c2
            x, y = p
        elif cmd in "Aa":
            rx, ry, phi, large, sweep = num(), num(), num(), int(num()), int(num())
            p = (num(), num())
            if cmd == "a":
                p = (x + p[0], y + p[1])
            cur += arc_points(x, y, rx, ry, phi, large, sweep, p[0], p[1])
            x, y = p
            last_c2 = None
        elif cmd in "Zz":
            cur.append((sx, sy))
            polys.append(cur)
            cur = []
            x, y = sx, sy
            cur = [(sx, sy)]   # a following relative command starts here
            last_c2 = None
        else:
            raise ValueError(cmd)
    if len(cur) > 1:
        polys.append(cur)
    return [p for p in polys if len(p) > 1]


def bake(d, size):
    s = size * SS
    img = Image.new("L", (s, s), 0)
    dr = ImageDraw.Draw(img)
    k = s / 24.0
    w = max(1, int(round(STROKE * k)))
    for poly in flatten(d):
        pts = [(px * k, py * k) for px, py in poly]
        dr.line(pts, fill=255, width=w, joint="curve")
        for px, py in (pts[0], pts[-1]):   # round caps
            dr.ellipse((px - w / 2, py - w / 2, px + w / 2, py + w / 2), fill=255)
    return img.resize((size, size), Image.LANCZOS).tobytes()


def main():
    out = sys.argv[1]
    with open(out, "w") as f:
        f.write("// Generated by quest/tools/mkicons.py (Velaria View icon set v3). Do not edit.\n")
        f.write("#pragma once\n#include <cstdint>\n\nnamespace icons_ui {\n\n")
        f.write("enum class Icon : uint8_t { %s, Count };\n\n" % ", ".join(n for n, _ in ICONS))
        f.write("constexpr int kSizeCount = %d;\nconstexpr int kSizes[kSizeCount] = {%s};\n\n" % (len(SIZES), ", ".join(str(s) for s in SIZES)))
        f.write("// bitmaps[size index][icon]: size*size alpha, row-major\n")
        for si, size in enumerate(SIZES):
            data = bytearray()
            for _, d in ICONS:
                data += bake(d, size)
            f.write("const uint8_t kBitmap%d[%d] = {\n" % (size, len(data)))
            for i in range(0, len(data), 32):
                f.write("    " + ",".join(str(b) for b in data[i:i + 32]) + ",\n")
            f.write("};\n")
        f.write("inline const uint8_t* bitmap(int sizeIndex) {\n    switch (sizeIndex) {\n")
        for si, size in enumerate(SIZES):
            f.write("        case %d: return kBitmap%d;\n" % (si, size))
        f.write("        default: return kBitmap%d;\n    }\n}\n\n}  // namespace icons_ui\n" % SIZES[0])


if __name__ == "__main__":
    main()
