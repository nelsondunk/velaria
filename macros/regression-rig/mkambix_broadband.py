"""Velaria ambisonic test signal, broadband version (20 Nov 2026). The spoken test's voice (macOS `say`) is
band-limited to about 8 kHz, and front/back and elevation in a binaural decode live above that, so the voice alone
cannot show whether a decode resolves them (Nelson, 20 Nov: "front and back... seem ambiguous"). This file keeps
the voice as a label only and puts broadband material at each position: three pink-noise bursts, then a click train.
First-order AmbiX (ACN W Y Z X, SN3D), 48 kHz, 24-bit.

  front · right · back · left · front left · front right · back left · back right · up (70 deg) · down (-60 deg) ·
  circling (noise bursts once round front -> right -> back -> left, 8 s) · clicks at the front (on the second, 6 s)

Usage: python3 mkambix_broadband.py <voice_dir> <out.wav>   (voice_dir: the clips mkambix_voice.py uses)"""
import math, os, sys, wave
import numpy as np

SR = 48000
rng = np.random.default_rng(20261120)

def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    assert sr == SR and sw == 2, (path, sr, sw)
    a = np.frombuffer(raw, dtype="<i2").astype(float) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(1)
    return a * 0.9

def pink(n):
    """Pink-ish noise: white noise shaped by a -3 dB/oct filter (Paul Kellet's economy filter)."""
    w = rng.standard_normal(n)
    b0 = b1 = b2 = 0.0
    out = np.empty(n)
    for i in range(n):
        x = w[i]
        b0 = 0.99765 * b0 + x * 0.0990460
        b1 = 0.96300 * b1 + x * 0.2965164
        b2 = 0.57000 * b2 + x * 1.0526913
        out[i] = b0 + b1 + b2 + x * 0.1848
    out /= np.max(np.abs(out)) + 1e-9
    return out

def bursts(seconds=2.4, count=3, length=0.4, level=0.45):
    n = int(SR * seconds)
    sig = np.zeros(n)
    step = seconds / count
    for k in range(count):
        i = int(SR * k * step)
        m = int(SR * length)
        env = np.hanning(m) ** 0.5
        sig[i:i + m] = level * pink(m) * env
    return sig

def clicks(seconds=2.0, rate=4.0):
    n = int(SR * seconds)
    sig = np.zeros(n)
    for k in range(int(seconds * rate)):
        i = int(SR * k / rate)
        sig[i:i + 48] = 0.9 * np.hanning(48)
    return sig

plan = [("front", 0, 0), ("right", -90, 0), ("back", 180, 0), ("left", 90, 0),
        ("front_left", 45, 0), ("front_right", -45, 0), ("back_left", 135, 0), ("back_right", -135, 0),
        ("up", 0, 70), ("down", 0, -60)]

vdir, out = sys.argv[1], sys.argv[2]
chunks = []

def add(sig, az, el):
    n = len(sig)
    chunks.append((sig, np.full(n, math.radians(az)) if np.isscalar(az) else az,
                   np.full(n, math.radians(el)) if np.isscalar(el) else el))

gap = np.zeros(int(SR * 0.35))
for name, az, el in plan:
    add(load(os.path.join(vdir, name + ".wav")), az, el)
    add(gap, az, el)
    add(bursts(), az, el)
    add(gap, az, el)
    add(clicks(), az, el)
    add(gap * 2, az, el)
add(load(os.path.join(vdir, "circling.wav")), 0, 0)
add(gap, 0, 0)
n = int(SR * 8)
add(bursts(8.0, 10, 0.5), -2 * math.pi * (np.arange(n) / SR) / 8.0, np.zeros(n))
add(gap * 2, 0, 0)
add(load(os.path.join(vdir, "clicks_at_the_front.wav")), 0, 0)
add(gap, 0, 0)
clk = np.zeros(int(SR * 6))
for s in range(6):
    clk[s * SR:s * SR + 48] = 0.9 * np.hanning(48)
add(clk, 0, 0)

sig = np.concatenate([c[0] for c in chunks])
az = np.concatenate([c[1] for c in chunks])
el = np.concatenate([c[2] for c in chunks])
W = sig
Y = sig * np.sin(az) * np.cos(el)
Z = sig * np.sin(el)
X = sig * np.cos(az) * np.cos(el)
pcm = np.clip(np.stack([W, Y, Z, X], axis=1), -1, 1)
ints = (pcm * 8388607).astype(np.int64).reshape(-1)
u = ints & 0xFFFFFF
arr = np.zeros((u.size, 3), dtype=np.uint8)
arr[:, 0] = u & 0xFF
arr[:, 1] = (u >> 8) & 0xFF
arr[:, 2] = (u >> 16) & 0xFF
with wave.open(out, "wb") as w:
    w.setnchannels(4)
    w.setsampwidth(3)
    w.setframerate(SR)
    w.writeframes(arr.tobytes())
print(out, "%.1f s" % (len(sig) / SR))
