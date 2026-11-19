"""Velaria ambisonic test signal: first-order AmbiX (ACN order W, Y, Z, X; SN3D), 48 kHz, 24-bit, 32 s.

A pulsed tone moves around the listener so each direction can be named by ear:
   0-8 s   circles once, front -> right -> back -> left -> front (880 Hz, 4 pulses a second)
   8-11 s  front   440 Hz      11-14 s  back   220 Hz
  14-17 s  left    660 Hz      17-20 s  right  990 Hz
  20-23 s  up      1320 Hz     23-26 s  down   330 Hz
  26-32 s  front, a one-frame click on every second (sync check against the picture)
AmbiX conventions: X front, Y left, Z up; W = s, Y = s sin(az) cos(el), Z = s sin(el), X = s cos(az) cos(el).
Usage: python3 mkambix.py out.wav
"""
import math, struct, sys, wave
import numpy as np

SR = 48000
T = 32.0
t = np.arange(int(SR * T)) / SR
n = len(t)
sig = np.zeros(n)
az = np.zeros(n)   # radians, 0 front, +pi/2 left
el = np.zeros(n)

def pulses(seg, f, rate=4.0, duty=0.35):
    i0, i1 = int(seg[0] * SR), int(seg[1] * SR)
    tt = t[i0:i1] - seg[0]
    env = ((tt * rate) % 1.0 < duty).astype(float)
    # soften pulse edges
    k = int(SR * 0.004)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    sig[i0:i1] = 0.5 * np.sin(2 * math.pi * f * tt) * env

# circle
i0, i1 = 0, int(8 * SR)
pulses((0, 8), 880)
az[i0:i1] = -2 * math.pi * (t[i0:i1] / 8.0)      # negative = clockwise from above (front -> right)
for seg, f, a, e in (((8, 11), 440, 0, 0), ((11, 14), 220, math.pi, 0), ((14, 17), 660, math.pi / 2, 0),
                     ((17, 20), 990, -math.pi / 2, 0), ((20, 23), 1320, 0, math.radians(70)), ((23, 26), 330, 0, math.radians(-60))):
    pulses(seg, f)
    s0, s1 = int(seg[0] * SR), int(seg[1] * SR)
    az[s0:s1] = a
    el[s0:s1] = e
# clicks on the second, front
for sec in range(26, 32):
    i = int(sec * SR)
    sig[i:i + 48] = 0.9 * np.hanning(48)
W = sig
Y = sig * np.sin(az) * np.cos(el)
Z = sig * np.sin(el)
X = sig * np.cos(az) * np.cos(el)
pcm = np.stack([W, Y, Z, X], axis=1)
pcm = np.clip(pcm, -1, 1)
ints = (pcm * 8388607).astype(np.int32)
out = sys.argv[1] if len(sys.argv) > 1 else "ambix_1st_order_test.wav"
with wave.open(out, "wb") as w:
    w.setnchannels(4)
    w.setsampwidth(3)
    w.setframerate(SR)
    b = bytearray()
    flat = ints.reshape(-1)
    # 24-bit little-endian packing
    arr = np.zeros((flat.size, 3), dtype=np.uint8)
    u = flat.astype(np.int64) & 0xFFFFFF
    arr[:, 0] = u & 0xFF
    arr[:, 1] = (u >> 8) & 0xFF
    arr[:, 2] = (u >> 16) & 0xFF
    w.writeframes(arr.tobytes())
print(out, n, "frames, 4 ch, 24-bit")
