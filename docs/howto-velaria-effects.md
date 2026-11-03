# Velaria effects in Resolve — how-to

The three things you place with Velaria, as they appear in Resolve's Inspector and in the headset. Resolve Studio 19+, Fusion library installed by the Velaria installer (Edit page ▸ Effects ▸ **Velaria**; Fusion page ▸ Effects Library ▸ Macros).

## 1. Footage window or flat title — Velaria Canvas Control

What it is: one node that places a rectangle on the sphere — footage (a PIP), a flat graphic, or a 3D scene — at a pan, tilt, distance and size, in true stereo.

**Drop it on a clip above the plate** (a stacked clip on V2 or higher), not on the plate itself: its output is the window on transparency, composited by the timeline. On the Fusion page it wants:

- *Left / Mono Footage* ← the clip (MediaIn1). For stereo footage in two files, *Right Footage* ← the right clip and **Mono/Stereo** = Stereo. One file with both eyes: add a **Splitter** first (Vert for top/bottom, Horiz for side by side); Image1 → Left, Image2 → Right.
- *Graphic* ← a Text+ or any flat title, when **Window shows** = Graphic (alpha kept, no mask or frame).
- *3D Scene* ← a Velaria 3D Title or your own FBX/OBJ scene, when **Window shows** = Nothing (3D only).

Inspector pages:

| Page | Controls | Notes |
|---|---|---|
| Frame | Window shows (Footage / Graphic / Nothing), Window Aspect (1.778 = 16:9), Window Size, Crop Sides, Crop Top & Bottom, Frame Colour, Frame Thickness, Corner Radius | Crop shrinks the window to the content (pillarbox / letterbox bars), nothing zooms. Window Aspect means the window keeps its shape on a square 360 timeline. |
| Glow | Glow Amount (0 = off), Glow Size, Glow Brightness | The glow is a blurred copy of the footage behind the frame. |
| Placement | Yaw (Pan), Pitch (Tilt), Roll, Distance (−3 = 3 m in front), Object Turn / Tilt / Spin | Yaw/Pitch/Roll move it on the sphere; Object Turn/Tilt/Spin rotate the window in its own plane. |
| Stereo / Output | Mono (0) / Stereo (1), Window Depth in Z, Equirect Width (per eye) | Window Depth shifts convergence: + = toward the viewer. Output width 2880 per eye is the default; match your plate for pixel-exact composites. |

In the headset the window is on the **Overlays** layer: pinch it in the picture and move, or tap its name on the strip and use the Placement panel. The Look panel carries the Frame, Glow and Stereo pages.

## 2. 3D title with real depth — Velaria 3D Card (Skylight-internal today; templates later)

> **Not in the alpha package.** The Vine / Salmon / Hay Bale cards are Skylight's own project cards and are not shipped. Testers build 3D depth through Canvas Control's *3D Scene* input (end of this section). A set of chooseable 3D card templates is planned for beta/launch.

What it is: Canvas Control and a 3D title in one node, pre-wired. **Drop it straight on the plate clip** (or an adjustment clip): it composites the card over the plate itself. Output 2, *Card only*, is the card on transparency if you'd rather put it on its own track.

Inspector pages:

| Page | Controls |
|---|---|
| Placement | Yaw, Pitch, Roll, Distance, Object Turn / Tilt / Spin |
| Stereo / Output | Mono / Stereo, Window Depth, Equirect Width |
| Text | Title (auto CAPS), Body Text, Title scale (1 = as designed) |
| 3D Model | Show 3D Model, Model Kit Folder, Model Scale / Position X Y Z / Rotation X Y Z, Model Brightness / Contrast / Saturation |
| Animation | Animation Start (frame), Animation Length (×), Fade out after (seconds, 0 = never), Sway amount |
| Environment | Environment is over/under (left eye on top) |

Animation: the title fades in over the first 24 frames, the plate grows from frame 8, the body text from 24, the model from 30 (all × Animation Length). The card then **stays for the length of the clip** unless *Fade out after* is set.

Environment: the card's plate reflects the footage around it. The plate it is dropped on is the environment; tick *over/under* for a top/bottom stereo plate, untick for mono.

Model Kit Folder: the folder holding the kit's FBX files and textures. Today these point at Skylight's project drives; on another Mac set this to wherever the kit lives, or turn *Show 3D Model* off.

Building your own: on the Fusion page, **Velaria 3D Title · Vine** (Macros) is the title alone, with a 3D Scene output and an *Environment* input; connect it to Canvas Control's *3D Scene* and set Window shows = Nothing. Any 3D scene built before Canvas Control (FBXMesh3D → materials → lights → Merge3D) goes into the same input; Velaria places it, your scene lights it.

## 3. Patches and re-orientation — Velaria Orient 360

What it is: a stereo re-orientation of a top/bottom 360 plate at the **end** of the chain — after the LatLong Patcher paint-outs — so patches turn with the plate. Splits the eyes, PanoMaps both identically, recombines. Inputs: the T/B plate. Controls: Pan, Tilt, Roll (Placement page).

Add it on the Fusion page, between the last patch/paint and MediaOut. (Dropped from the Edit page onto a clip that already has a comp, Resolve adds it unwired — wire it on the Fusion page.) A mono plate takes a plain PanoMap instead and shows up on the same headset layer.

In the headset: the **Picture** layer shows Orient 360 / PanoMap with pan, tilt and roll; turn on the **Level grid** (View tab) and bring the horizon to the copper line. The **Patches** layer lists each LatLong Patcher window; lock the ones you don't want a stray pinch to move.

## 4. A trim pass from the headset — Velaria Trim

What it is: one Fusion ColorCorrector in a macro, meant for an **adjustment clip** on top of the stack so it sees every grade below it. Not a grading tool (the Quest panel is not a reference display); a trim you make on the device the audience will wear, that Resolve's colour page never sees and that you can throw away.

1. Put an adjustment clip over the section (Effects ▸ Toolbox ▸ Effects ▸ Adjustment Clip) and drop **Velaria Trim** on it from Effects ▸ Velaria.
2. Controls, all on one *Trim* page: Exposure (an offset, −0.5…+0.5), Lift, Gamma, Gain, Contrast, Saturation, **Temperature** (blue ↔ amber) and **Tint** (green ↔ magenta), both approximated with per-channel gain, and **Amount** (0 turns the trim off, for before/after). Every one of them keyframes and undoes like any other Resolve control.
3. In the headset: the **Picture** layer lists the trim as *Trim*; tap it and the Placement panel becomes **TRIM** — the nine bars, with nudge chips.
4. When the trim is right, either leave it (it renders in the export like any Fusion effect) or read the values off the Inspector and build them into the grade on the colour page.

Why a Fusion node and not the colour page's own CDL: Resolve's scripting can *set* a node's CDL but not read it back, so the headset would be blind to the colourist's wheels and would overwrite them. The Fusion route is readable, writable, keyframeable, undoable, and it stacks on top of the real grade instead of replacing it.

## Quick answers

- *The window is black / the card is a dark slab:* the macro in your comp is an old copy; delete it and drop the current one from Effects ▸ Velaria (Resolve bakes a copy of the macro into each clip).
- *White bars above and below the footage:* Window Aspect is 1.778 but the footage isn't 16:9 — set Window Aspect to the footage's ratio, or use Crop.
- *The card ignores the headset:* the strip is on the wrong layer, or the group is locked (lock icon on its name).
- *Nothing in Effects ▸ Velaria:* Resolve reads the library at launch — restart it after installing.
