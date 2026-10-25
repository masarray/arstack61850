# ARStack Process Bus Simulator M3.H.3 — Official Stable Lab Release

**Release:** `processbus-sim-m3h3-20261025` · **25 October 2026** · **Free/open-source official stable lab release**

Official release artifacts: [GitHub latest stable](https://github.com/masarray/arstack61850/releases/latest). Builds retain the **exact binaries that passed CI** for commit `2231ac35fa13211b2b0c3b51243a7e352389559c` (merged by PR #332 as `9d980e8f766f779a881e770894c09a05b37e42e2`). No source modification or binary rebuild was introduced during stable packaging. Windows Setup EXE, portable ZIP, firmware, Linux AppImage and SHA256 checksums are all provided together.

## Delivered

- Windows x64 ARStack Studio installer and portable package, with matching ESP32-P4 4I+4V SV/PTP firmware.
- Linux x86_64 AppImage, tested in exact-source CI.
- **Autonomous publisher**: closing Studio or disconnecting the USB control session does not itself stop board SV transmission while ESP32-P4 remains powered and Ethernet remains UP.
- **Reconnect adoption**: reopening Studio reads and follows the board's actual STARTED/STOPPED state and verified channel/frequency/CT parameters; no unsolicited START/STOP.
- Fail-closed physical Ethernet link-down/driver-stop behavior; reconnecting the LAN does not automatically rearm SV.
- 4I+4V · 4000 SV fps (nominal) · 1 ASDU · 64-byte payload · PTP SOURCE internal laboratory grandmaster emulation · `SV-LAN-AUTONOMY-V1` capability contract.
- 12/12 exact-head CI passing including matched firmware checks, Windows installer installation/upgrade/uninstall, Linux AppImage.

## Real Ethernet receiver validation (25 October 2026)

| Independent capture measure | Observed |
| --- | ---: |
| SV wire capture span | **1176.305628 s (19 min 36 s)** |
| Sampled Values received | **4,705,090 frames** |
| Observed SV frame rate | **3,999.8865 fps** |
| Missing sample count | **0** |
| Duplicate sample count | **0** |
| Counter reset / reorder | **0** |
| Malformed SV frames | **0** |
| PTP L2 frames | **3,528** |
| PTP Announce / Sync / Follow_Up | **1,176 each** |
| SV smpSynch=2 | **4,705,090 frames (100%)** |

Independent PCAP acquired by dumpcap/Npcap; input report `ARStack_SV_20261025_063546_648_report.json`. For this **free stable lab release**, the previous 3600-second *duration-only* mandatory release gate is reduced based on this strong 19-minute observation. The former 3600-second attempt finished early after publisher STOP; the report marks that attempt `INVALID_CAPTURE` and `capture_interrupted=true`. This release does **not** claim a 3600-second pass; exact `SVLIFE` stop cause not independently established.

## PTP and scope — precise interpretation

- PTP `Announce`, `Sync`, `Follow_Up` traffic was observed, and SV wire reported `smpSynch=2`.
- Studio telemetry during the capture recorded `ptp_role=SOURCE`, `ptp_discipline=UNLOCKED`, `ptp_source=NONE`, and `ptp_offset_ns=NA`. In `INTERNAL_GM_ONLY` mode, **external PTP grandmaster lock or UTC traceability was not proven**. Do not use the on-wire `smpSynch=2` field alone as proof of an external GM lock or phase-error guarantee.
- External ARSAS/DigSubAnalyzer physical interoperability, network-cable fault isolation and formal clock-offset/phase characterization remain follow-up engineering items, not claimed as completed.
- This product is for isolated **laboratory education, experiments and test benches**, not protection operation, metrology certification or connection to a live process bus.

## Download and install

- **Windows x64 installer:** `ARStack-Studio-M3H3-win-x64-setup.exe`.
- **Windows x64 portable:** `ARStack-Studio-M3H3-win-x64-portable.zip`.
- **ESP32-P4 firmware:** `ARStack-ESP32P4-M3H3-firmware.bin` and `firmware-manifest.json`.
- **Linux x86_64:** `ARStack-Studio-linux-x86_64.AppImage` and checksum.
- **Integrity:** `SHA256SUMS.txt` covers all six downloadable data/checksum assets.

The Windows executable was originally compiled by the verified **Native Preview** CI pipeline and retains its internal preview installer identity; it is officially distributed here as the accepted lab build. This **does not silently overwrite an older stable installation**. Install/flash the firmware **from this same release**, not a historic M3.H or v0.1.1 image.

## Engineering provenance

- Windows and ESP32-P4 exact-head CI: https://github.com/masarray/arstack61850/actions/runs/40165495478
- Linux exact-head CI: https://github.com/masarray/arstack61850/actions/runs/40165495452
- Mainline PR #332: https://github.com/masarray/arstack61850/pull/332
- Ongoing independent receiver acceptance (optional for lab-release designation): https://github.com/masarray/arstack61850/issues/324
