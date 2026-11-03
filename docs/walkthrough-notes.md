# Velaria walkthrough video — bullet notes (working draft, 30 Oct 2026; wording refreshed for UI v3 on 1 Nov)

Audience: an immersive editor on a Mac with a Quest, cutting in Resolve Studio. Each section is one chapter of the video. Bullets are what to show and say, not a script. Spoken lines in quotes are suggestions.

## 0. Cold open (15 s)
- Headset on, timeline playing, a card gets pinched and moved; Resolve updates behind it.
- "Stop exporting to see your cut."

## 1. Setup (one chapter, under two minutes)
- Mac: run the installer. Resolve ▸ Preferences ▸ Video and Audio I/O ▸ pick **Velaria (Quest)**. Square 360 timelines: set Output scaling to 3840.
- Headset: the free Velaria View app (sideload today; our guide walks through developer mode, and the installer puts the app on with its own bundled adb — `sudo ./install.sh --headset`). Plug the USB cable in or stay on Wi‑Fi; it finds the Mac on its own.
- If Resolve is set up wrong, the strip says so with the fix in Resolve's own menu words (output scaling on a square timeline, monitoring raster, frame rate, no picture). Show one on purpose: set a square timeline without output scaling, read the line, fix it, watch it clear.
- Press play in Resolve. The picture is in the headset.
- Status strip at the top: **Waiting to connect** → **USB · Resolve**. If it says "Premiere / After Effects", tap SOURCE on the View tab back to Auto.

## 2. Formats (show each for ten seconds)
- 360 over/under stereo, 360 mono, VR180 side by side, Canon dual fisheye, flat. Layout and projection are auto‑detected; two taps on the View tab if you need to force one.
- Level grid on (Look panel ▸ View tab ▸ Overlays ▸ Level grid): copper horizon, yaw ticks — judge the horizon in the headset, not on a flat monitor.
- Fisheye: the View tab's lens presets (Canon RF 5.2 mm 190°, RF-S 3.9 mm 144°, 180/200/220, ±5° trim).
- Scopes: the fifth panel — waveform + vectorscope, parade, histograms — measured on the Mac from the uncompressed frame Resolve plays out, so they read the monitoring signal, not the headset's compressed picture.

## 3. Velaria Canvas Control — the one node you place things with
- Edit page: Effects ▸ **Velaria ▸ Velaria Canvas Control** onto a clip above the plate (or Fusion page: Macros ▸ Velaria Canvas Control). Rename the inspector tabs as you go: Frame, Glow, Placement, Stereo / Output.
- **Monoscopic rectilinear footage:** connect the clip to *Left / Mono Footage*. Frame colour, thickness, corner radius, glow.
- **Stereoscopic rectilinear footage:**
  - Two files: left clip → *Left / Mono Footage*, right clip → *Right Footage*. Set **Mono (0) / Stereo (1)** to 1.
  - One file with both eyes (top/bottom or side by side): add a **Splitter** node first (Split = Vert for top/bottom, Horiz for side by side); Image1 → Left, Image2 → Right. Mention that most cameras deliver one file; only pro rigs give you separate eyes.
- **Crop:** *Crop Sides* for pillarbox bars, *Crop Top & Bottom* for letterbox bars. The window shrinks to the content; nothing zooms.
- **Flat titles and stock graphics:** connect a Text+ or any Fusion title to the *Graphic* input and set **Window shows** to 1 (Graphic); mask and frame are bypassed, alpha is kept, glow optional. *Window Aspect* sets the window's shape (16:9 by default) whatever the timeline format is.
- **The one-drop version (lead with this):** Edit page ▸ Effects ▸ Velaria ▸ **Velaria 3D Card · Vine** onto the plate clip. Nothing to wire: it is Canvas Control + the Vine title in one node. Inspector: Placement, Stereo / Output, Text, 3D Model, Animation, Environment. Headset sees it as one TITLE card.
- **3D titles with real depth (Velaria 3D Titles), the Fusion-page version:** Macros ▸ *Velaria 3D Title · Vine* (or Salmon, Hay Bale) next to Canvas Control; its output goes to the **3D Scene** input, the plate goes to the title's **Environment** input (that's what the card reflects — tick *over/under* for a top/bottom plate), and Canvas Control's **Window shows** goes to 2 (nothing: 3D only). Text, model and animation live on the title node; position lives on Canvas Control. Depth is real stereo, not a flat card. Mention: the title node has no placement controls any more, on purpose.
- **True 3D objects (FBX / OBJ):** everything — loader, materials, lights — is built *before* the 3D Scene input. Velaria places the finished scene; it doesn't light or texture it. Show the node tree: FBXMesh3D → materials → Merge3D → Canvas Control ▸ 3D Scene.

