# Process Bus Simulator Dual Firmware — Stable Release Promotion

**Release tag:** `processbus-sim-dual-stable-20261105`  
**Source accepted by operator:** Public RC1 `processbus-sim-dual-rc1-20261105`  
**Exact RC1 firmware/Studio source:** `ab646b58da601569c956f2d19ff45100129f19a0`  
**RC1 integrated main SHA:** `525902548504aa997f868c43f51208fb13beecaf`

This file is a one-time, post-merge trigger for the existing-artifact stable promotion
in `.github/workflows/processbus-dual-stable-publish.yml`.

## Product release contract

- ARStack61850 is an IEC 61850 Process Bus **Merging Unit + GPS-style PTP Grandmaster simulator** for isolated laboratory current/voltage SV injection and SCL-driven engineering. Emulated PTP and SV must work as though genuine process-bus devices were connected.
- **No C++ Studio, Qt, ESP-IDF, SV encoder, PTP runtime, or firmware rebuild.** Promote the exact operator-tested public RC1 binaries, matching the two distinct ESP32-P4 pre-v3 and v3.x images and catalog by pinned SHA256.
- The one necessary packaging change: compile the already-gated Inno Setup source **without ARSTACK_PREVIEW**, using the stable AppId and installer identity. Run fresh Windows stable install, exact Studio binary/firmware contract, and clean uninstall checks.
- Package `ARStack-Studio-win-x64-setup.zip` containing only `ARStack-Studio-win-x64-setup.exe` as the **preferred browser download**. Provide portable ZIP, Setup EXE, Linux AppImage, firmware images, revision catalog, manifest, release notes, SHA256SUMS and source provenance separately.
- Create a **new public non-prerelease stable release** and make it latest. Do not edit the accepted RC1 tag or historic M3.H.3 stable tag/assets. Publication must fail closed on provenance/hash mismatch or CI/package test failure.
- ZIP helps with browser download handling but does **not** bypass Microsoft SmartScreen or security checks. Keep unsigned EXE provenance and installation instructions clear.
- Stable release reflects operator-tested RC1 behavior; avoid making unsupported claims that 3x4000fps physical HIL on every PCB or certified UTC/GPS hardware were proven.

**Gate:** Merge this release PR only after static validation and no outstanding review issues; the merge starts the single Windows packaging/publishing job without reopening expensive firmware CI.
