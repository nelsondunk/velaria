# Velaria 1.0 release checklist

Internal. Written 12 Nov 2026 (week-of-9-Nov session G). Not shipped: `check_release.sh` rejects a release folder that contains it.

The steps to freeze and ship 1.0, in order. Each has an owner, a due date and a done-when. "Claude on the Mac" means a Claude session on Bruce or NYC with Nelson present to approve; it cannot run unattended in the cloud. Dates assume Nelson is back Mon 16 Nov and the target on-sale date is **Fri 4 Dec 2026** (the date in the commercial package). If any step in sections A to C slips more than three working days, move the on-sale date instead of skipping the step.

Aligned with "Nelson's one-time actions" in the 2 Nov launch plan: store, Canon clip, UltraStudio order, Quest into the second Mac, screen recording, page approval and the one post are steps A1, A6, A7, C4, E1, E3 and F3.

## A. Decisions and one-time actions (Nelson)

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| A1 | Confirm the Core price, one-time model and 14-day refund; create the store under H&A | Nelson | Wed 18 Nov (decision), Fri 27 Nov (store) | `priceCore` in `site/index.html` shows the confirmed number, and the store's product URL is pasted into `buyUrl` |
| A2 | Decide the support email, the legal entity name and the mailing address | Nelson | Fri 20 Nov | `grep -rn '\[SUPPORT EMAIL\]\|\[LEGAL ENTITY NAME\]\|\[MAILING ADDRESS\]' legal docs/public site` prints nothing |
| A3 | Decide the public version number. Internal builds are already at 1.0.9, and the headset's build number must go up. Recommended: ship as **1.0.10**, shown as "Velaria 1.0" on the page | Nelson | Fri 20 Nov | One line in the Notion Velaria page; the same number is used in C1 |
| A4 | Decide whether the `Skylight_UpgradeMacros` script and the `ca.skylightproductions.velaria` identifiers stay as they are for 1.0. The page says the seller is Hammer & Anvil. Renaming touches the installer, the bridge, the headset app ID and every doc, and needs a full re-test. Recommended: keep for 1.0, say "made by Skylight for H&A" nowhere, revisit with signing | Nelson | Fri 20 Nov | Yes or no on the Notion Velaria page |
| A5 | Legal read of the EULA, privacy statement and notices; read Blackmagic's DeckLink SDK licence and the Android SDK terms for bundling `adb` | Nelson, with a lawyer | Fri 27 Nov | A written reply is saved; the "Open questions" section and every `[VERIFY]` and DRAFT marker are gone from `legal/` |
| A6 | Get one native Canon dual-fisheye clip (rent, borrow or a Canon rep) | Nelson | Fri 27 Nov | The clip is on the second Mac. If it is not there by this date, 1.0 ships with the fisheye wording as it is in `docs/public/howto-180-and-fisheye.md` ("a preview, not a calibrated conversion") |
| A7 | Order the UltraStudio test fixture: an UltraStudio Monitor 3G or the newer UltraStudio Express Monitor 3G. **Not** the older UltraStudio Mini Monitor: Desktop Video dropped it in 14.0 and it was Intel-only before that (checked 13 Nov; see `docs/decklink-coexistence-test-plan.md`) | Nelson | Fri 20 Nov | Order confirmation saved. It is for coexistence (1.0.1 or 1.1), not a 1.0 blocker |

