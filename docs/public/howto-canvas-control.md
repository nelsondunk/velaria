# Canvas Control: a footage window or a title

**Velaria Canvas Control** places a rectangle on the sphere: a piece of footage (a picture-in-picture window), a flat graphic or title, or your own 3D scene. You set its pan, tilt, distance and size, in true stereo, from Resolve's Inspector or by hand in the headset.

You need DaVinci Resolve Studio 19 or newer and the Velaria effects, which the installer puts in **Edit page > Effects > Velaria** and in the Fusion page's Macros.

## Put a window on a shot

1. On the Edit page, put the clip you want in the window on a track **above** your 360 or 180 shot (V2 or higher).
2. Drag **Velaria Canvas Control** from Effects > Velaria onto that upper clip. Do not drop it on the background shot itself: the effect outputs the window on a transparent background, and the timeline lays it over the shot.
3. Put the headset on. The window is on the **Overlays** layer. Pinch it in the picture and move it, or tap its chip on the strip and use the Selected panel.
4. Let go. Resolve renders the change and the strip shows how long it took.

## A flat title or graphic

Open the clip on the Fusion page. Connect a Text+ or any flat graphic to the **Graphic** input and set **Window shows** to *Graphic*. The graphic keeps its own transparency and gets no frame.

## Stereo footage in the window

On the Fusion page the effect has these inputs:

- **Left / Mono Footage**: the clip.
- **Right Footage**: the right-eye clip, when the two eyes are in separate files. Set **Mono / Stereo** to Stereo.
- One file with both eyes: add Resolve's **Splitter** first (Vert for over/under, Horiz for side by side) and connect its two outputs to Left and Right.

## Your own 3D scene

Build a 3D scene in Fusion (for example FBXMesh3D, materials, lights, Merge3D) and connect it to the **3D Scene** input. Set **Window shows** to *Nothing*. Velaria places the scene; your scene lights it.

## The controls

| Inspector page | Controls | Notes |
|---|---|---|
| Frame | Window shows (Footage, Graphic or Nothing), Window Aspect, Window Size, Crop Sides, Crop Top & Bottom, Frame Colour, Frame Thickness, Corner Radius | Window Aspect is width divided by height: 1.778 is 16:9. Crop shrinks the window to the content. Nothing is zoomed. |
| Glow | Glow Amount, Glow Size, Glow Brightness | The glow is a blurred copy of the footage behind the frame. Amount 0 turns it off. |
| Placement | Yaw (Pan), Pitch (Tilt), Roll, Distance, Object Turn, Object Tilt, Object Spin | Yaw, Pitch and Roll move the window around you. Turn, Tilt and Spin rotate the window in place. Distance is negative going forward: -3 is 3 m in front. |
| Stereo / Output | Mono / Stereo, Window Depth in Z, Equirect Width (per eye) | A positive Window Depth brings the window toward the viewer. Match Equirect Width to your shot for pixel-exact results. |

In the headset, the **Placement** panel carries the Placement page and the **Look** panel carries Frame, Glow and Stereo.

## Quick answers

- **The window is black.** The clip carries an old copy of the effect. Delete it and drop the current one from Effects > Velaria. Resolve stores a copy of an effect in each clip.
- **White bars above and below the footage.** Window Aspect is 1.778 but the footage is not 16:9. Set Window Aspect to the footage's ratio, or use Crop.
- **After a Velaria update, an old clip lacks a new control.** Open the clip on the Fusion page and run **Workspace > Scripts > Comp > Skylight_UpgradeMacros**. It swaps each Velaria effect in that composition for the current one and keeps your values, keyframes and connections. It is one undo step.
- **The window ignores the headset.** The strip is on another layer, or the window is locked (a lock on its chip).
- **Nothing in Effects > Velaria.** Resolve reads its effects when it starts. Restart it after installing.
