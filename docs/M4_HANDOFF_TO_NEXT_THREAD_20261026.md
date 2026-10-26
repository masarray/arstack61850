# HANDOFF — ARStack61850 M4: 3 SCL SV outputs, shared PTP, one Studio GUI

**Snapshot:** 2026-10-26 (Asia/Jakarta). **This file supersedes the obsolete “NEXT P0C1” instructions inside** `docs/M4_EXECUTION_CHECKPOINT_20261025.md`. Verify current GitHub HEAD, PR state and CI before making changes: other threads may work simultaneously.

**Repository:** https://github.com/masarray/arstack61850  
**Tracking:** https://github.com/masarray/arstack61850/issues/333  
**Engineering architecture:** [ARD M4](ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md)  
**Latest official stable release:** `processbus-sim-m3h3-20261025` (**do not overwrite released assets**)  
**Main HEAD at handoff inspection:** `7ee7ad39d76f8072b7940f438143049549b21bb6`.

## 1. Product outcome (do not redesign)

ONE ESP32-P4 is a virtual three-Merging-Unit process-bus source based on the user's `01_SV_Stream_4I+4V_(9-2LE).scd`. The host **Studio** reads SCL/SCD, resolves three streams, compiles the existing supported V1 device profile for each, embeds all three in the validated `ARM3` V2 configuration and deploys it to firmware. The MCU never parses XML.

- **MU 1:** IED `MU01_4I_4V_1`, svID `MU01_SV1`, multicast `01:0C:CD:04:00:01`
- **MU 2:** IED `MU01_4I_4V_2`, svID `MU01_SV2`, multicast `01:0C:CD:04:00:02`
- **MU 3:** IED `MU01_4I_4V_3`, svID `MU01_SV3`, multicast `01:0C:CD:04:00:03`

Each: **APPID `0x4000`** (same APPID as specified by SCD, different MACs), priority-tagged VLAN 0 / PCP 4, `confRev=1`, `nofASDU=1`, 8 values (4I+4V) plus 8 qualities = 64B sample payload, 4,000 frames/s. Expected aggregate **12,000 canonical frames/s**, **one shared 250-µs PTP-referenced sampling grid**. Physical Ethernet egress is serialized, so the packets represent the *same sample time*, not zero inter-packet separation. Golden mapping: `tests/fixtures/scl/m4_3sv_uploaded_scd_expected.json`; reference SCD test fixture: `tests/fixtures/scl/m4-three-9-2le-reference.scd`.

User specifically requires a **single native Studio GUI** with MU1/MU2/MU3 pagination for **Table, Waveform, Phasor**; each MU's 4I+4V setpoints, quality, phase and waveform must be dedicated and isolated. Global PTP and connection controls stay shared. Do not build three GUIs or three FreeRTOS publisher tasks/timers/PTP masters.

## 2. CURRENT VERIFIED CODE STATUS (consult PRs before continuing)

