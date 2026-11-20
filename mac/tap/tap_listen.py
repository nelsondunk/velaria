#!/usr/bin/env python3
"""Listen for Velaria Tap datagrams on 127.0.0.1:47813 and print one line a second: channels, rate, frames,
host playing flag, timeline position, packet loss, per-channel RMS. The stand-in for the driver while the plugin
is being judged in Fairlight.  python3 mac/tap/tap_listen.py"""
import math
import socket
import struct
import sys
import time

HDR = struct.Struct("<IHHIIqII")   # magic, version, channels, sampleRate, flags, projectTime, frames, seq
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 47813))
s.settimeout(0.5)
print("listening on 127.0.0.1:47813 (Ctrl-C to stop)")
last = time.time()
pk = frames = lost = 0
seq = None
sq = [0.0] * 16
info = None
while True:
    try:
        d, _ = s.recvfrom(70000)
    except socket.timeout:
        d = None
    if d and len(d) >= HDR.size:
        magic, ver, ch, sr, flags, pt, n, sq_no = HDR.unpack_from(d)
        if magic == 0x50415456:
            if seq is not None and sq_no != (seq + 1) & 0xFFFFFFFF:
                lost += (sq_no - seq - 1) & 0xFFFFFFFF
            seq = sq_no
            pk += 1
            frames += n
            body = memoryview(d)[HDR.size:]
            vals = struct.unpack_from("<%df" % (n * ch), body)
            for c in range(ch):
                sq[c] += sum(v * v for v in vals[c::ch])
            info = (ch, sr, flags, pt, n)
    now = time.time()
    if now - last >= 1.0:
        if info:
            ch, sr, flags, pt, n = info
            lv = " ".join("%d:%s" % (c + 1, "-" if sq[c] / max(1, frames) < 1e-10 else "%.0f" % (10 * math.log10(sq[c] / frames)))
                          for c in range(ch))
            print("%d pkt/s %d fr/s lost %d | %d ch @ %d | %s%s%s | t=%d (%.2f s) | %s" % (
                pk, frames, lost, ch, sr, "PLAYING" if flags & 1 else "stopped", " ctx" if flags & 2 else " NO-CTX",
                " ambi" if flags & 4 else "", pt, pt / max(1, sr), lv))
        else:
            print("no packets")
        sys.stdout.flush()
        last, pk, frames, lost, sq = now, 0, 0, 0, [0.0] * 16
