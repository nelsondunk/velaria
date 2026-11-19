"""Velaria ambisonic test signal with spoken labels (Nelson, 19 Nov 14:40): first-order AmbiX (ACN W Y Z X, SN3D),
48 kHz, 24-bit. Each direction is announced by a voice placed AT that direction, then a pulsed tone plays from the
same spot, so the ear can be checked against the word. Voices: macOS `say -v Samantha` clips in a folder.

Sequence (direction, label, tone):
  front 440 · right 990 · back 220 · left 660 · front left · front right · back left · back right ·
  up (70 deg) 1320 · down (-60 deg) 330 · "circling": 880 Hz once round front -> right -> back -> left ·
  "clicks at the front": one-frame clicks on the second, 6 s (sync against the picture)
Usage: python3 mkambix_voice.py <voice_dir> <out.wav>
"""
import math, os, sys, wave
import numpy as np

SR = 48000

def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    assert sr == SR and sw == 2, (path, sr, sw)
    a = np.frombuffer(raw, dtype="<i2").astype(float) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(1)
    return a * 0.9

segments = []   # (label_clip_name, az_deg, el_deg, tone_hz or None, tone_seconds)
plan = [("front", 0, 0, 440), ("right", -90, 0, 990), ("back", 180, 0, 220), ("left", 90, 0, 660),
        ("front_left", 45, 0, 550), ("front_right", -45, 0, 770), ("back_left", 135, 0, 495), ("back_right", -135, 0, 605),
        ("up", 0, 70, 1320), ("down", 0, -60, 330)]

vdir, out = sys.argv[1], sys.argv[2]
chunks = []   # (mono signal, az array, el array)

def tone(f, seconds, rate=4.0, duty=0.35):
    tt = np.arange(int(SR * seconds)) / SR
    env = ((tt * rate) % 1.0 < duty).astype(float)
    k = int(SR * 0.004)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    return 0.5 * np.sin(2 * math.pi * f * tt) * env

def add(sig, az, el):
    n = len(sig)
    chunks.append((sig, np.full(n, math.radians(az)) if np.isscalar(az) else az, np.full(n, math.radians(el)) if np.isscalar(el) else el))

gap = np.zeros(int(SR * 0.35))
for name, az, el, f in plan:
    add(load(os.path.join(vdir, name + ".wav")), az, el)
    add(gap, az, el)
    add(tone(f, 2.5), az, el)
    add(gap * 2, az, el)
# circling: announced from the front, then the tone goes round
add(load(os.path.join(vdir, "circling.wav")), 0, 0)
add(gap, 0, 0)
n = int(SR * 8)
add(tone(880, 8.0), -2 * math.pi * (np.arange(n) / SR) / 8.0, np.zeros(n))
add(gap * 2, 0, 0)
# clicks on the second at the front
add(load(os.path.join(vdir, "clicks_at_the_front.wav")), 0, 0)
add(gap, 0, 0)
clk = np.zeros(int(SR * 6))
for s in range(6):
    i = s * SR
    clk[i:i + 48] = 0.9 * np.hanning(48)
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