| Package | Verified status at snapshot | References |
|---|---|---|
| P0A SCL→three host publisher profiles | **MERGED** | [PR #334](https://github.com/masarray/arstack61850/pull/334) |
| P0B bounded `ARM3 V2` 3-profile binary + CRC/atomic decode | **MERGED** | [PR #335](https://github.com/masarray/arstack61850/pull/335) |
| P0C1 ESP32 `MULTISV BEGIN/CHUNK/VALIDATE/COMMIT/SHOW` (control plane) | **MERGED** | [PR #336](https://github.com/masarray/arstack61850/pull/336), [protocol](M4_P0C1_MULTISV_FIRMWARE_PROTOCOL.md) |
| P0C2A portable common 4-kHz PTP epoch/smpCnt planner | **MERGED** | [PR #338](https://github.com/masarray/arstack61850/pull/338) |
| P0C2B **opt-in three canonical SV TX calls per one GPTimer tick** | **MERGED** | [PR #339](https://github.com/masarray/arstack61850/pull/339), [TX preview](M4_P0C2B_THREE_SV_TX_PREVIEW.md) |
| P0D1 per-MU independent 4I4V atomic signal banks | **MERGED** | [PR #340](https://github.com/masarray/arstack61850/pull/340), `include/ariec61850/sampled_values/m4_three_sv_signals.hpp` |
| **P0D2 firmware per-MU SET/FREQ/SHOW + TX bank hookup** | **OPEN, NOT MERGED at snapshot**; seven observed PR CI workflows SUCCESS | [PR #341](https://github.com/masarray/arstack61850/pull/341), branch `feat/m4-p0d2-stream-setpoints-tx`, [branch-specific protocol](https://github.com/masarray/arstack61850/blob/feat/m4-p0d2-stream-setpoints-tx/docs/M4_P0D2_STREAM_SETPOINT_CONTROL.md) |
| P0D3 atomic **multi-command** per-MU scenario transaction, per-MU enable/START/STOP | **NOT DONE** | implement after integrating #341 |
| P0E **one Studio GUI / MU pagination / SCD upload/deploy/readback** | **NOT DONE** | new focused PR |
| P0F **paired M4 firmware+Studio package, actual 3-stream 12kfps physical capture and real PTP phase/batch budget telemetry** | **NOT DONE** | acceptance / release candidate |

**Important distinction:** P0C2B firmware source has a selectable *triple packet transmit path*, but nominal 12,000 fps, MAC/PHY descriptor headroom, inter-frame skew and field PTP behavior have **not been physically demonstrated by a capture from the user's board**. CI build success is not on-wire validation. The shipping M3.H.3 firmware remains unchanged.

## 3. Most urgent next action — complete P0D2

1. **Check PR #341 right now** for newer commits, conflicts, and CI. At inspection, head `aa78cdaa8ffbfe7c494511713aed8f68da70b34e`; it was clean/mergeable and observed workflows passed: `C++ CI`, `ESP32-P4 PTP Lab Profile`, `ARStack Studio Native Preview`, `Public Documentation Navigation`, `Embedded Profile CI`, `Product Naming Guard`, `ESP32-P4 SMV Injector`. If still clean and all required checks pass, merge per repository policy; don't assume it stays open.
2. P0D2 provides `SVSTREAM <1|2|3> SHOW`, `SET <channel> <rms> <phase_mdeg> <quality>`, and `FREQ <millihertz>`. It wires **the three distinct signal banks into actual triple-SV TX**, not merely GUI previews, while preserving one GPTimer and one TX publisher.
3. Validate that edits to MU2's phase, RMS, quality and frequency change **only MU2 on-wire payload**. MU1 and MU3 identities, values, generations and common sampling phase must remain intact.
4. Inspect stale console wording: some existing `MULTISV COMMITTED` log line still says `output=INACTIVE_UNTIL_M4_P0C2`; after P0C2B this is misleading. Correct to explicit `CONFIG_COMMITTED_TX_REQUIRES_SELECT_AND_VERIFIED_START` or equivalent while keeping `MULTISV SELECT` and existing START gates. Do not over-advertise stable `SV-MULTI3-V1` before physical acceptance.
5. **Never silently change single-SV V1**: existing `PROFILE`, `CFG`, `START` / `STOP` handshake and LAN autonomy remain. Triple TX is selected only while STOPPED via `MULTISV SELECT`; `DESELECT` restores legacy behavior.

## 4. Implementation queue after P0D2

**P0D3 — transactional per-stream setpoints + optional independent stream on/off.** Reuse existing `SvScenarioTransaction` semantics with a stable `StreamId` or one bounded transaction per slot; stage a *complete* 8-channel signal scenario and swap the bank snapshot atomically only on valid COMMIT+readback. Preserve per-slot generation/hash across tab navigation and USB reconnect. Per-MU start/stop changes should take effect only at a shared sample boundary and rejoin current global `smpCnt`, not reset other streams or the PTP grid. One serial worker; do not allocate in 4kHz tick.

**P0E — Studio pagination.** Inspect `apps/arstack_studio/src/SclProfileModel.cpp`, `DeviceController`, `DeviceIoWorker`, `apps/arstack_studio/qml/Main.qml`, `SignalMatrix.qml` and `SignalPreview.qml`. Implement a three-entry stable IED-qualified `StreamRosterModel/StreamScenarioModel` with dedicated MU scenario state, plus one `selectedStreamId` controlling Table/Waveform/Phasor together. Reuse existing QML components and ONE serial worker. Open user SCD, compile 3 device profiles, encode `ARM3 V2`, upload using `MULTISV`, validate status/hash, SELECT, use existing verified START flow. Handle disconnected/reopened Studio by querying the board, *not* overwriting live MU setpoints with a stale GUI draft. Render visible previews only at UI frame rate, not 4kHz.

**P0F — on-wire engineer acceptance.** One ordinary isolated switch/NIC/Wireshark capture is sufficient for practical iteration: check EtherType `0x88BA` three `svID`s, MAC suffix 01/02/03, same APPID 0x4000, VLAN 0 PCP4, three independent 4I+4V payloads, aligned `smpCnt` modulo 4000, each ~4000fps and total target ~12000fps. Check PTP EtherType `0x88F7` and the existing qualified shared clock status. Record actual batch max TX submission duration, >250us overruns, drop/miss counts, stream faults and phase/egress separation. **Don't invent measurements.** Keep expensive external GPS/GM or commercial test hardware optional feedback, not development blockers.

## 5. Engineering constraints and testing commands

- **One clock/PTP lease** + one GPTimer ISR + one high-priority publisher task; 3 separately prebuilt packet buffers; per-MU signal state & DDS accumulator; immutable template offsets and shared read-only ~4KiB quarter-wave sine lookup. No XML, dynamic string building, heap allocation, 3 RTOS stacks, per-frame log formatting or mutex in hot path.
- Keep `smpSynch` as result of existing PTP qualification, not a manually asserted proof. All three share the same common clock decision. Preserve link-down fail-closed and Studio-close autonomous behavior.
- All-or-nothing roster change: `ARM3 V2` validation & CRC, bounded UART chunks, STOPPED-only COMMIT/SELECT. Keep V1 binary ABI untouched.
- CI baseline: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARIEC61850_WARNINGS_AS_ERRORS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure`. ESP-IDF workflow: [ESP32-P4 SMV Injector](https://github.com/masarray/arstack61850/actions/workflows/embedded-ci.yml) (check actual project workflow names before citing).
- SCL mapping golden fixture: `tests/fixtures/scl/m4_3sv_uploaded_scd_expected.json`; C++ tests: `tests/test_m4_three_sv_roster.cpp`, `test_m4_three_sv_binary.cpp`, `test_m4_multi_profile_staging.cpp`, `test_m4_three_sv_grid.cpp`, `test_m4_three_sv_signals.cpp`.
- Preserve released [M3.H.3 official stable](https://github.com/masarray/arstack61850/releases/latest), including its published assets and webpages. Keep M4 changes in reviewable PRs, not an oversized untested rewrite.

## 6. What the next thread should report

Always give the user **DONE / IN PR / NOT DONE / NEXT**, concrete PR numbers, commit hashes, CI details and exact claims. No empty strategy-only response. Start with **#341**, merge only on verified passing CI/merge policy, then implement a meaningful next P0D3 or P0E milestone in GitHub and keep issue #333 updated.

**Copy-paste request to the next thread:** “Lanjutkan ARStack61850 M4 berdasarkan `docs/M4_HANDOFF_TO_NEXT_THREAD_20261026.md`. Cek GitHub terbaru agar tidak mengulang pekerjaan. Prioritaskan PR #341 P0D2, kemudian P0D3 setpoint transaksi per MU, Studio MU1/MU2/MU3 pagination, dan akhirnya paket M4 + verifikasi Ethernet 3×4000fps dari satu ESP32-P4. Optimized coding, isolated setpoints, satu shared PTP/GPTimer/TX task. Kerjakan kode dan CI, laporkan done/pending/next; jangan mengubah M3.H.3 official stable.”
