#!/usr/bin/env python3
"""Stereo disparity check for a Canvas Control render (top/bottom equirect PNG).

    python3 stereo_check.py frame.png [--yaw 60] [--dist 3] [--width-per-eye 3840]

Isolates the card by differencing the two halves (the plate is identical in both eyes), finds the card and reports
its centre in degrees and the horizontal disparity in pixels. Use a plate without per-eye labels (the Velaria chart's
"L 360" / "R 360" confuse the card finder; the yaw figure is then off, the disparity usually still right). Expected disparity for parallel
cameras: eyeSep / (2*pi*dist) * widthPerEye, right eye shifted LEFT (negative). Exit 1 when the card is missing
from a half or the disparity has the wrong sign / is zero.
"""
import argparse
import math
import sys

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("--yaw", type=float, default=None, help="expected yaw in degrees")
    ap.add_argument("--dist", type=float, default=3.0)
    ap.add_argument("--eyesep", type=float, default=0.065)
    a = ap.parse_args()
    im = np.asarray(Image.open(a.png).convert("L")).astype(float)
    h, w = im.shape
    top, bot = im[: h // 2], im[h // 2 :]
    diff = np.abs(top - bot) > 20
    if diff.sum() < 200:
        print("FAIL: the two eyes are identical (no card, or no stereo)")
        return 1
    prof = diff.sum(axis=0)
    on = prof > 2
    # close gaps up to 2 % of the width (the card differs between eyes only along its edges), then take the widest
    # run of differing columns = the card; chart labels that differ between eyes (L 360 / R 360) are narrow
    gap = max(1, w // 50)
    idx = np.where(on)[0]
    closed = np.zeros_like(on)
    for i in idx:
        closed[i:i + gap] = True
    on = closed
    runs, start = [], None
    for x, v in enumerate(list(on) + [False]):
        if v and start is None: start = x
        if not v and start is not None: runs.append((x - start, start, x)); start = None
    _, rx0, rx1 = max(runs)
    cx = (rx0 + rx1) // 2
    ys = np.where(diff[:, max(0, cx - w // 12): cx + w // 12].any(axis=1))[0]
    y0, y1 = max(0, ys.min() - 20), min(h // 2, ys.max() + 20)
    x0, x1 = max(0, rx0 - 40), min(w, rx1 + 40)
    A = top[y0:y1, x0:x1]; B = bot[y0:y1, x0:x1]
    # weight by the (dilated) difference so an identical plate behind the card does not pin the correlation at 0
    m = diff[y0:y1, x0:x1].astype(float)
    k = np.ones((9, 41)) / (9 * 41)
    from numpy.lib.stride_tricks import sliding_window_view as swv
    pad = np.pad(m, ((4, 4), (20, 20)))
    wgt = (swv(pad, (9, 41)) * k).sum(axis=(2, 3)) > 0
    A = (A - A.mean()) * wgt; B = (B - B.mean()) * wgt
    # bottom eye shifted by s relative to the top eye: the s that best realigns them
    best = max(((s_, float((A * np.roll(B, s_, axis=1)).sum())) for s_ in range(-150, 151)), key=lambda t: t[1])
    disp = -best[0]
    yaw = (cx / w - 0.5) * 360
    expect = -a.eyesep / (2 * math.pi * a.dist) * w
    print("card: centre yaw %+.1f deg, right-eye disparity %+.1f px (expected about %+.1f px for %.3f m at %.1f m)" % (yaw, disp, expect, a.eyesep, a.dist))
    ok = disp < -1 and abs(disp - expect) < max(6, abs(expect) * 0.6)
    if a.yaw is not None:
        ok = ok and abs(yaw - a.yaw) < 4
        print("yaw expected %+.1f: %s" % (a.yaw, "ok" if abs(yaw - a.yaw) < 4 else "OFF"))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