## B. Merge and freeze (Nelson and Claude on the Mac)

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| B1 | Read session R's review of branch `claude/week-nov-9` and decide what merges to `main`, area by area | Nelson | Wed 18 Nov | Each area in R's report is marked merge, hold or drop |
| B2 | Merge the approved commits to `main` | Claude on the Mac, Nelson approves | Thu 19 Nov | `git log main` shows them; nothing unapproved is on `main` |
| B3 | Run everything that runs without hardware: `sh common/tests/run.sh`, `python3 mac/bridge/tests/test_bridge.py`, `shellcheck mac/install/*.sh mac/package.sh mac/check_release.sh`, `python3 docs/build_docs_site.py` | Claude on the Mac | Thu 19 Nov | All four exit 0 on the Mac (they do on Linux as of 12 Nov) |
| B4 | Build the driver and run its own tests on the Mac: `mac/driver/build.sh`, then `build/selftest`, `build/area_test`, `build/bench`, `tests/e2e.sh` | Claude on the Mac | Fri 20 Nov | Each prints its pass line; results pasted into `docs/install-soak.md` under a 1.0 heading |
| B5 | Rebuild the headset app with the new version and a higher build number: `quest/build.sh <version> <build>` | Claude on the Mac | Fri 20 Nov | `Velaria.apk` installs over the old one; the View tab shows the new build; with the bridge stopped, the strip reads "On the Mac, run sudo ./install.sh from the Velaria folder" |
| B6 | Macro regression on the second Mac with Resolve open (`macros/regression-rig`) | Claude on the Mac | Mon 23 Nov | Same pass count as the last run (21 of 27 after the rename) or better, with any change explained in `docs/macro-regression.md` |
| B7 | Set the version in the three places that carry it: `BRIDGE_VERSION` in `mac/bridge/vrlink_bridge.py`, the arguments to `quest/build.sh`, and the argument to `mac/package.sh` | Claude on the Mac | Mon 23 Nov | The headset's versions line shows the same number for the app, the Mac package and the bridge |
| B8 | Fill the placeholders: support email and entity in `legal/` and `docs/public/`, and `supportEmail`, `buyUrl` in `site/index.html` | Claude on the Mac | Mon 23 Nov | The grep in A2 prints nothing |
| B9 | Tag the freeze | Nelson | Mon 23 Nov | `git tag v<version>` exists on `main`; no commit after it except fixes found in section C |

## C. Build and prove the release (Claude on the Mac, Nelson for the headset)

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| C1 | Package: `STRICT=1 mac/package.sh <version>` | Claude on the Mac | Tue 24 Nov | It prints "check_release: ... is clean" and "Release: ...zip" with no WARNING line |
| C2 | Open the zip and read the file list | Nelson | Tue 24 Nov | It holds the installer, the three effects, the six customer documents, `docs/`, the three legal texts and `licenses/`; no `README.md`, no `tools/`, no 3D card or title files |
| C3 | Clean install on the second Mac from a **Safari-downloaded** copy of the zip (so macOS quarantines it). Copy the exact words of any macOS dialog | Nelson | Wed 25 Nov | Install completes; any dialog text matches `docs/public/setup-guide.md` and the page's requirements box word for word, or both are corrected |
| C4 | Plug the Quest into the second Mac and run `sudo ./install.sh --headset` there (never done on a Mac other than the build machine) | Nelson | Wed 25 Nov | "Installed Velaria View on the headset." and the app opens |
| C5 | Walk `docs/public/READ-ME-FIRST.md` and `setup-guide.md` start to finish on that Mac, doing only what they say | Nelson | Wed 25 Nov | First picture in the headset without consulting anything else; every step that needed outside knowledge is fixed in the doc |
| C6 | Cause each of the five failures on the read-me-first card on purpose and apply its fix | Nelson and Claude on the Mac | Thu 26 Nov | Each fix works as written. The Blackmagic one needs Desktop Video installed (no card needed): refusal, `--replace-blackmagic`, `--uninstall`, and Blackmagic's driver is back |
| C7 | Run `./send-logs.sh` with the headset plugged in and open the zip | Nelson | Thu 26 Nov | The zip's contents match the table in `docs/public/support.md` |
| C8 | Check the three how-tos and the 180 and fisheye page against Resolve and the headset: every menu path, control name and chip label | Nelson | Fri 27 Nov | Each page is marked checked; mismatches are fixed in `docs/public/` and the site is rebuilt |
| C9 | Uninstall on the second Mac and compare what is left with `docs/public/uninstall.md` | Nelson | Fri 27 Nov | Only the items in the "what it leaves" table remain |
| C10 | Prove 180 on real footage: Blackmagic's public immersive clips, and the Canon clip if it arrived (A6) | Claude on the Mac, Nelson in the headset | Mon 30 Nov | A dated note in `docs/test-footage.md` for each clip: layout, projection, preset, and whether straight lines stay straight |
| C11 | Ten-cycle install soak with the release zip (`mac/tools/soak.sh`) | Claude on the Mac | Mon 30 Nov | Ten of ten cycles pass; result appended to `docs/install-soak.md` |
| C12 | Rebuild the release if C3 to C11 changed anything, and repeat C1 and C2 | Claude on the Mac | Tue 1 Dec | The zip's checksum (`shasum -a 256`) is written on the Notion Velaria page |

