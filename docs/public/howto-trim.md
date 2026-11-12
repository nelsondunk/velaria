# Trim: a trim pass from the headset

**Velaria Trim** is a small set of picture controls you adjust while wearing the headset, on the device your audience will wear. It is not a grading tool. The Quest's screen is not a reference display. Use Trim to judge brightness, contrast and colour balance in the headset, then keep the result or build it into your grade.

## Set it up

1. On the Edit page, put an **Adjustment Clip** over the section (Effects > Toolbox > Effects > Adjustment Clip), on the top track so it sees everything below it.
2. Drag **Velaria Trim** from Effects > Velaria onto the adjustment clip.
3. In the headset, switch the strip to **Picture** and tap **Trim**. The Placement panel becomes **TRIM**.

## The controls

All nine are on one Inspector page, *Trim*, and as bars in the headset:

| Control | What it does |
|---|---|
| Exposure | An offset, from -0.5 to +0.5 |
| Lift, Gamma, Gain | Shadows, midtones, highlights |
| Contrast | |
| Saturation | |
| Temperature | Blue to amber |
| Tint | Green to magenta |
| Amount | 0 turns the trim off. Use it for before and after. |

Each bar has nudge chips for exact steps. Every control keyframes and undoes like any other Resolve control.

## When the trim is right

Either leave it in place, and it renders in your export like any other effect, or read the values off the Inspector and build them into your grade on the Color page, then delete the adjustment clip.

## Good to know

- Trim sits on top of your grade. It does not read or change the Color page.
- Temperature and Tint are approximations made with per-channel gain. They are for a trim, not a white balance.
- The scopes in the headset are measured on the Mac from the picture Resolve sends, before it is compressed for the headset.
