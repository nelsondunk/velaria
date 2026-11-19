#!/usr/bin/env python3
"""Reference for Velaria Dome Output: the same geometry in NumPy, compared with ffmpeg's v360 and with a Resolve render.

  python3 dome_check.py <equirect.png> [--size N] [--tilt T] [--horizon H] [--pan P] [--fov F] [--render resolve.png]

Writes ref_numpy.png and ref_ffmpeg.png (ffmpeg: v360 equirect->fisheye, h_fov=v_fov=180, pitch=90-tilt-horizon,
yaw=pan) next to the input and prints the mean difference inside the circle, blurred to take out line aliasing.
A Resolve render of the Fuse or DCTL on the same plate (--render) is compared the same way. Expected: ~0.001-0.005.
18 Nov 2026: the DCTL on Bruce (Resolve 20) matched ffmpeg to 0.0012 at tilt 0 and 0.0011 at tilt 20; flipped
variants were 15x worse, so the orientation (front at the bottom, left on the left) is unambiguous."""
import argparse, os, subprocess
import numpy as np
from PIL import Image, ImageFilter

def dome(src, size, tilt=0.0, horizon=0.0, pan=0.0, fov=180.0):
    """src: HxWx3 float equirect (rows top-down). Returns size x size x 3, rows top-down (PNG order)."""
    H, W = src.shape[:2]
    ys, xs = np.mgrid[0:size, 0:size]
    nx = (xs + 0.5) / size * 2 - 1
    ny = (ys + 0.5) / size * 2 - 1          # y up: ny = -1 is the bottom of the frame = the front of the dome
    r = np.sqrt(nx * nx + ny * ny)
    phi = np.arctan2(ny, nx)
    theta = r * np.radians(fov / 2)        # equidistant: angle from the zenith grows linearly with radius
    dr = np.sin(theta) * np.cos(phi); df = -np.sin(theta) * np.sin(phi); du = np.cos(theta)
    a = -np.radians(tilt + horizon)       # dome frame -> world: pitch about the right axis
    f2 = df * np.cos(a) - du * np.sin(a); u2 = df * np.sin(a) + du * np.cos(a)
    lon = np.arctan2(dr, f2) - np.radians(pan); lat = np.arcsin(np.clip(u2, -1, 1))
    u = (0.5 + lon / (2 * np.pi)) % 1.0; v = 0.5 + lat / np.pi
    px = np.clip((u * W).astype(int), 0, W - 1); py = np.clip(((1 - v) * H).astype(int), 0, H - 1)
    out = src[py, px]
    out[r > 1] = 0
    return out[::-1]

def inside_diff(a, b):
    s = a.shape[0]
    ys, xs = np.mgrid[0:s, 0:s]; r = np.hypot((xs + .5) / (s / 2) - 1, (ys + .5) / (s / 2) - 1); m = r < 0.97
    blur = lambda x: np.asarray(Image.fromarray((x * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3))).astype(float) / 255
    return float(np.abs(blur(a) - blur(b)).mean(2)[m].mean())

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("equirect"); ap.add_argument("--size", type=int, default=1024)
    for k in ("tilt", "horizon", "pan"): ap.add_argument("--" + k, type=float, default=0.0)
    ap.add_argument("--fov", type=float, default=180.0); ap.add_argument("--render")
    A = ap.parse_args()
    here = os.path.dirname(os.path.abspath(A.equirect))
    src = np.asarray(Image.open(A.equirect).convert("RGB")).astype(np.float32) / 255
    ref = dome(src, A.size, A.tilt, A.horizon, A.pan, A.fov)
    Image.fromarray((ref * 255).astype(np.uint8)).save(os.path.join(here, "ref_numpy.png"))
    ff = os.path.join(here, "ref_ffmpeg.png")
    vf = "v360=input=equirect:output=fisheye:h_fov=%g:v_fov=%g:pitch=%g:yaw=%g:w=%d:h=%d" % (A.fov, A.fov, 90 - A.tilt - A.horizon, A.pan, A.size, A.size)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", A.equirect, "-vf", vf, ff], check=True)
    f = np.asarray(Image.open(ff).convert("RGB")).astype(float) / 255
    print("numpy vs ffmpeg:", round(inside_diff(ref, f), 4))
    if A.render:
        rr = np.asarray(Image.open(A.render).convert("RGB").resize((A.size, A.size), Image.BILINEAR)).astype(float) / 255
        print("render vs ffmpeg:", round(inside_diff(rr, f), 4), " flipped V:", round(inside_diff(rr[::-1], f), 4), " flipped H:", round(inside_diff(rr[:, ::-1], f), 4))
