# Kalyx macro render regression

Every Kalyx macro rendered through DaVinci Resolve's export path (the one that fails with "The Fusion composition at … could not be processed successfully" when something is wrong), on a Mac that is not the one the macros were built on. First run: 31 Oct 2026 on NYC (macOS 26.3.1, Resolve 21.1.0.17, clean Kalyx 1.0.7 install, project "KALYX TEST").

## How it runs

- `macros/gen_regression.py` builds one Fusion comp per variant from the `.setting` files: `MediaIn1 → macro(s) → MediaOut1` with the variant's published inputs set on the inner tools. A comp must reference a macro's output by its **inner** tool (`Stack_Eyes_OverUnder.Output`); `MacroName.MainOutput1` imports but renders nothing.
- On the test Mac, `rq/s_regress.py` (driven by `rq.py`, Resolve's scripting API) keeps a scratch timeline **Kalyx regression** (5760 × 2880, project frame rate), one 60-frame clip per variant (a real BRAW clip from the project — generator clips cannot take an imported comp), imports the comp with `TimelineItem.ImportFusionComp`, renders frames 40–41 to `out/<variant>/` as a render job, and records the job status, the files produced and every ERROR line Resolve logged during that job in `result.json`.
- Pass = job Complete with a file on disk. Fail = job Failed, or no file, or a 4-minute timeout.

## Variants

| id | what |
|---|---|
| cc_ws0 / ws1 / ws2 | Kalyx Canvas Control, Window shows = Footage / Graphic / Nothing, footage and graphic fed from the clip |
| title_Vine / Salmon / HayBale | Kalyx 3D Title → Canvas Control's 3D Scene input, Window shows = Nothing (the only way a title becomes pixels) |
| card_X_ws0 / ws1 / ws2 | Kalyx 3D Card (Vine, Salmon, Hay Bale), Window shows 0 / 1 / 2 |
| card_X_hold1 | fade out after 1 s (frame 40 is mid-fade) |
| card_X_env0 | Environment not over/under |
| card_X_out2 | output 2 (card only, transparent) |
| card_Vine_missingkit | kit folder pointed at a path that does not exist |
| orient360 | Kalyx Orient 360, pan 30 |
| p_direct / p_inner / p_inner_ws0 | harness probes: pass-through, and Canvas Control via the inner output |

## Results

### Pass 1 — 31 Oct 2026 09:14–09:21, NYC as installed (Kalyx 1.0.7, no Montserrat, no Salmon kit)

| variant | result | seconds | why |
|---|---|---|---|
| cc_ws0, cc_ws1, cc_ws2 | **PASS** | 5–7 | — |
| orient360 | **PASS** | 4.5 | — |
| title_Vine / Salmon / HayBale | FAIL | 6–16 | `Could not find font: Montserrat` → `Exception caught while executing Fusion algorithms: Fonts Not Found` → the whole scene chain "cannot get Parameter" → **job fails** |
| card_Vine_* (6) | FAIL | 14–28 | same: fonts |
| card_Salmon_* (6) | FAIL | 4–8 | fonts, and `Could not locate pool item "/Volumes/Earth-1610/…/Salmon_Kit/…"` (the kit's textures) — plus the Alembic mesh was an absolute path, so even a ModelKit override could not have found the fish |
| card_HayBale_* (6) | FAIL | 16–41 | fonts, and `Could not locate pool item "/Volumes/The Baxter Building/2609…/HayBale_Kit/…"` (to be read again in pass 2; the folder does exist on NYC's mount) |
| card_Vine_missingkit | FAIL | 19.5 | `FBX Error: "FBX SDK error: "Unexpected file type" while importing file` — a missing kit fails the export, it does not degrade to "no model" |
| p_direct, p_inner, p_inner_ws0 | PASS | 4–7 | harness probes |

**What this says.** The parts of the library that carry no outside dependency (Canvas Control, Orient 360) export cleanly on a Mac that is not Bruce. Everything with a 3D Title in it fails there, and fails the way Nelson's Salmon export failed: "The Fusion composition at … could not be processed successfully". Two causes, both ours, both now fixed in the source:

1. **Fonts.** The titles are set in Montserrat Medium and ExtraBold Italic. A Mac without them does not fall back to another face — Resolve's Text3D throws *Fonts Not Found* and the frame is refused. Fix: `fonts/Montserrat-{Medium,ExtraBoldItalic}.ttf` (SIL OFL, licence beside them) ship in the package and `install.sh` copies them to `~/Library/Fonts` (Resolve reads fonts at launch). On NYC they are installed by hand for pass 2.
2. **Model kit paths.** The Salmon title's Alembic mesh (`Salmon_Swim.abc`) was a bare absolute path on Earth-1610, unlike every other model file, which follows `CARD_CONTROLS.ModelKit`. `make_3dtitle.py` now rewrites every `ImportFile` / `Filename` under the kit's default folder into a kit-relative expression, and the three titles and three cards were regenerated and redeployed (Bruce and NYC libraries, dated backups in `Fusion-archive/2026-10-31`).

Still open from this pass: the kits themselves (Vine 22 MB, Salmon 131 MB, Hay Bale 167 MB) live on project volumes. A tester gets a 3D Card only if the kit travels with the package or they point *Model Kit Folder* at a copy; a missing kit fails the export outright. That is the licence/bundling decision already on Nelson's list, now with sizes.

### Pass 2 — fonts installed, Salmon kit copied to NYC, fixed macros — **not yet run to completion**

Blocked by the test rig, not the library: after Resolve is quit and relaunched over ssh on the headless NYC, its UI ends up behind a modal (the scripting API answers queries but `GetCurrentPage()` is `None` and `AppendToTimeline` / `ImportFusionComp` / `AddRenderJob` return nothing). One relaunch out of four came up clean (page Edit, edits worked) and then a 26-clip `DeleteClips` raised a confirmation dialog that cannot be dismissed without a screen. Harness changes made for the re-run: no bulk deletes (clips are reused, comps replaced per item), `LoadFusionCompByName` after import, job status tolerant of `None`.

**To finish pass 2 (five minutes once Resolve is sane):** on NYC via screen sharing, dismiss whatever Resolve is showing, open *KALYX TEST*, then from Bruce: `ssh -4 nyc.local 'cd ~/kalyx-test/rq && rm -rf out result.json && nohup python3 rq.py s_regress.py > regress.out 2>&1 &'`; results land in `result.json`. Expected: every title/card variant passes except `card_Vine_missingkit` (by design) — and card_HayBale_* tells us whether the Hay Bale "pool item" line was a real missing file.
