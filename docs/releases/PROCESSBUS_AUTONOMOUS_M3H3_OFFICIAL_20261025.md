# ARStack M3.H.3 — Official Stable Process Bus Simulator for Engineers

**Release:** `processbus-sim-m3h3-20261025` · **25 October 2026** · **Free/open-source official stable lab release**

**Generate real IEC 61850 Sampled Values and PTP traffic with one ESP32-P4.** Use ARStack Studio for practical isolated-bench relay/BCU subscriber experiments, SV configuration troubleshooting, digital substation demonstrations and waveform scenarios. This is a traffic-generation simulator: configure the destination MAC, APPID, VLAN and svID to match the receiving engineering setup.

Official release artifacts: [GitHub latest stable](https://github.com/masarray/arstack61850/releases/latest). Builds retain the **exact binaries that passed CI** for commit `2231ac35fa13211b2b0c3b51243a7e352389559c` (merged by PR #332 as `9d980e8f766f779a881e770894c09a05b37e42e2`). No source modification or binary rebuild was introduced during stable packaging. Windows Setup EXE, portable ZIP, firmware, Linux AppImage and SHA256 checksums are all provided together.

## What you can do

- Windows x64 ARStack Studio installer and portable package, with matching ESP32-P4 4I+4V SV/PTP firmware.
- Linux x86_64 AppImage, tested in exact-source CI.
- **Autonomous publisher**: closing Studio or disconnecting the USB control session does not itself stop board SV transmission while ESP32-P4 remains powered and Ethernet remains UP.
- **Reconnect adoption**: reopening Studio reads and follows the board's actual STARTED/STOPPED state and verified channel/frequency/CT parameters; no unsolicited START/STOP.
- Fail-closed physical Ethernet link-down/driver-stop behavior; reconnecting the LAN does not automatically rearm SV.
- **SV + PTP on real Ethernet:** 4I+4V · 4000 SV fps (nominal) · 1 ASDU · 64-byte payload · PTP SOURCE laboratory clock emulation · `SV-LAN-AUTONOMY-V1` capability contract.
- 12/12 exact-head CI passing including matched firmware checks, Windows installer installation/upgrade/uninstall, Linux AppImage.

## M3.H.3 SV/PTP traffic captured on real Ethernet

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

The capture was collected on a separate Ethernet interface using dumpcap/Npcap (`ARStack_SV_20261025_063546_648_report.json`). It records **4,705,090 actual SV frames**, stable counters, and simultaneously observed PTP Layer-2 traffic. The board ran with the **internal lab PTP SOURCE** setting (`INTERNAL_GM_ONLY`); this is a Process Bus simulator, not a GPS/UTC metrology reference.

### Next engineering features

- **Multi-SV:** multiple separately configured stream identities and sample counters published concurrently from a single ESP32-P4.
- **SCL-driven virtual Merging Unit:** load SCL/SCD/CID/IID, select supported `SampledValueControl`/`DataSet` definitions and publish their SV identities over the lab LAN.
- **Practical workflow:** select a stream, configure waveform and stream addressing, START and observe subscriber behavior. No costly external PTP equipment, relays or vendor analyzers are required to develop and use the simulator; community relay/BCU feedback is welcome.

These are **next-phase goals**, not current M3.H.3 features. [Multi-SV + SCL architecture and milestones](https://github.com/masarray/arstack61850/blob/main/docs/PROCESS_BUS_MULTI_SV_SCL_NEXT_PHASE.md).

**Network scope:** dedicated/isolated engineering LAN or test bench only, not an operational substation process bus.

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
