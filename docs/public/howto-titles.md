# Titles: the Velaria templates

Four titles that drop onto a 360 or 180 timeline and come out placed in the sphere with real stereo depth, controllable from the Inspector or from the headset. Each is a Velaria Canvas Control with a design already wired into it, so it has the same Placement controls as a footage window plus its own Text, Look and Animation pages.

| Template | What it is | Depth |
|---|---|---|
| **Velaria Title - Clean** | A title and a line under it, left-aligned, a thin rule between. Rises in. | Placement depth (a flat graphic on the sphere) |
| **Velaria Lower Third** | Name and role on a translucent plate with a coloured edge, bottom left. The plate wipes in. | Placement depth |
| **Velaria Caption** | One line on a dark pill, centred low: subtitles, place names, a credit. | Placement depth |
| **Velaria Title 3D - Slab** | Extruded letters over a slab, lit. The slab grows in. | Depth inside the title as well |

## Use one

1. On the Edit page, put an **Adjustment Clip** over the shot (Effects > Toolbox > Effects > Adjustment Clip), on a track above it, and trim it to how long the title should stay.
2. Drag the template from **Effects > Velaria** onto the adjustment clip.
3. In the Inspector, type the words on the **Text** page. **Placement** puts it where you want it: Yaw, Pitch, Distance, Size.
4. Put the headset on. The strip is on the **Graphics** stage; the title is one card, named for what it is (*Title · Clean*, *Lower third*). Pinch it in the picture and move it, or open **Selected** and use the dials. The Text, Look and Animation pages are tabs on the same panel.

The title composites over the plate inside the macro, so it works on an adjustment clip or dropped straight on the shot.

## Controls

- **Placement:** Yaw, Pitch, Roll, Distance, Size, and the object's own turn, tilt and spin. The same as every Canvas Control card.
- **Text:** the words and the text size. The 3D title also has letter depth.
- **Look:** colours as pickers (text, rule, plate, edge, pill, slab), plate or pill opacity and width, slab on or off and its size.
- **Animation:** start frame, length in frames, and whether to animate out before the clip ends. The in and out are the same length. Clean's rise can be switched off.
- **Stereo / Output:** eye separation and window depth, as on Canvas Control; Equirect Width should match your timeline width.

## What to know

- The three flat templates get their depth from where you place them; letters and plate sit at one distance. The Slab title has depth inside it, so its letters stand off the slab in the headset. Keep flat titles at two metres or more; closer than that a flat card reads as a cutout.
- Fonts are Open Sans, which DaVinci Resolve installs. The Inspector lets you change the font on any of them; a font the other Mac does not have fails the render there, as with any Resolve title.
- The 3D title renders slower than the flat ones (a lit 3D scene per eye). On a long timeline, cache the adjustment clip.
- Resolve's own Titles and any Fusion title also work: drag a **Velaria Canvas Control** onto the adjustment clip, plug the title into its Graphic input on the Fusion page, and set Window shows to Graphic. The templates save you that wiring.
