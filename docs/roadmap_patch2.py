import sys
p = sys.argv[1]
doc = open(p).read()

def rep(old, new):
    global doc
    assert old in doc, old[:70]
    doc = doc.replace(old, new)

rep("## Kalyx View UI v2 (28–29 Oct 2026, app v21 → v32)", "## Kalyx View UI v2 (28–30 Oct 2026, app v21 → v34)")
rep("v32 **monitor mode** (NDI source or no Resolve bridge → only the View panel, centred; scene not pickable; status rows name the sender).",
    "v32 **monitor mode** (NDI source or no Resolve bridge → only the View panel, centred; scene not pickable; status rows name the sender); v33 (30 Oct) **View tab regrouped** (STREAM/PLAYBACK summary, then SOURCE · PICTURE · OVERLAYS · PANELS groups of chips, diagnostics small at the bottom; active-chip glow halved), **Fisheye 180 projection** (dual circular fisheye SBS/mono unwrapped to per-eye 180 equirect in the GPU blit, equidistant model, inscribed circle, lens FOV ±5° row); v34 **panel dock** on the Instance strip (PANELS · Placement / Look / Timeline chips: off = gone, not rolled; persisted as `panelOn`; Nelson: \"more panels but they won't all be on or present\").")
rep("- [ ] Kalyx View: fisheye-180 input (Apple Immersive / Canon dual-fisheye side-by-side) → same headset layer; auto-detect alongside 2:1 and 1:1. Next build after NDI.",
    """- [x] Kalyx View: fisheye-180 input (v33): dual circular fisheye (SBS or mono) unwrapped on the headset; verified on a synthetic graticule (`scratchpad/fisheye`, test chart `dualfisheye_3840x1920.uyvy` for the NDI test sender). Selected by hand (Projection → Fisheye 180): a 2:1 dual-fisheye frame is indistinguishable from SBS equirect by aspect alone.
- **Format decision (Nelson, 30 Oct): no Apple Immersive work.** "If someone is designing for Vision Pro they probably have one and don't need our tool." Targets are Canon dual fisheye (RF 5.2 mm, 190°; RF-S 3.9 mm, 144°) and generic 180/200/220° fisheye (dome renders). Note: the Blackmagic URSA Cine Immersive *is* the Apple Immersive camera (per-unit lens calibration, Resolve immersive timeline built for Vision Pro); its common export, VR180 equirect SBS, already works; whether a Resolve immersive timeline puts anything useful on the DeckLink output is an untested question, ranked below Canon.
- [ ] Lens presets instead of the ±5° dial (Canon 5.2 · Canon 3.9 · 180 · 200 · 220 · custom) plus a circle-radius trim (real image circles aren't exactly inscribed); optional auto-detect of a circular image by black corners.""")
open(p, "w").write(doc)
print("patched", len(doc))
