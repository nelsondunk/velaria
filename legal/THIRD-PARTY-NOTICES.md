# Velaria third-party notices

**DRAFT, 12 November 2026.** Built from an audit of the `nelsondunk/velaria` repository on that date. Items marked **[VERIFY]** were written from the repository and general knowledge, because the licence pages could not be opened during the audit. Check each one before release.

Velaria includes or was built with the following software and fonts from other authors. Each remains under its own licence. Full licence texts are in the `licenses` folder beside this file.

## In the Mac package

### Android Debug Bridge (adb), from Android SDK Platform-Tools

- Where: `platform-tools/adb`, used only to install the headset app and to carry the USB connection.
- Copyright: The Android Open Source Project.
- Licence: Apache License 2.0 (`licenses/Apache-2.0.txt`). Google's own `NOTICE.txt` for the binary ships beside it in `platform-tools/`, with `source.properties` giving the exact revision.
- **[VERIFY]** The adb source code is Apache 2.0. The prebuilt binary is downloaded from Google, and Google's download is offered under the Android SDK terms, which may limit redistribution of the download itself. See "Open questions" below.

### Montserrat (font files)

- Where: `fonts/Montserrat-Medium.ttf`, `fonts/Montserrat-ExtraBoldItalic.ttf`, installed to the user's Fonts folder.
- Copyright 2024 The Montserrat.Git Project Authors (https://github.com/JulietaUla/Montserrat.git).
- Licence: SIL Open Font License 1.1 (`licenses/OFL-1.1-Montserrat.txt`).

### Blackmagic DeckLink SDK (interface headers)

- Where: compiled into `DeckLinkAPI.framework` and `vrlink_ndi`. No Blackmagic source or binary is shipped.
- Copyright (c) 2024 Blackmagic Design.
- Licence: the permission notice in the SDK headers, which also refers to Blackmagic's End User License Agreement for the Software Development Kit (https://www.blackmagicdesign.com/EULA/DeckLinkSDK). The header notice does not have to be reproduced for object code.
- **[VERIFY]** See "Open questions" below. This is the most important one.

### Steinberg VST 3 SDK (Velaria Sound plugin, `VelariaSound.vst3`)

- Where: the Fairlight insert `VelariaSound.vst3` is built with Steinberg's VST 3 SDK (base, pluginterfaces, public.sdk; version 3.7.14). Nothing of Steinberg's ships as source.
- Copyright (c) Steinberg Media Technologies GmbH.
- Licence: the SDK is offered under either the GNU GPLv3 or the proprietary "VST 3 Plug-In SDK Licensing Agreement" (free of charge, signed with Steinberg through the developer portal, https://www.steinberg.net/developers/). Velaria ships closed, so the **proprietary agreement must be signed by the legal entity that sells Velaria before the plugin leaves the test Macs**; `package.sh` refuses to include the plugin until `VELARIA_VST3_LICENSED=1` says it has been. The agreement also sets the trademark line below.
- Trademark line (required by the agreement; also goes in the headset's About / the web page where the plugin is mentioned): "VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries."
- **[VERIFY]** Confirm the current agreement text and whether a logo is required in the documentation.

## In the Velaria View headset app (`Velaria.apk`)

### OpenXR loader

- Where: `lib/arm64-v8a/libopenxr_loader.so`, from the Khronos OpenXR loader for Android (AAR).
- Copyright The Khronos Group Inc. and contributors.
- Licence: Apache License 2.0 (`licenses/Apache-2.0.txt`).
- **[VERIFY]** The loader bundles small third-party parts (JsonCpp, under MIT or public domain). Copy the licence and notice files from the exact AAR used for the release build into `licenses/` and record the loader version here: version [FILL IN].

### Android NDK: native app glue and C++ runtime

- Where: compiled into `libvrlink.so`.
- `android_native_app_glue`: Copyright The Android Open Source Project. Apache License 2.0.
- LLVM libc++ (statically linked): Copyright the LLVM Project contributors. Apache License 2.0 with LLVM Exceptions.

### Red Hat Text (glyph bitmaps)

- Where: baked into the app's interface from `quest/fonts/RedHatText-*.ttf`.
- Copyright 2021 The Red Hat Project Authors (https://github.com/RedHatOfficial/RedHatFont).
- Licence: SIL Open Font License 1.1 (`licenses/OFL-1.1-RedHatText.txt`).

### DejaVu Sans Mono Bold (glyph bitmaps)

- Where: baked into the app (`quest/src/font.h`).
- Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of Bitstream, Inc. DejaVu changes are in the public domain.
- Licence: Bitstream Vera Fonts licence (`licenses/Bitstream-Vera-DejaVu.txt`).

### Saira

- Where: `quest/fonts/Saira-*.ttf` in the source tree, and the Velaria web page. **[VERIFY]** whether any Saira glyphs remain in the 1.0 headset app; the interface moved to Red Hat Text in build 40. Listed here to be safe.
- Copyright 2020 The Saira Project Authors (https://github.com/Omnibus-Type/Saira).
- Licence: SIL Open Font License 1.1 (`licenses/OFL-1.1-Saira.txt`).

## Used at run time but not included

- **NDI** (optional, only with `install.sh --ndi`): Velaria loads the NDI library that the user has installed with NDI Tools. Nothing from the NDI SDK is shipped. NDI is a registered trademark of Vizrt NDI AB. **[VERIFY]** NDI's brand rules ask products that use NDI to carry this trademark line and a link to ndi.video; confirm the current wording if the NDI path is ever mentioned publicly.
- **DaVinci Resolve scripting API**: the bridge talks to the copy of DaVinci Resolve Studio on the user's Mac, using the Python that comes with Apple's Command Line Tools. Neither is shipped.

## On the web page only (not in the software)

- **GSAP 3.12.5 and ScrollTrigger**, loaded from cdnjs. Copyright GreenSock (now part of Webflow). **[VERIFY]** licence terms for the version used; see below.
- **Saira and Saira Extra Condensed**, loaded from Google Fonts. SIL Open Font License 1.1.

## Open questions for Nelson and counsel

1. **Blackmagic DeckLink SDK terms.** Velaria is built against Blackmagic's SDK headers and installs a framework with the same name and location as Blackmagic's own driver. Whether the SDK agreement allows a product that stands in for Blackmagic hardware, and allows setting Blackmagic's driver aside, has not been checked. Read the SDK agreement, and have counsel read it, before the first sale.
2. **Bundling Google's adb binary.** If the Android SDK terms do not allow redistribution, the fallback is simple: stop shipping `platform-tools/` and have the installer tell the buyer to install it (`brew install android-platform-tools`, or Google's own download). The installer already prefers a system adb.
3. **GSAP on a commercial page.** GreenSock's older "no charge" licence had conditions for commercial use; in 2025 GSAP was announced as free for all uses. The page pins 3.12.5. Either confirm the terms for 3.12.5 or move to a current release.
4. **Icons.** The headset's line icons were drawn on the Velaria design canvas (`quest/tools/mkicons.py`). No icon library was found in the repository, though a few shapes (for example the eye) are close to common open-source line icons. Low risk; confirm that none were pasted from a library that needs a notice.
5. **Steinberg VST 3 agreement.** Sign it (Nelson, as the entity that sells Velaria) before Velaria Sound ships; until then the plugin stays off the package. Nelson, 20 Nov: "can do! Put it on the list."
6. **Not in the package, so no notice needed:** the internal 3D title and card templates and their model kits are excluded by `package.sh` and must stay excluded.


## stb_image (headset app)

`quest/third_party/stb_image.h` (Sean Barrett and contributors, v2.32) decodes the dome theatre background JPEGs in Velaria View. Public domain (Unlicense) or MIT, at the user's choice, per the file's own notice. Unmodified.
