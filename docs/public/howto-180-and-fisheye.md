# 180, 360 and fisheye: picking the right view

Velaria View has to know two things about the picture: how the eyes are packed (the layout) and what shape the picture is (the projection). It works both out on its own in most cases. This page covers when to set them by hand.

Both settings are in the headset: look down, open the **Look** panel, **View** tab, under **Picture**. Tap a setting to step through its choices. Your choice is remembered.

## What Auto does

| The picture Resolve sends | Auto shows it as |
|---|---|
| Twice as wide as it is tall (for example 5760 x 2880) | Side by side, 180° |
| Square (for example 3840 x 3840) | Over/under, 360° |

Anything else needs setting by hand. The common case is a **mono 360** shot at 2:1: set Layout to *Mono* and Projection to *360°*.

## Layout

*Auto*, *Mono*, *Side by side*, *Over/under*.

**Swap eyes** is next to it. If depth looks inside out, or near things are uncomfortable to look at, the eyes are the wrong way round. Tap it.

## Projection

*Auto*, *Flat*, *180°*, *360°*, *Fisheye 180°*.

- **180° and 360°** expect an equirectangular picture, the stretched map shape that stitching software and most VR180 cameras' own software produce.
- **Fisheye 180°** is for footage straight from a dual-fisheye lens, with two circles side by side and nothing converted yet. Velaria unwraps the circles in the headset so you can review camera originals before you convert them.
- **Flat** shows the picture on a flat screen in front of you.

## Fisheye lens presets

With Projection on *Fisheye 180°*, a row of lens presets appears. Each sets the field of view the image circle covers:

| Preset | Field of view |
|---|---|
| Canon 5.2 | 190° |
| Canon 3.9 | 144° |
| 180° | 180° |
| 200° | 200° |
| 220° | 220° |

Under the presets, the **5°** minus and plus chips trim the value between 120° and 240°. Velaria assumes each eye's image circle touches the edges of its half of the picture.

If straight lines bow outward or the world seems to swim when you turn your head, the field of view is too low or too high. Step it by 5° until a straight edge stays straight as you look across it.

The fisheye view is for checking framing, depth and comfort. It is not a calibrated lens conversion, and it does not correct for the small differences between individual lenses. Judge final geometry on your converted footage.

## Quick answers

- **A 360 shot looks like a flat band, or the top and bottom are missing.** A square timeline needs **Timeline Settings > Output scaling > 3840 x 3840** in Resolve while you monitor. The headset's status line tells you when this is the cause.
- **The picture is doubled, one eye beside the other.** Layout is wrong. Set it to *Side by side* or *Over/under* to match the timeline.
- **Everything is the right shape but feels wrong.** Try **Swap eyes**.
