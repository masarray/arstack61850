# HANDOFF — ARStack61850 M4: 3 SCL SV outputs, shared PTP, one Studio GUI


> **HANDOFF TERBARU (29 Oktober 2026):** Thread baru **WAJIB mulai dari [M4_HANDOFF_NEXT_THREAD_20261029.md](M4_HANDOFF_NEXT_THREAD_20261029.md)**. Isi handoff 2026-10-26 di bawah hanyalah arsip kronologis dan memuat status PR lama. Selalu periksa ulang live `main`, PR terbuka, CI, dan [Issue #333](https://github.com/masarray/arstack61850/issues/333).

**Snapshot:** 2026-10-26 (Asia/Jakarta). **This file supersedes the obsolete “NEXT P0C1” instructions inside** `docs/M4_EXECUTION_CHECKPOINT_20261025.md`. Verify current GitHub HEAD, PR state and CI before making changes: other threads may work simultaneously.

**Repository:** https://github.com/masarray/arstack61850  
**Tracking:** https://github.com/masarray/arstack61850/issues/333  
**Engineering architecture:** [ARD M4](ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md)  
**Latest official stable release:** `processbus-sim-m3h3-20261025` (**do not overwrite released assets**)  
**Latest known M4 merge HEAD:** `be54592af7be086f1d883d4d0db9fe1af3807224`. Reverify before modifying.

## CURRENT M4 CHECKPOINT — 2026-10-28 (AUTHORITATIVE; OVERRIDES OLDER STATUS BELOW)

**Verified merged main:** `b7fdf402a37bc5a189e2e4aa92f1b41a573babe0`. **P0E3B1 [PR #348](https://github.com/masarray/arstack61850/pull/348) is MERGED.** Earlier M4 #341–#347 are also merged; do not redo them. Stable M3.H.3 official release remains untouched.

- **DONE P0E2C1 #346:** Studio operator-deployed 3-MU ARM3 roster via existing sole DeviceIoWorker, exact ACK/hash/identity/SELECT readback, safety STOP, boot/session guards.
- **DONE P0E3A #347:** Three separate canonical `SVSTREAM <1..3> CFG` full scenario transactions with COMMIT+READBACK hash/generation, verified 3/3 MU banks. Worker retires unsent CFG tails on cancellation. Group START remains blocked.
- **DONE P0E3B1 #348:** ESP32-P4 `SVSTREAM <slot> SHOW` now emits one **atomic bank snapshot** `SNAP BEGIN / 8 ROW / SNAP END` from the actual active MU signal bank: 8 channel counts, phase, quality, enabled, frequency, CT shaping and TX-enable flag. Generation and canonical `sv_scenario_hash` are checked in the production bounded Qt `M4ThreeMuBoardReadback` for all three sequential slots. Missing/duplicated channel, changed quality/hash, boot/session change or timeout invalidate the readback. Pristine generation 0 is valid. Firmware work is UART/control-plane only, no modification to 250µs publisher path.
- **Studio operator feature:** **Inspect board** is read-only, works while output is autonomously RUNNING, **even without an SCD currently loaded**, and shows three board summaries with full channel/CT details via hover. Inspection never automatically writes a GUI draft, selects a roster, starts or stops streams. Existing sole worker, 6s per-slot timer, queued write confirmation, cancellation on STOP/disconnect/firmware reboot; PTP STOP preempts inspection and pending PTP readback is drained safely. The 2 Codex P2 review findings were repaired and resolved; new S8 deterministic session test confirms safety preemption.
- **Quality gate:** [PR #348](https://github.com/masarray/arstack61850/pull/348) exact head `9124faca0bbda9d4d9b6557f4588e0ae2518786f` completed **19/19 executed CI SUCCESS, zero failures/pending**, including Linux Qt/M4 parser test, native Windows preview, lifecycle, Windows MSVC, GCC/Clang, ESP32-P4 build/PTP source+receiver, embedded host simulation, package and RC2 policy. One release-publish step intentionally skipped. Merged as `b7fdf402a37bc5a189e2e4aa92f1b41a573babe0`. No extra CI job/workflow/matrix was introduced. [Engineer guide](M4_P0E3B1_FULL_BOARD_READBACK.md).

**SCOPE STILL NOT ACCEPTED:** Inspector produces a **manual read-only snapshot**, not automatic reconnect RUNNING-state adoption or proof that SCD selection, bank generations and board runtime are mutually consistent. Three-stream group START is deliberately still blocked; physical 12,000fps, shared smpCnt/PTP and egress timing have NOT been measured. Never claim physical acceptance from CI.

**NEXT milestone P0E3B2:** secure reconnect discovery of board-selected `MULTISV` roster/status, boot-scoped read-only adoption of all three bank states/stream TX enable, full firmware authority / SCD identity matching, and robust UI state handling without draft overwrite or automatic START. Preserve STOP as safety priority and do not create a second serial writer.

**THEN P0E3C / P0F:** verified board-owned three-MU group START/STOP and per-stream enable readback, followed by paired firmware+Studio build and independent physical NIC/Wireshark proof of three distinct 4I4V 4000fps streams on the common PTP/250us sample epoch with actual egress skew, miss/drop/overrun evidence. Do not modify Official Stable assets. Check latest main/PRs before code changes.

---

## CURRENT M4 CHECKPOINT — 2026-10-27 (THIS SECTION OVERRIDES HISTORICAL STATUS BELOW)

**Verified main:** `3d18c50c8a98e8f3604ec04256756d913153d674` after [PR #347](https://github.com/masarray/arstack61850/pull/347) squash-merged. **Do not redo any of #341–#347.** PR #346 already merged earlier at main commit `f171b7cf511015088e302a90a87b2f9d83233002`.

- **DONE P0E2C1 (#346):** `DeviceController` and supervisor now use the **existing single DeviceIoWorker** for operator-triggered SCD/ARM3 three-MU deployment; transmit-confirmed ACK, 5-second step / 120-second total deadline, boot/session/STOP/rejection guards, hash + generation + three exact MAC/APPID/svID/VLAN identities + SELECT firmware readback. M4 roster proof does **not** permit legacy V1 START. Qt/S8/firmware/native Windows preview CI passed. Emergency GUI STOP/F6 path fixed.
- **DONE P0E3A (#347):** all 3 MU draft scenarios are typed/preflighted **before any UART write**, then sent with one existing worker as three independent bounded `SVSTREAM 1/2/3 CFG BEGIN/FREQ/8 SET/SHAPE/COMMIT` transactions. Each requires exact COMMITTED token/hash/generation and independent firmware READBACK token/hash/generation; cancellation retires **unsent** exclusive commands on the worker, preserving single-writer authority. Board state after timeout is explicitly marked uncertain; never claim rollback of a command already written. STOP/F6 remains available. **All 11 executed CI checks PASS**, one release upload skipped. Codex P2 queue-retirement concern fixed and review resolved. Merged `3d18c50c8a98e8f3604ec04256756d913153d674`.
- **Current product boundary:** Studio supports **Deploy 3 MU** and **Apply 3 MU** with readback but **M4 START remains blocked** intentionally. No board-owned group START/reconnect running-state adoption has been verified; no real 3×4,000 fps/PTP wire capture yet. Stable M3.H.3 official release untouched.
- **NEXT P0E3B:** complete firmware-side SHOW bank detail for all 3 live scenarios including CT shaping/enable/quality; ensure reconnect reads all 3 authoritative slots and **adopts RUNNING board state without automatically writing drafts**, exact boot/generation roster and per-slot match. Then implement guarded group START/STOP with actual board proof and autonomy semantics.
- **NEXT P0F:** paired firmware/Studio plus independent physical NIC/Wireshark captures: 12,000 total SV frames/s, exact 3 SCL identities, 250-us shared sample grid and synchronized smpCnt, network/EMAC TX egress skew and dropped/overrun diagnostics. Host CI is NOT proof of this.

**CI economy:** avoid new workflows and parallel PRs for the same P0E3B code. Before writing, verify current main/head/open PRs and outstanding reviews; consolidate offline changes and run the existing relevant regressions once.

---

## LATEST MERGED M4 CHECKPOINT — 2026-10-26 (authoritative)

**Main HEAD checked:** `be54592af7be086f1d883d4d0db9fe1af3807224`. The following are **fully merged** to `main`, not pending PRs:

- **P0D2 #341 / P0D3 #342:** firmware isolated per-MU signal banks, bounded atomic scenario transactions, independent stream TX gates on one common PTP/GPTimer/publisher. Hardware acceptance still pending.
- **P0E1 [#343](https://github.com/masarray/arstack61850/pull/343):** one native Studio Qt/QML GUI with isolated MU1/MU2/MU3 draft-page Table, Waveform, Phasor state. Legacy autosave and unsaved frequency-tab switching bugs fixed. Merged `6de01f3ccf6bde396c0c48a85ceec6d465cc6c9f`.
- **P0E2A [#344](https://github.com/masarray/arstack61850/pull/344):** real Studio `SclProfileModel` compiles three SCD-backed exact 4I4V/4000Hz selected 9-2LE device profiles into one bounded CRC-checked `ARM3 V2` envelope with firmware-identical full-envelope hash. Preserves three genuine MAC/svID/APPID/VLAN identities and MU2 VLAN override isolation; invalidation on changed/invalid source. Merged `310be156e5e71a496ae65639d8374d075235de06`.
- **P0E2B [#345](https://github.com/masarray/arstack61850/pull/345):** production-compiled Qt host ACK-driven `M4MultiSvUploadTransaction`, one command at a time through `MULTISV STATUS/ABORT/BEGIN/48B CHUNK/VALIDATE/COMMIT/SHOW/SELECT`. Rejects mismatched transaction IDs/offsets/CRC/board boot, checks full envelope committed hash, size, generation and **all 3 MU authoritative SHOW identities before SELECT**. Native Linux/Windows Qt harnesses, matched Windows preview/installer, ESP32-P4 firmware CI all passed. One release-publish job intentionally skipped. Codex P1 harness bug fixed and review resolved. **Merged `be54592af7be086f1d883d4d0db9fe1af3807224`**.

**CRITICAL CURRENT LIMITATION:** P0E2B transaction state machine is linked in the production Studio build but is **not yet wired into actual `DeviceController` / the single existing `DeviceIoWorker`**. It has no real UART runtime/ACK deadline/reconnect flow yet. Studio M4 GUI stays **DRAFT ONLY** and its legacy V1 START path remains blocked. Do not claim successful M4 GUI deploy/START from #345, or physical 12,000fps/PTP capture from CI.

**NEXT CODE PRIORITIES:**
1. **P0E2C:** wire the proven transaction engine to one serial owner, with explicit device capability guard, `STOPPED` guard, ACK timeout, session/boot generation, cancellation on disconnect/operator STOP, serialized command ownership and full 3-SV readback before enabling any M4 START. Integrate proper Studio deploy action; verify START does **not** deselect M4 roster or mutate legacy V1 state. Add real deterministic session/worker regression to CI.
2. **P0E3:** per-MU live scenario UI transactions `SVSTREAM <slot> CFG` and complete bank readback; reconnect must adopt running firmware state instead of overwriting it. Preserve one shared PTP epoch/smpCnt, 3 independent banks.
3. **P0F:** paired firmware+Studio engineering package and **physical** independent Wireshark/NIC capture proving 3×4,000fps, authentic MAC/APPID/svID/VLAN, same PTP epoch/counter, measured egress skew, missed/drops/overrun. Do NOT invent physical evidence.

**Do not restart P0D2/D3/P0E1/P0E2A/B; all were merged. Do not modify M3.H.3 Official Stable assets.** Always recheck latest `main` and open PRs before code changes.

---

## LIVE 2026-10-26 checkpoint — supersedes older P0E status below

**Latest verified main:** `310be156e5e71a496ae65639d8374d075235de06` after **P0E1 PR #343 merged** (squash `6de01f3ccf6bde396c0c48a85ceec6d465cc6c9f`) and **P0E2A PR #344 merged** (squash `310be156e5e71a496ae65639d8374d075235de06`). All executed PR-head checks for #343/#344 SUCCESS; release upload step skipped. P0D2 #341 and P0D3 #342 were already merged before these.

- **DONE P0E1:** one native Qt/QML Studio with three independent draft MU pages, common Table/Waveform/Phasor surfaces and per-MU value/quality/frequency/phase/waveform isolation. Addressed Codex legacy persistence and frequency-tab-loss findings; native Windows/Linux preview regression passed. Still deliberately **DRAFT ONLY** until real deployment.
- **DONE P0E2A:** real `SclProfileModel` now compiles the three engineer-selected SCD/9-2LE profiles into one canonical CRC-checked `ARM3 V2` binary, with firmware-equivalent full-envelope 64-bit hash, all MAC/APPID/svID/VLAN identities intact. Compile is cached on SCD/profile changes; invalid source/duplicate identities clear the deployable binary. Linux/Windows Qt and matched native-preview regression passed, including MU2 VLAN isolation, deterministic encode and invalidation. [PR #344](https://github.com/masarray/arstack61850/pull/344).
- **IN PR P0E2B:** [#345](https://github.com/masarray/arstack61850/pull/345), branch `feat/m4-p0e2b-ack-gated-uploader`, latest inspected head `2d42a09beade084ed704101638d6eee052ff395d`, **OPEN / NOT MERGED**. Production-linked host `M4MultiSvUploadTransaction` emits one command only after matching firmware ACK: `STATUS / ABORT / BEGIN / bounded 48B CHUNK / VALIDATE / COMMIT / SHOW / SELECT`. Requires exact full commit hash, generation, size and **three exact SHOW stream identities** before SELECT; rejects reboot/transaction mismatch. Linux Qt, Windows Qt, ESP32 firmware, and other listed executed checks PASS; paired Windows native preview was still in progress at this write. Original Codex P1 harness TXMODE informational-line bug fixed (`a71d3794`, diagnostic `2d42a09b`); review thread resolved. Recheck final PR HEAD/CI before merge.
- **PENDING P0E2C:** integrate the P0E2B state machine into the **existing one** `DeviceController` / `DeviceIoWorker` serial authority with boot/session ownership, ACK timeout, cancellation, status/readback/SELECT confirmation and real Studio deploy UI. DO NOT simply bulk-enqueue MULTISV commands. Preserve V1 PROFILE/START and M3.H.3 assets. Only enable M4 START after the board confirms all 3 identities and selected roster, using verified board-owned START semantics without silent legacy deselect.
- **PENDING P0E3:** bind 3 independent full-scenario SVSTREAM transactions and firmware readback to the three GUI draft/live states; reconnect must first adopt board state without overwriting an independently running board. Retain independent slot control on the same 250-us/PTP grid.
- **PENDING P0F:** paired ESP32-P4 firmware + Studio release candidate and **physical** independent NIC/Wireshark capture proving 3×4,000 canonical SV frames/s, common smpCnt/PTP grid, exact three SCD identities, TX overrun/drop/skew and Ethernet budget. CI does not prove wire performance.

**Priority for next thread:** verify #345 exact-head CI/review, repair and merge only if all required gates pass; then implement actual P0E2C transport/GUI deployment and P0E3 per-MU readback. Do not redo merged #343/#344, do not claim physical 12kfps. Official M3.H.3 stable release remains immutable.

---

## 0. Latest verified continuation checkpoint (2026-10-26; supersedes historical sections 2–4)

- **Main:** `c79bbfbc17c78641dea5e48a9b8bd981b185c31c` after **P0D2 PR #341 merged** (commit `c57e154c0e30d40df4a5fc8c97ee039556b9d828`) and **P0D3 PR #342 merged**. The latter passed **all 10** final-head GitHub workflows including firmware, Clang ASan/UBSan, native matched preview, Studio Qt and C++ GCC/Clang/MSVC.
- **P0D3 is now CODE COMPLETE and merged**: three independent full-scenario firmware transactions reusing V1 `SvScenarioTransaction` (hash, stale-generation guard, timeout, readback), and per-MU TX gates on the **one shared** 250µs grid. See [P0D3 protocol](M4_P0D3_STREAM_SCENARIO_TRANSACTIONS.md) and [PR #342](https://github.com/masarray/arstack61850/pull/342). Physical performance **NOT** validated.
- **P0E1 actual GUI code is in [PR #343](https://github.com/masarray/arstack61850/pull/343)**, branch `feat/m4-p0e-studio-mu-pagination-model`, **NOT merged at this checkpoint**. Adds one Studio Table/Waveform/Phasor surface with MU1/MU2/MU3 pagination, a dedicated three-slot Qt scenario draft model, SCD-classified host identities, Linux/Windows native harness regression and safe **DRAFT ONLY** state. The existing single-SV START/SET/QUALITY/CT/ZERO commands are blocked in M4 draft mode; do not claim actual M4 GUI deploy functionality yet. Recheck latest PR HEAD and CI, repair failures and merge only after checks pass.
- **P0E2 next:** Host compile from same SCD profiles into three native `CompiledSvDeviceProfile` values, canonical `ARM3 V2` encode, serialized bounded `MULTISV BEGIN/CHUNK/VALIDATE/COMMIT/SELECT`, parse firmware readback/boot ID, use single existing `DeviceIoWorker` and only enable M4 START after device confirms all three identities. Reuse verified V1 start/stop handshake without accidentally deselecting the 3-stream roster.
- **P0E3 next:** Per-MU Studio `SVSTREAM <slot> CFG` complete atomic setpoint transactions and readback; reconnect must first adopt firmware state rather than overwrite live values.
- **P0F next:** Build matched ESP32-P4 firmware + Studio M4 package, then capture on physical board 3×4,000fps, same `smpCnt`/PTP reference, identities and actual peak batch timing. A CI green check alone does not establish PTP phase or Ethernet wire performance.

**The older “P0D2 OPEN / P0D3 NOT DONE” text below is historical input from the initial handoff, not the current task list. Do not repeat or overwrite those completed milestones.**

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
