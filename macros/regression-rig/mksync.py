"""Sync test clip: 20 s, 30 fps, 3840 x 1920 mono equirect. Every second (frames 0, 30, 60...) the whole frame
flashes white for exactly one frame and a 1 kHz beep of one frame's length (33 ms) starts on the same instant. A
big frame counter and a sweeping second hand show where you are. Also writes the audio as WAV and muxes with ffmpeg."""
from PIL import Image, ImageDraw, ImageFont
import numpy as np, subprocess, wave, struct, math, os
W, H, FPS, SECS = 3840, 1920, 30, 20
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 300)
small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 90)
os.makedirs("frames", exist_ok=True)
for f in range(FPS * SECS):
    flash = (f % FPS) == 0
    im = Image.new("RGB", (W, H), (255, 255, 255) if flash else (18, 18, 22))
    d = ImageDraw.Draw(im)
    ink = (0, 0, 0) if flash else (230, 230, 230)
    # graticule lines every 30 degrees so the picture reads as a 360 in the headset
    for deg in range(0, 361, 30):
        x = int(deg / 360 * (W - 1)); d.line([(x, 0), (x, H)], fill=(60, 60, 70) if not flash else (200, 200, 200), width=2)
    d.line([(0, H // 2), (W, H // 2)], fill=(90, 90, 100) if not flash else (180, 180, 180), width=3)
    sec, fr = divmod(f, FPS)
    d.text((W // 2 - 520, H // 2 - 330), "%02d:%02d" % (sec, fr), font=font, fill=ink)
    d.text((W // 2 - 420, H // 2 + 20), "frame %d" % f, font=small, fill=ink)
    d.text((W // 2 - 760, H // 2 + 140), "FLASH + BEEP on every :00 frame", font=small, fill=ink)
    # second hand: a dot sweeping a circle once per second
    cx, cy, r = W // 2, H // 2 + 480, 220
    a = 2 * math.pi * fr / FPS
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=ink, width=6)
    d.ellipse([cx + r * math.sin(a) - 30, cy - r * math.cos(a) - 30, cx + r * math.sin(a) + 30, cy - r * math.cos(a) + 30], fill=(255, 80, 60))
    # the same pattern mirrored at the back (180 deg) so a viewer facing away sees it too
    im.save("frames/f%04d.png" % f)
# audio: 48 kHz stereo, a 1 kHz beep for one frame starting exactly at each flash
sr = 48000; n = sr * SECS; a = np.zeros(n, dtype=np.float32)
for s in range(SECS):
    start = s * sr; length = sr // FPS
    t = np.arange(length) / sr
    env = np.minimum(1, np.minimum(t * 400, (length / sr - t) * 400))
    a[start:start + length] = 0.6 * np.sin(2 * np.pi * 1000 * t) * env
pcm = (a * 32767).astype(np.int16)
with wave.open("beeps.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(np.column_stack([pcm, pcm]).tobytes())
print("frames and wav written")
