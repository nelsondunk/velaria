# Velaria macro render regression

Every Velaria macro rendered through DaVinci Resolve's export path (the one that fails with "The Fusion composition at … could not be processed successfully" when something is wrong), on a Mac that is not the one the macros were built on. First run: 31 Oct 2026 on NYC (macOS 26.3.1, Resolve 21.1.0.17, clean Velaria 1.0.7 install, project "KALYX TEST").

## How it runs

- `macros/gen_regression.py` builds one Fusion comp per variant from the `.setting` files: `MediaIn1 → macro(s) → MediaOut1` with the variant's published inputs set on the inner tools. A comp must reference a macro's output by its **inner** tool (`Stack_Eyes_OverUnder.Output`); `MacroName.MainOutput1` imports but renders nothing.
- On the test Mac, `rq/s_regress.py` (driven by `rq.py`, Resolve's scripting API) keeps a scratch timeline **Kalyx regression** (5760 × 2880, project frame rate), one 60-frame clip per variant (a real BRAW clip from the project — generator clips cannot take an imported comp), imports the comp with `TimelineItem.ImportFusionComp`, renders frames 40–41 to `out/<variant>/` as a render job, and records the job status, the files produced and every ERROR line Resolve logged during that job in `result.json`.
- Pass = job Complete with a file on disk. Fail = job Failed, or no file, or a 4-minute timeout.

## Variants

| id | what |
|---|---|
| cc_ws0 / ws1 / ws2 | Velaria Canvas Control, Window shows = Footage / Graphic / Nothing, footage and graphic fed from the clip |
| title_Vine / Salmon / HayBale | Velaria 3D Title → Canvas Control's 3D Scene input, Window shows = Nothing (the only way a title becomes pixels) |
| card_X_ws0 / ws1 / ws2 | Velaria 3D Card (Vine, Salmon, Hay Bale), Window shows 0 / 1 / 2 |
| card_X_hold1 | fade out after 1 s (frame 40 is mid-fade) |
| card_X_env0 | Environment not over/under |
| card_X_out2 | output 2 (card only, transparent) |
| card_Vine_missingkit | kit folder pointed at a path that does not exist |
| orient360 | Velaria Orient 360, pan 30 |
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

### Pass 2 — 2 Nov 2026 08:08–08:12, NYC with Montserrat installed, regenerated macros, Resolve relaunched by Nelson

| variant | result | seconds | why |
|---|---|---|---|
| cc_ws0, cc_ws1, cc_ws2 | **PASS** | 3–7 | — |
| orient360 | **PASS** | 2.4 | — (the FBX error lines in its log window belong to the missingkit run just before it) |
| **trim_warm** | **PASS** | 1.8 | Velaria Trim renders through the export path |
| title_Vine, title_HayBale | **PASS** | 6.6 / 9.8 | fonts fixed it. Hay Bale still logs `Could not locate pool item "/Volumes/The Baxter Building/2609…"` at INFO level and **completes** — a missing texture is non-fatal |
| card_Vine_* (6) | **PASS** | 4.6–8.0 | — |
| card_HayBale_* (6) | **PASS** | 7.5–13.2 | — |
| title_Salmon, card_Salmon_* (6) | FAIL | — | `Could not locate pool item "/Volumes/Earth-1610/2608 - BC Salmon - …"` ×5 then the job fails: the Salmon kit's **mesh** is found through the copied kit, but its material textures live under the *project* folder on Earth-1610, not under `Salmon_Kit/`, so the kit-relative rewrite did not catch them. Moot: the Salmon card is Skylight-internal and not shipped (2 Nov) |
| card_Vine_missingkit | FAIL | — | `FBX Error: "Unexpected file type"` — by design: a missing kit still fails the export rather than degrading |

Every "Error occured during exporting subtitle sidecar file" line is the harness's render preset (subtitle export on, no subtitles), not the library.

**What this says.** On a second Mac, with the fonts the package now installs, everything that ships (Canvas Control ×3, Orient 360, Trim) and everything Skylight uses internally with a reachable kit (Vine, Hay Bale titles and cards, 12 variants) exports. The two failure modes left are both "a file the macro points at is not on this Mac": a texture only logs, a mesh fails the job. Rig lesson that cost a day: Resolve relaunched headless over ssh comes up behind a modal; the pass needed Nelson's own relaunch.

### Pass 3 — 2 Nov 2026 19:02–19:06, after the rename (Velaria_* macros, regenerated comps)

Same result as pass 2: **cc_ws0–2, orient360, trim_warm, title_Vine, title_HayBale, card_Vine ×6, card_HayBale ×6 pass (21)**; title_Salmon + card_Salmon ×6 fail on the project-folder textures (internal card, not shipped); card_Vine_missingkit fails by design. The rename changed nothing in what renders, which is the point of the pass.

Rig note: right after the missing-kit job fails, the next `AddRenderJob` returned an empty id twice in a row (orient360 and trim_warm showed "FAIL None, 0 files" with no error); re-running those two variants on their own passed in 2.6 s each. The harness should retry `AddRenderJob` once after a failed job, or run the missing-kit case last.

### Pass 2 as first planned (31 Oct, for the record)

Blocked by the test rig, not the library: after Resolve is quit and relaunched over ssh on the headless NYC, its UI ends up behind a modal (the scripting API answers queries but `GetCurrentPage()` is `None` and `AppendToTimeline` / `ImportFusionComp` / `AddRenderJob` return nothing). One relaunch out of four came up clean (page Edit, edits worked) and then a 26-clip `DeleteClips` raised a confirmation dialog that cannot be dismissed without a screen. Harness changes made for the re-run: no bulk deletes (clips are reused, comps replaced per item), `LoadFusionCompByName` after import, job status tolerant of `None`.

**To finish pass 2 (five minutes once Resolve is sane):** on NYC via screen sharing, dismiss whatever Resolve is showing, open *KALYX TEST*, then from Bruce: `ssh -4 nyc.local 'cd ~/kalyx-test/rq && rm -rf out result.json && nohup python3 rq.py s_regress.py > regress.out 2>&1 &'`; results land in `result.json`. Expected: every title/card variant passes except `card_Vine_missingkit` (by design) — and card_HayBale_* tells us whether the Hay Bale "pool item" line was a real missing file.
