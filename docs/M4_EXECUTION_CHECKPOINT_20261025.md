# ARStack M4 — Implementation Checkpoint / Execution Handoff

**Date:** 25 October 2026  
**Priority:** Exactly three independently mapped IEC 61850 4I+4V SV streams from the owner's SCD, shared PTP sample clock, one native ARStack Studio GUI with three MU pages.  
**Architecture authority:** [ARD M4](ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md) · [Issue #333](https://github.com/masarray/arstack61850/issues/333)

## DONE — merged into main

### P0A: SCL-to-three-stream core compiler

[PR #334](https://github.com/masarray/arstack61850/pull/334) · merge commit `e56560bdf32bbe5ec138fcac900eb21efe77b91b`.

- New `M4ThreeSvRosterCompiler` is built into portable `ARIEC61850::core`.
- Compiles exactly **three** `SclSampledValuesStream` entries from **one** existing `SclDocument`. Reuses the existing SCL type resolver, `SvPublisherProfileCompiler`, single-stream `CompiledSvDeviceProfile` and existing V1 binary codec.
- Strict M4.0 shape: explicit 9-2/9-2LE family selection, explicitly confirmed 4000-sample counter wrap, 4000 Hz, `nofASDU=1`, 16 ordered 4I+4V leaves, 64-byte payload.
- Each stream keeps its own IED/control-block identity, multicast MAC, APPID, VLAN, svID and compiled device profile. **Identical APPID with distinct MAC/svID is accepted** to match the owner's uploaded SCD; identical complete wire identity or another invalid profile invalidates the whole roster.
- New SCD-equivalent regression fixture `tests/fixtures/scl/m4-three-9-2le-reference.scd` has MU1/2/3 and explicit `SmvOpts`, independent 3-stream CTest, and binary V1 round-trips. The original uploaded SCD's SHA and mapping remain pinned in `tests/fixtures/scl/m4_3sv_uploaded_scd_expected.json`.
- Nine relevant PR workflows PASS; C++ CI GCC/Clang/MSVC PASS: https://github.com/masarray/arstack61850/actions/runs/40184292427.

### P0B: Multi-SV V2 binary envelope, host-side

[PR #335](https://github.com/masarray/arstack61850/pull/335) · merge commit `bcc497a914f0bec8702551f9caa0e18cbd2962de`.

- New `M4ThreeSvBinaryCodec`: 28-byte explicit `ARM3` V2 envelope and **three length-prefixed existing ARSV V1 records**.
- Caller-owned bounded `std::span` for encode/decode, network-byte-order fields, aggregate CRC32, each nested V1 record's CRC, strict total size/record lengths, schema and reserved-bits checks.
- Decode into temporary 3-record candidate; only replace destination if all three records and their mutual identities are valid. No XML/heap allocation in the decoder.
- Regression covers reproducible binary round-trip, small buffers, corrupted payload, invalid version/length, full-identity collision and unsupported stream rate. GCC/Clang/MSVC PASS on reviewed content: https://github.com/masarray/arstack61850/actions/runs/40184749369.
- Retained both pre-existing M3.H.3 released firmware and the V1 single-stream ABI, with no firmware behavior changes.

## NOT DONE — do not claim otherwise

- **P0C ESP32-P4 firmware** does not yet accept the V2 binary envelope, publish three different SV packets per 250-us sample slot or use a committed `SvStreamRuntime[3]` array.
- **P0D** three independently controllable signal banks / phase engines / start-stop and per-stream telemetry are not installed on the device; reconnect adoption currently belongs to the M3.H.3 single-stream runtime.
- **P0E** Studio still edits one active 4I+4V configuration; there is no MU1/MU2/MU3 synchronized Table/Waveform/Phasor pagination or three-stream deploy UX.
- **P0F** a matched M4 Studio+firmware installer/build and captured simultaneous 12,000 canonical SV fps are not yet delivered. Do not confuse a host roster/codec test with real triple-stream Ethernet output.

## NEXT — exact implementation order

### 1. P0C1: Firmware V2 device staging and readback

Extend `embedded/esp32p4_smv_injector/main/profile_control.cpp` with new `MULTISV` commands **without changing** `PROFILE BINBEGIN/BINCHUNK/BINCOMMIT` V1. Decode the new `ARM3` envelope using `M4ThreeSvBinaryCodec::decode` into one bounded inactive triple-profile staging area. Use staged transaction ID/offset, generation/hash, explicit capability `SV-MULTI3-V1` and readback. Initial M4.0 commit only while all SV streams STOPPED. Three-record validation failure must preserve the exact old active roster. Never block the 4-kHz publisher for serial parsing.

### 2. P0C2: One timebase / three publisher contexts

Refactor `embedded/esp32p4_smv_injector/main/app_main.cpp` single `active_profile` and fixed canonical packet into **`std::array<SvStreamRuntime,3>`** with stream-specific packet template, channel bank, fixed-point sine phase, smpCnt diagnostics, fail counters and identity. **Keep ONE GPTimer ISR and ONE realtime SV publisher task**, use a common 4000 Hz timer sample slot referenced to the existing EMAC/PTP clock. Each qualified tick emits at most three ordered canonical frames; all three carry the same sample epoch `smpCnt` and common `smpSynch` decision, with different per-stream data. Preserve the existing source/receiver clock lease and fail-closed degradation, not artificial UI flags. Allow expected deterministic sequential egress skew; measure it.

A simple fixed order MU1→MU2→MU3 is preferred initially. **Do not spawn three SV FreeRTOS tasks, three timers or three EMAC owners.** Disable extra per-stream diagnostic mirrors initially. Benchmark actual 3-frame TX burst cost and MCU internal RAM headroom: 12,000 canonical fps + low-rate PTP on the isolated network. If deadlines are missed, count and report, do not replay late samples.

### 3. P0D: Isolation and operator control

Use **one existing `SvLiveSignalBank` per slot** and `SvFixedPointSineEngine` per slot. The generated sine lookup table remains shared read-only. Add atomic per-stream `SETPOINTS` bank commit and stream-specific `SVSTREAM ID SHOW/START/STOP`; no implicit sibling changes, no partial setpoint transaction. Stream starts join the next valid global PTP epoch/sample tick instead of resetting the other streams' counters. Closing Studio/USB must preserve all eligible running streams; re-open adopts each stream's actual board state. Hardware link loss still fail-closes.

### 4. P0E: Single Qt GUI, three dedicated pages

Extend `SclProfileModel` with selected *roster* and immutable per-IED stream key; introduce a 3-row `StreamScenarioModel` that owns 3 *independent* 4I+4V scenario drafts. Keep existing `Main.qml`, `SignalMatrix.qml` and `SignalPreview.qml`: tabs MU1/MU2/MU3 page the common Table/Waveform/Phasor region; global PTP and device connection bars do not repeat. Only visible preview is repainted at UI-rate (~30fps); USB stays on existing `DeviceIoWorker`. Switching MU never submits setpoints to the wrong board stream or mutates siblings.

### 5. P0F: paired release-candidate and practical lab validation

Build matched firmware + native Studio and run CI. A normal Wireshark/Npcap capture from an isolated NIC must show the SCD's **three different svIDs**, destination MAC suffix `01/02/03`, repeated APPID `0x4000`, VLAN 0/PCP4, distinct 4I+4V sample values, 4000 frames/s each, one common sample counter/timebase, and PTP Announce/Sync/Follow_Up traffic. Confirm independent stream STOP/START and GUI restart adoption. Measure real MCU per-tick latency and packet egress separation; do not invent timing numbers. Keep external GPS equipment and purchased relay/BCU interoperability **optional feedback**, not release prerequisites.

## Product freeze rule

**M3.H.3 remains the latest official stable release until a matched, working M4 candidate genuinely emits the three streams.** This checkpoint is implementation progress, not a product-release announcement.

**Next thread instruction:** Start directly from **P0C1 firmware V2 staging** at `main` after merge `bcc497a914f0bec8702551f9caa0e18cbd2962de`. Reuse the new `M4ThreeSvBinaryCodec` and its CTest; do not start another SCL parser, rewrite the V1 profile ABI, add three timer tasks, or repeat PTP lock proof debates.