## 4. In the headset: Velaria Canvas
- The strip (UI v3): the layer control **Overlays · Patches · Picture** with a count of what is at this frame; one named chip per thing on the layer (copper = what your hand moves); Copy / Paste / Undo as icon chips; the four icons on the right show and hide Placement, Look, Timeline and Scopes (raised = shown); the chevron rolls the strip up. Below: timecode · track · clip · what the overlay is built from, then the ice status line (USB / Wi-Fi, software, "Resolve updated in 0.4 s").
- Look panel ▸ View tab, bottom right: the versions line (headset build · Mac package · Resolve) — mention it as "what to screenshot when you report something".
- Placement panel: pan ring, tilt arc, distance and scale throttles, roll / card pitch / card yaw / card roll dials. Pinch a dot and move; tap a dial to zero it.
- Or just pinch the card in the picture and drag it. Release; Resolve renders; "Resolve updated in 0.4 s".
- Keyframes: the ◆ next to any control. Marks on the Timeline deck.
- Look panel tabs: Border, Image (fill, crop), Glow, Stereo (depth), Output — bars with − / + nudges, switches on copper, the ◆ keyframe beside each control.
- Undo is Resolve's undo. Nothing happens in the headset that isn't a real project change.

## 5. Patches, re‑orientation and the trim pass (the Patches and Picture layers)
- Your tripod/nadir paint‑out chain (LatLong Patcher + Paint) stays set up in Resolve. By default the headset does **not** touch it: those tools only appear when you switch the strip to **Patches**.
- On Patches nothing in the picture is pickable (tap a name instead), the tag is ice (PATCH), and each patch can be **Locked** so a stray pinch can't move it — a lock icon on its name; Lock is remembered.
- **Picture** is the whole sphere: PanoMap / Velaria Orient 360 (PICTURE tag) and the **Velaria Trim** pass (TRIM tag) — one node on an adjustment clip with exposure, lift, gamma, gain, contrast, saturation, temperature and tint as bars on the Placement panel (TRIM). "A trim on the device the audience will wear; the colour page never knows."
- Re‑orienting a whole stereo 360 plate at the end of the chain: **Velaria Orient 360** (Effects ▸ Velaria). Feed it the top/bottom clip *after* all patches and paint: it splits the eyes, PanoMaps both identically, recombines. Pan / Tilt / Roll from the headset with the level grid on. Mono plate: a plain PanoMap does the same job and shows up on the same layer.

## 6. Premiere Pro and After Effects (monitor only)
- Install the free NDI Tools. Premiere/AE ▸ Preferences ▸ Playback ▸ Mercury Transmit ▸ enable NDI.
- Headset SOURCE: Auto finds it; or set Premiere / After Effects explicitly.
- It sends while the program monitor refreshes: scrub or play to see frames; a parked playhead holds the last frame.
- No cards or controls here yet — After Effects controls are on the roadmap, Premiere after.

## 7. Close
- Roadmap page: what works today, what's next. Alpha users set the order.
- "Join the alpha" — velaria site.

## Things to decide before shooting
- Project to demo on (Strange Worlds has the best cards; a NOFIA 360 shot has the patch chain).
- Whether to show sideloading on camera or cut to the written guide.
- Pace: Apple‑style cold open from the storyboard, then calm screen capture. Headset capture via scrcpy/adb at 1080p; Mac capture at 4K.
