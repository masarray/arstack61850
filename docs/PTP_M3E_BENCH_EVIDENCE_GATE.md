# M3E.1 — Evidence-driven ESP32-P4 SOURCE/RECEIVER bench handoff

**Status: host-side validation implemented; no new physical board run was performed.** This gate exists so a real ESP32-P4 bench run cannot be confused with a successful software build, host timestamp or arbitrary `PTP LOCKED` printout. It builds on [M3D.2](PTP_START_STOP_CANCELLATION_M3D2.md), [PTP-P2 acceptance](PTP_P2_BENCH_ACCEPTANCE.md) and the existing [M2B streaming PTP+SV wire audit](PTP_SV_WIRE_AUDIT_M2B.md).

## One native Studio / ESP32-P4 workflow

Use the native **ARStack Studio** application for normal profile deploy and operator START/STOP. The board's serial command transport is a fallback for diagnostics (`IDENTIFY`, `PTP SHOW`, `PTP START`, `PTP STOP`, `SHOW`, `START`, `STOP`), not a replacement desktop web app.

1. Retain the stable board image before experimenting. Confirm the ESP32-P4 target **and silicon revision** against `sdkconfig` in the chosen bundle. Current P0 hardware is pre-v3; do not force v3 defaults. Keep the Ethernet lab **isolated from any live process bus**. Do not use these experimental images on an in-service relay network.
2. Download the GitHub Actions artifact from [M3D.2 run #38655888456](https://github.com/masarray/arstack61850/actions/runs/38655888456): `esp32p4-m3d-ptp-bench-7d2d5146b1892124c1744ad26ab31b3489e8387e` (retention expires October 21, 2026 UTC). It contains `source/` and `receiver/`, each with its **own** `manifest.json`, `flasher_args.json`, `sdkconfig`, and three images. Never mix SOURCE and RECEIVER application binaries. Verify each image hash and use offsets from the **actual** `flasher_args.json` rather than guessing. This tool does **not** perform flashing.
3. Run the preflight before any flash (also supported on Windows with Python 3):

   ```sh
   python scripts/ptp_m3e_bench_gate.py --bundle arstack61850-esp32p4-m3d2-bench-7d2d514.zip --role receiver --output preflight-receiver.json
   ```

   `firmware.status=VERIFIED` proves only consistency with the packaged manifests/checksums; it does **not** prove a production signature, a physically installed image, measured PTP accuracy or compatibility with the target board. The current artifact's app offset is `0x10000`, bootloader `0x2000`, partition table `0x8000`; preflight reads and checks all three independently from each role's manifest.

4. After installing **one** role on the appropriate test board, collect *raw* serial/Studio logs including `IDENTIFY` (must contain the embedded 16-character build ID) and repeated `PTP SHOW` before START, during acquisition/LOCKED, after deliberate GM disconnect/change and during STOP/recovery. Preserve the complete transcript and record board silicon revision, external GM model/profile and test topology.
5. In parallel, record a **canonical** Ethernet classic-PCAP (VLAN preserved via mirror/TAP if necessary). A diagnostic broadcast mirror is **not** evidence of the canonical SV stream. Capture at least two `smpCnt=0` events plus one observed counter wrap per single uninterrupted segment. Capture loss/reacquisition and GM switch as **separate segments**: the preexisting wire auditor intentionally fails a single stream that mixes conflicting PTP sources.
6. Build the **existing** streaming host parser without adding another wire codec:

   ```sh
   cmake -S . -B build-m3e -DARIEC61850_BUILD_TESTS=OFF -DARIEC61850_BUILD_TOOLS=ON
   cmake --build build-m3e --target ariec61850_ptp_sv_wire_audit --parallel 2
   ./build-m3e/ariec61850_ptp_sv_wire_audit --self-test
   ```

7. Run the combined host evidence gate. Replace SV identity, APPID, `smpCnt` modulus and PTP domain with **the exact active SCL/Studio profile**. The example below is a placeholder and **not** a default physical profile:

   ```sh
   python scripts/ptp_m3e_bench_gate.py \
     --bundle arstack61850-esp32p4-m3d2-bench-7d2d514.zip \
     --role receiver \
     --serial-log receiver-serial.log \
     --pcap receiver-canonical.pcap \
     --wire-audit ./build-m3e/ariec61850_ptp_sv_wire_audit \
     --sv-id 'YOUR_ACTUAL_SVID' --app-id 0x4000 \
     --counter-modulus 4000 --ptp-domain 0 \
     --output receiver-bench-report.json
   ```

   The Python gate **executes** `ariec61850_ptp_sv_wire_audit --json` over the actual PCAP with bounded streaming C++ parsing. It never reparses PTP or BER/SV wire frames. `--serial-log` checks the board's `ARSTACK identity ... build=...` against both firmware manifests and scans existing `PTP2 role=... discipline=... source=...` status messages; `LOCKED` is explicitly classified as **reported by firmware**, not independently measured PTP lock.

## What the machine-readable result means

- `firmware.status=VERIFIED`: matching two-role commit, build ID in the image, role/target/Kconfig constraints, non-overlapping ESP-IDF flash offsets, SHA-256 for all images, no staged mirror/probe, distinct SOURCE/RECEIVER applications. It validates **provenance consistency**, not a trusted signing chain.
- `serial.status=OBSERVED`: matching board-reported build ID, role, PTP2 status history and selected source in RECEIVER LOCKED lines. A missing/wrong build ID, wrong role or LOCKED with no GM rejects the run. A status report alone cannot establish GM accuracy.
- `wire.status=CONSISTENT`: the existing native PCAP auditor reports the selected canonical SV counter sequence/identity consistent and the selected-domain PTP Announce/Sync/Follow_Up complete; all selected wire `smpSynch` remain **0**, and the PCAP contains at least two sample-zero events and one wrap. Any `smpSynch=1/2`, counter gap or insufficient capture fails this M3 test. PCAP timestamps are **host receive timestamps, not EMAC egress timestamps**.
- `softwareEvidenceConsistent=true`: both the serial and PCAP consistency checks succeeded **within their evidence limits**. It is not a conformance, hardware phase or relay acceptance PASS.
- `independentHardwarePhaseQualified=null`, `independentPTPGrandmasterAccuracyQualified=null` and `relayAcceptanceQualified=null` remain unknown in **every** report. `autoSmpSynchPromotionPermitted=false`, `publicReleasePermitted=false` are hard fail-closed outputs. There is intentionally no flag that claims these physical gates are green.

The gate exits `0` for clean artifact-only preflight or complete consistent serial+PCAP *software* evidence. An incomplete/contradictory evidence set exits `2`; no absent input is silently treated as a real device PASS. The `sha256` fields bind input files so later test analysis can verify that logs/PCAP did not change.

## Physical acceptance still required

M3E is a **repeatable collection boundary**, not a completed board test. Before considering qualified PTP/SV synchronization, independently measure MAC SV egress timestamps vs external GM/PPS, sustained drift/jitter, epoch second continuity across loss/recovery, and relay/IED acceptance per P2 criteria. Record observed physical deviations and do not upgrade `smpSynch AUTO` or merge to a public stable release based on host capture or the software result.

## M3E.2 — Ordered PTP lifecycle evidence (fail-closed host gate)

An M3E.1 weakness has been closed: a clean PCAP plus a RECEIVER serial log
showing only `UNLOCKED`/`ACQUIRING` must **not** return
`softwareEvidenceConsistent=true`. M3E.2 adds an **ordered** operational gate
that is required alongside serial identity and wire consistency. It consumes
existing `IDENTIFY`, `PTP2` and `PTP status` lines; it adds no firmware task,
no ISR instrumentation, no wire decoder and no claim of physical PTP lock.

The `--scenario` option defaults to `auto` (`receiver-acquisition` for
RECEIVER or `source-ready` for SOURCE). For deliberate negative testing choose:

| Scenario | Required **firmware-reported** sequence |
| --- | --- |
| `receiver-acquisition` | `UNLOCKED` or `ACQUIRING` → measured `LOCKED` |
| `receiver-recovery` | measured `LOCKED` → `HOLDOVER`/`UNLOCKED`/`ACQUIRING`/`FAULT` → measured `LOCKED` |
| `receiver-gm-handoff` | measured `LOCKED` with GM A → non-LOCKED → measured `LOCKED` with a **different** GM identity/port |
| `source-ready` | SOURCE reports `PTP status=RUNNING` with nonzero sent Announce, Sync, Follow_Up |

For RECEIVER a usable `LOCKED` line must include selected, nonzero GM
identity/port and valid numeric offset/path/jitter plus positive Sync,
Follow_Up, Pdelay and accepted discipline counters. Truncated LOCKED lines,
zero-evidence counters, direct LOCKED-to-LOCKED GM changes without observed
demotion, mixed boot IDs or missing 16-hex-digit boot identity are incomplete.
A new source does **not** inherit the previous locked lease. The gate performs
one linear pass over log observations, with a 16 MiB serial-input ceiling.

Example: run an acquisition, then separately test loss/recovery on the same
bench (preserve unedited serial and capture artifacts):

```sh
python scripts/ptp_m3e_bench_gate.py \
  --bundle arstack61850-esp32p4-m3d2-bench-7d2d514.zip \
  --role receiver --scenario receiver-recovery \
  --serial-log receiver-loss-recovery.log \
  --pcap receiver-recovery-segment.pcap \
  --wire-audit ./build-m3e/ariec61850_ptp_sv_wire_audit \
  --sv-id 'ACTUAL_SVID' --app-id 0x4000 \
  --counter-modulus 4000 --ptp-domain 0 \
  --output m3e2-recovery-report.json
```

Use a **single uninterrupted stable-GM PCAP segment** as the wire input for
each audit. Retain a separate PCAP for the handoff itself; the existing PTP
wire auditor intentionally detects mixed GM identity and must not be bypassed
by relabeling sources or concatenating captures. A positive lifecycle result
is `operationalLifecycle.status=OBSERVED`, which means only that the board
reported the required ordered evidence. It cannot prove every transition
occurred or was measured between serial polls. `softwareEvidenceConsistent`
now requires **all three** gates: serial identity, ordered operational status,
and canonical PCAP continuity. All hardware qualification fields remain
`null`, and AUTO `smpSynch` promotion/public release remain `false`.

All M3E.2 changes are Python host-tool regression/doc additions. The proven
M3D.2 SOURCE/RECEIVER firmware binaries and native ARStack Studio are not
rebuilt or modified by this checkpoint.