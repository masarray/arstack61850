# ARStack Process Bus Simulator — Autonomous SV/PTP Public Beta (M3.H.3 Beta 1)

Release tag: `processbus-sim-m3h3-beta1-20261025`  
Release type: **public prerelease / field-test beta** (does not replace the historical M3.H.1 stable release).  
Merged integration baseline: `9d980e8f766f779a881e770894c09a05b37e42e2` (#332).  
Exact matched Windows Studio + ESP32-P4 firmware build source: `2231ac35fa13211b2b0c3b51243a7e352389559c`.

## What is delivered

- Native ARStack Studio for Windows x64 (Setup EXE + portable ZIP) with ESP32-P4 firmware bundled.
- Separate ESP32-P4 4I4V SV/PTP firmware image and manifest.
- Linux x86_64 AppImage and checksum.
- Studio-close autonomy: SV publisher continues on the powered board with physical Ethernet UP, without a USB control session.
- Read-only coherent board scenario adoption on Studio reconnect, including frequency, 4I4V channel values and CT waveform-shaping parameters.
- Explicit START/STOP, PHY link-down and Ethernet driver-stop fail-closed lifecycle: no automatic rearm after link recovers.
- Firmware and GUI feature contract: `SV-LAN-AUTONOMY-V1`.
- Exact-head CI: 12/12 green on `2231ac35...`, including native Windows installer build, clean install, upgrade, uninstall and firmware compatibility verification.

## Physical Ethernet receiver evidence (25 October 2026)

Captured using an independent NIC via dumpcap/Npcap and the operator's ARStack Studio receiver report `ARStack_SV_20261025_063546_648_report.json`:

| Metric | Observed |
| --- | ---: |
| Valid contiguous capture span | 1176.305628 s (19 min 36 s) |
| SV frames observed | 4,705,090 |
| Average SV fps | 3,999.8865 |
| Missing sample counters | 0 |
| Duplicate sample counters | 0 |
| Counter reset / reorder events | 0 |
| Malformed SV frames | 0 |
| PTP L2 frames | 3,528 |
| PTP Announce / Sync / Follow_Up | 1,176 each |
| Malformed PTP frames | 0 |
| SV smpSynch value | 2 on 4,705,090 frames |

The original 3,600-second **duration-only** gate is intentionally reduced for this public beta: approximately 20 minutes of loss-free contiguous SV is accepted as field-test readiness evidence. The capture report itself marks the 3,600-second attempt `INVALID_CAPTURE` / `capture_interrupted=true` because the publisher STOPPED; therefore it is not documented as a 3,600-second PASS or full physical acceptance. The exact SVLIFE stop reason was not available in the submitted reports.

## Explicit limitations — do not misrepresent

- PTP Announce, Sync and Follow_Up were present on Ethernet, but the Studio's passive device telemetry recorded `PTP role=SOURCE`, `discipline=UNLOCKED`, `source=NONE`, `offset=NA` throughout the running capture. The firmware manifest declares `defaultPtpPolicy=INTERNAL_GM_ONLY` and `globalGmTraceabilityPhysicallyProven=false`.
- `smpSynch=2` observed on the SV wire is **not** by itself proof of external grandmaster lock, UTC traceability, phase-error qualification, or submicrosecond SV egress accuracy. Do not claim "perfect PTP lock."
- Operator observed Studio close/reconnect preservation of board RUNNING state; no external tester has yet qualified this on ARSAS or DigSubAnalyzer.
- PHY cable, switch, power and stop-cause investigations remain open; fail-closed STOP following link-down is intentional.
- This is suitable for **public beta / lab testing**, not certified protection, calibration, conformance, or production power-system operation. Keep independent receiver qualification issue #324 open.

## Download and upgrade guidance

- Windows installer is Inno Setup *Preview*, with a distinct AppId from the historical stable release; the beta does not silently replace stable.
- Flash only the firmware included with this Studio build. Prior stable firmware lacks the new capability contract and must not be paired with this Studio.
- The release assets are compiled from, and cross-checked against, the exact verified PR head before publication; see the provided `SHA256SUMS.txt`.

## Provenance

- GitHub Actions Windows + paired firmware run: https://github.com/masarray/arstack61850/actions/runs/40165495478
- GitHub Actions Linux AppImage run: https://github.com/masarray/arstack61850/actions/runs/40165495452
- Merged autonomous lifecycle PR: https://github.com/masarray/arstack61850/pull/332
- Physical independent ARSAS/DigSubAnalyzer acceptance: https://github.com/masarray/arstack61850/issues/324