## D. Docs site and page

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| D1 | Decide where the docs live on the H&A site | Nelson | Mon 23 Nov | The address is on the Notion Velaria page |
| D2 | Build for publishing: `SUPPORT_EMAIL=... SITE_URL=... DOCS_URL=... python3 docs/build_docs_site.py` | Claude on the Mac | Tue 1 Dec | It prints "wrote 11 pages" with no WARNING line, and `docs/site/sitemap.xml` exists |
| D3 | Upload `docs/site/` and set `#DOCS-URL` on the sales page | Nelson | Wed 2 Dec | Every page in the left rail loads on a phone and a laptop, and the page's docs link opens the start page |
| D4 | Supply the page pictures or approve a real headset capture for the hero | Nelson | Fri 27 Nov | The sales page renders with no missing pictures |

## E. Store and page live

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| E1 | Record 60 to 90 seconds of screen plus headset view (scrcpy), no voice | Nelson | Mon 30 Nov | The clip plays on the sales page |
| E2 | Upload the zip from C12 to the store; paste the listing copy and the receipt email | Nelson | Wed 2 Dec | The store shows the product as a draft with the right price |
| E3 | Approve the page copy, the listing and the receipt email | Nelson | Wed 2 Dec | Each is marked approved in the commercial package doc |
| E4 | Test purchase with a 100% discount code, on a different browser profile | Nelson | Thu 3 Dec | The receipt arrives, its download link gives a zip whose checksum matches C12, and the docs link works |
| E5 | Publish the page and the store listing | Nelson | Fri 4 Dec | A stranger's browser can reach Buy and complete checkout |

## F. After it is live

| # | Step | Owner | Due | Done when |
|---|---|---|---|---|
| F1 | Send a test email to the support address from outside | Nelson | Fri 4 Dec | It arrives where Nelson reads mail |
| F2 | Refund the test purchase path once with a real card | Nelson | Mon 7 Dec | The refund shows in the store |
| F3 | Post once to r/davinciresolve, r/VRvideo, r/360video and the Premiere thread (session J's drafts) | Nelson | Mon 7 Dec | Four links saved on the Notion Velaria page |
| F4 | Count page visits, sales, refunds and support emails | Nelson | Fri 8 Jan 2027 | The four numbers are on the Notion Velaria page beside the price test in the commercial package |

## What is UNTESTED as of 12 Nov 2026

Nothing below has run on a Mac or a headset since the changes on the branch. Sections B and C exist to close this list.

- `install.sh` changes (signature warning, per-file font quarantine, `--headset` message). Dry-run on Linux only.
- `package.sh` changes and `check_release.sh`. Dry-run on Linux only; `ditto`, the driver build and the real zip were stubbed.
- The headset string change in `quest/src/panels.cpp`. Compiled on Linux through the preview tool; the app was not rebuilt.
- Every macOS dialog quoted in the docs and on the page.
- `sudo ./install.sh --headset` on a Mac other than the build machine.
- The Blackmagic set-aside and restore path with Desktop Video really installed.
- Fisheye 180 on camera-original footage.
- The driver's own tests (`selftest`, `area_test`, `bench`, `e2e.sh`): macOS only.
