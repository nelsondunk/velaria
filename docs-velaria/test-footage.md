# Test footage for every format Velaria claims (2 Nov 2026)

> Name: renamed from Kalyx to Velaria on 31 Oct 2026. Code, macro, driver, folder and Notion/Design names that still say Kalyx are left as they are until the rename lands in the build.


Nelson, 2 Nov: "We should get some 180, Blackmagic, dome, Canon and whatever else footage so we can test everything on those formats." What exists in the wild, what it costs to get, and what each one tests. Nothing downloaded yet — the big ones are tens of gigabytes and want NYC's disk, not a headset session.

| Format | Source | What you get | Size | What it exercises |
|---|---|---|---|---|
| **Apple Immersive / Blackmagic URSA Cine Immersive** | [Blackmagic gallery page](https://www.blackmagicdesign.com/products/blackmagicursacineimmersive/gallery) — "Download Blackmagic RAW Clip" | 18 BRAW clips (Desert Offroad, Sandy Valley Ranch, Iceland), 16320 × 7200 at 90 fps, dual-fisheye stereo, plus three `.drp` Resolve projects and one `.aivu`. Needs Resolve Studio 20.1+ | 3.8–25.8 GB each; pick **Desert Landscape 2 (3.8 GB)** and **Chasing Turn (6.1 GB)** | 180° stereo fisheye in Resolve's Immersive workflow; the monitoring path at 90 fps (our preflight will flag the frame rate — that is the point); whether the bridge's lens presets (180 / 200 / 220°) describe it |
| **Canon RF 5.2 mm dual fisheye** | Canon does not publish raw camera files; the lens page has processed demos only ([Canon RF 5.2mm F2.8L](https://www.canon-europe.com/lenses/rf-5-2mm-f2-8l-dual-fisheye-lens/), [PetaPixel sample reel](https://petapixel.com/2021/10/18/this-is-what-footage-from-canons-5-2mm-dual-fisheye-lens-looks-like)) | Need a real R5 / R5 C clip (8K dual fisheye, side-by-side in one frame, Canon Log 3) | one 30 s clip ≈ 2–3 GB | the **Canon RF 5.2 (190°)** preset and the RF-S 3.9 (144°) preset in the View tab; side-by-side layout detection; the EOS VR Utility → Resolve path vs. straight BRAW-style fisheye |
| **VR180 consumer** | [Kandao gallery](https://kandaovr.com/gallery) (Obsidian / QooCam samples), Lenovo Mirage samples via [360Rumors](https://360rumors.com/lenovo-mirage-vr180-samples/) | 5.7–8K side-by-side equirect 180, H.264/H.265 | 0.5–2 GB | SBS 180 equirect — the common YouTube VR180 case; Orient 360 on a 180 plate (should refuse or clamp) |
| **Fulldome** | [ESA/Hubble fulldome clips](https://esahubble.org/Hubble25/fulldome/) (free, 4K dome masters), [NOIRLab video formats](https://noirlab.edu/public/videoformats), [FDDB 8K timelapse](https://www.fddb.org/?p=538) | 4K/8K square dome masters (azimuthal/fisheye, mono) | 1–10 GB | the one projection we **don't** have: a 180° fisheye dome master is not equirect. Needs a "Dome" projection in the headset (tilted fisheye, mono) — a new item, not a preset |
| **Insta360 Titan / Pro 2** (ours) | Nelson's own stitched masters | 7680 × 7680 TB stereo, 8K mono | — | the path everything was built on; stays the reference |
| **Flat 16:9 / 2:1** | any Resolve project | — | — | Flat projection window, PIP sources |

## Order of attack

1. **Blackmagic first** (free, public, the format the site names). Download the two small clips onto NYC overnight (`curl -L -o` from the gallery links; the links are behind a form on the page, so grab them in a browser and paste the URLs), open in KALYX TEST, drop on a 180 stereo timeline, monitor through Velaria. Expect the preflight to say 90 fps vs timeline fps and the picture to need a 180 projection + fisheye lens preset.
2. **Canon**: borrow or rent an R5 + RF 5.2 for a day, or ask a Canon rep for a native sample (they hand them out to dealers). Without a native file the two Canon presets are unverified claims.
3. **VR180 consumer**: ten minutes, Kandao gallery.
4. **Dome** is a feature, not a test: schedule it after the alpha unless a planetarium client appears.

## What "tested" means for each

Open in Resolve → appears in the headset with the right projection and eyes → preflight silent once the timeline is set up properly → Canvas Control card placed and rendered → export plays back on the Quest. Record the result per format in this file.
