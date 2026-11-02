# Velaria Stitch and Publish — state and packaging plan (1 Nov 2026)

> Name: renamed from Kalyx to Velaria on 31 Oct 2026. Code, macro, driver, folder and Notion/Design names that still say Kalyx are left as they are until the rename lands in the build.


What the site calls *Velaria Stitch* and *Velaria Publish* are two existing Nelson tools, found on Bruce (read-only look, nothing changed). Both are real and both work for Nelson; neither is a download a stranger can run today.

## Stitch — codename Thimble (`~/Documents/Claude/Projects/Insta360 Stitching/`)

- **What it is:** a drag-and-drop batch stitcher for **Insta360 Titan** footage that drives ProStitcher's command line (`ProStitcher -w stitch -x job.xml`) from a canonical template XML plus each clip's `pro.prj`. Locked production settings (slow sampling, top/bottom stereo, 7680×7680, ProRes 422 HQ), per-clip reference frame / blend angle / mode, test stitches, mode comparison, QC thumbnails, half-res H.265 proxies, resume safety. Python 3 (`titan_batch_stitch.py` CLI, `titan_stitcher_core.py`, `titan_stitcher_gui.py`; ~2,300 lines), `README_GUI.md` is a good user guide already.
- **Build state:** PyInstaller app **`dist/Thimble.app` + `Thimble.dmg` (39 MB, 19 Aug 2026)**, plus a Windows build script. Unsigned, un-notarised.
- **Dependencies on the user's machine:** Insta360 Stitcher (for the ProStitcher binary; free from insta360.com), FFmpeg (`brew install ffmpeg`), and the v4 engine on Windows for the AI/Fast modes. Titan only — the Pro 2 / X-series path is not in it.
- **Honest label for the site:** "Insta360 Titan batch stitcher (Mac + Windows), needs Insta360 Stitcher and FFmpeg". Not "Insta360 batch stitching" in general.
- **Packaging plan:** (1) ship the DMG as a separate download, not inside the Velaria zip (39 MB, different audience, different dependencies); (2) `README_GUI.md` becomes its page, with the dependency list at the top; (3) notarisation rides on the same Developer ID as the Velaria installer; (4) rename in the app window and DMG only when the code rename happens (today it still says Titan Stitcher / Thimble inside).

## Publish — codename Orbit 360 (`~/Desktop/Orbit360/`)

- **What it is:** a native **SwiftUI macOS app** (10 Swift files, ~2,000 lines, one git commit, 8 Jul 2026) with a watch folder and a job queue: per job it can crop an eye, encode with ffmpeg (`libx265`), **inject spherical / stereo / spatial-audio metadata with Google's `spatial-media`** tool, then verify the output has the metadata.
- **Build state:** a **Debug build only**, in Xcode's DerivedData (`…/Build/Products/Debug/Orbit360.app`). No Release build, no archive, no DMG, no README.
- **Dependencies on the user's machine:** Homebrew `ffmpeg` and `ffprobe`, `python3`, and a **checkout of Google's spatial-media repo** at `~/spatial-media` (or `~/Scripts/…`, `~/Developer/…`, or a path set in the app's Settings). That last one is the blocker: nobody outside this house has it.
- **Is it "a script that injects 360/VR180 metadata"?** No — it is an app that shells out to one. So it was **not** packaged into the 1.0.8 release folder: a wrapper around spatial-media with no ffmpeg would be a different product, and shipping ffmpeg raises its own licensing work.
- **Packaging plan:** (1) bundle `spatial-media` (Apache-2.0, bundle-able) inside the app and drop the checkout requirement; (2) decide on ffmpeg: bundle a static build (LGPL build, no GPL codecs; x265 is GPL, so the H.265 encode would move to VideoToolbox, which is the better Mac answer anyway) or keep "install ffmpeg with Homebrew" as a documented step for the alpha; (3) Release build + notarisation; (4) a README. Until (1) and (2) are done, Publish is not a free download, whatever the site says.

## What the site should say until then

Both appear as free downloads on the Velaria website comp. Neither should go live as a link. Suggested wording for the launch page: Stitch "coming with the alpha (Titan now; other Insta360 cameras later)", Publish "coming with the alpha". Site changes are not applied by this note; the edit list is in the roadmap doc.
