#!/usr/bin/env python3
"""Stereo disparity check for a Canvas Control render (top/bottom equirect PNG).

    python3 stereo_check.py frame.png [--yaw 60] [--dist 3] [--width-per-eye 3840]

Isolates the card by differencing the two halves (the plate is identical in both eyes), finds the card in each
half and reports its centre in degrees and the horizontal disparity in pixels. Expected disparity for parallel
cameras: eyeSep / (2*pi*dist) * widthPerEye, right eye shifted LEFT (negative). Exit 1 when the card is missing
from a half or the disparity has the wrong sign / is zero.
"""
import argparse
import math
import sys

import numpy as np
from PIL import Image


def card_centre(diffmask, half):
    ys, xs = np.where(diffmask & (half > half.mean() + 2 * half.std()))
    if len(xs) < 50:
        return None
    return float(xs.mean()), float(ys.mean())


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
    ct, cb = card_centre(diff, top), card_centre(diff, bot)
    if not ct or not cb:
        print("FAIL: card not found in", "top" if not ct else "", "bottom" if not cb else "")
        return 1
    yaw_t = (ct[0] / w - 0.5) * 360
    yaw_b = (cb[0] / w - 0.5) * 360
    disp = cb[0] - ct[0]
    expect = -a.eyesep / (2 * math.pi * a.dist) * w
    print("card: top eye yaw %+.1f deg, bottom eye yaw %+.1f deg, disparity %+.1f px (expected about %+.1f px)" % (yaw_t, yaw_b, disp, expect))
    ok = disp < -1 and abs(disp - expect) < max(6, abs(expect) * 0.6)
    if a.yaw is not None:
        ok = ok and abs(yaw_t - a.yaw) < 3 and abs(yaw_b - a.yaw) < 3
        print("yaw expected %+.1f: %s" % (a.yaw, "ok" if abs(yaw_t - a.yaw) < 3 else "OFF"))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
