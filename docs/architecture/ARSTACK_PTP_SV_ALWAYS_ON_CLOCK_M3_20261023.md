# ARStack61850 — Always-On PTP Clock & SV Sample Grid (M3 architecture contract)

**Status:** DESIGN ONLY. Baseline: checkpoint merge `e82040716c549af887579b369965ba96a890a4db` (PR #288); exact physical-PCAP firmware source `7c95f70cced4d126`. Do not change embedded SV wire encoder, 4I4V measurements, PTP SOURCE framing, or release state until gated test passes. Track implementation in #162/#154; coordinate with existing Draft #166. No main merge/public stable.

## Observed fault and why current approach cannot meet the user's requirement

2026-10-23 physical capture: 490881 SV + 375 PTP packets at 4kfps; one ~2.3599-s pause between SV packets during STOP/START. PTP Announce/Sync/Follow_Up continue, but `smpSynch` goes 2→0 and requalifies ~5.25 s after injection restarts. The requested operation is NOT PTP restart on SV STOP; it is a persistent time domain and phase-aligned sample grid for the device's full uptime.

Current `app_main.cpp`:
- `start_sample_clock()` allocates GPTimer and resets `g_sample_clock_context`, `phase_index`, `epoch_generation` and `armed_ptp_clock`.
- `stop_sample_clock()` **always** calls `revoke_sv_auto_claims(SvSyncLossReason::publisher_stop)`, stops/deletes GPTimer and zeros phase authority even if PTP SOURCE remains stable.
- In `publisher_task()`, STOP can lead to dry preflight recreation. START reuses it only if `SvAutoPreflightPolicy::can_reuse(...)` passes; otherwise clock restarts. PTP SOURCE is independent in `ptp_lab_task.cpp`, but the SV phase bridge is not.
- SV emission state is controlled by `live_control.cpp` `g_running`; loss of the 6-s Studio heartbeat stops SV, which must **not** stop PTP authority or sample clock.

## Proposed ownership — three strictly separated lifecycles

1. **ClockAuthorityService** (free-running since boot, owns EMAC PTP clock): starts independently of SV and remains active through SV STOP, host reconnect, capture, and CFG live edits. Provides internal lab GM and external receiver paths. Serves immutable `ClockSnapshot{role, state, selectedGM, generation, epoch, offset, uncertainty, age, link}`; no SV publisher ownership.
2. **SampleGridService** (independent 4000/4800-Hz GPTimer ISR + low-rate phase servo): once PTP clock is ready, keeps its logical `(ptpSecond, sampleIndex, smpCnt, phaseState)` advancing even while SV packets are suppressed. No ISR allocations, network calls, PTP I/O, blocking mutex, UART or log output; at 4000 Hz only bounded O(1) operations and RTOS notifications while transmitting.
3. **SvPublisher**: START/STOP changes **only** `svTxEnabled` and session/transport lease. START builds and commits canonical 4I4V profile then enables sending on a safe sample boundary, ideally next `smpCnt=0`. STOP suppresses packets, deasserts output lease, and never deletes/zeros ClockAuthority or SampleGrid. Atomic live value updates do not affect clock phase. Publisher faults do not revoke a healthy clock; they revoke *publisher qualification* separately.

Keep *clock synchronization state* and *SV subscription/injection state* orthogonal. There is no `smpSynch` field while SV is not transmitting; first resumed frame reflects qualified clock+grid evidence. Never falsely declare lock when source or phase is actually stale.

## Advanced policy — configurable, non-surprising

`TimeSourcePolicy`: **INTERNAL_GM_ONLY**, **EXTERNAL_ONLY**, **AUTO_PREFER_EXTERNAL**, **MONITOR_ONLY** (plus explicit OFF if needed).
- INTERNAL_GM_ONLY: boot EMAC PTP local source, reply Pdelay requests, maintain sample grid and simulator settings even when SV TX is off.
- EXTERNAL_ONLY: listen to external Announce/Sync/Follow_Up and peer delay; select compatible GM, discipline EMAC; if absent show WAITING/UNQUALIFIED, never silently become GM.
- AUTO_PREFER_EXTERNAL: initialize local PTP/grid immediately while independently discovering external candidates; only promote a compatible, qualified, stable GM after BMCA/profile checks and hysteresis. Switch local SOURCE off before external RECEIVER becomes timing authority on the same PTP domain; no simultaneous conflicting GM announcements. After GM loss use bounded HOLDOVER with uncertainty budget; optional local fallback *only if explicitly enabled*, with safe network election, source-generation changes, and requalification.
- MONITOR_ONLY: observe but neither inject SV nor act as GM (when selected).
- Expose domain, VLAN/PCP, transportSpecific, Announce/Sync cadence, Pdelay, source policy, holdover window, failover hysteresis, acceptance mode, selected GM and clock/phase status in Advanced UI. Persist policy, not unverified LOCKED state.

Important: `AUTO` cannot guarantee uninterrupted qualified phase through an arbitrary external GM time-step/change/link loss. Preserve clock continuity where physically possible, otherwise transient UNQUALIFIED → requalification; **SV STOP/START alone must never trigger this**. Do not conflflate free-running local GM, external UTC traceability, or relay PTP-slave LOCK.

## Implementation sequence (no broad rewrite)

**M3.A (P0): remove STOP coupling only.** Split `stop_sample_clock()` into `stop_sv_output()` and `shutdown_sample_grid(reason)`; make STOP/lease-expiry/START no longer revoke healthy PTP sync or delete GPTimer. Preserve logical grid while TX disabled; verified START launches output on aligned epoch. Retain source generation and phase-authority whenever link, rate/profile and clock-source generation unchanged. Validate without changing SV BER/codec or PTP packet framing.

**M3.B (P0): background supervisor.** Retarget existing `SvAutoPreflightPolicy`, phase observer and timer ownership to uptime service; make lifecycle transitions pure and testable. Publish `ClockState` and `SampleGridState` independently, retain stale-generation guards. Only real clock/GM/profile incompatibility re-arms phase.

**M3.C (P1): external receiver and BMCA.** Integrate existing `ptp_receiver_task.cpp` from #166 only after code/protocol audit and same checkpoint merge-base reconciliation. Implement announce candidate aging, proper master selection, hardware peer delay and offset discipline; add stable hysteresis/holdover/fallback. Default to no unexpected GM promotion on an energized process bus.

**M3.D (P1): Advanced settings and diagnostics.** UI policy dropdown, status tags LOCAL_GM_READY / EXTERNAL_LOCKED / HOLDOVER / WAITING / FAULT, source generation, last source change, phase uncertainty, clock age. START/STOP remains only SV TX.

## Non-regression FAT gates (must all pass to merge into checkpoint)

1. **100 STOP/START cycles**, including USB reconnect and host control-lease expiry: PTP Announce/Sync/Follow_Up continue without resetting clock generation, no SV while STOPPED, first SV resumed at defined epoch; if phase is qualified before STOP and environment unchanged, no 2→0→2 reset solely from STOP.
2. **120 s and 3600 s PCAP**, 4000 fps 4I4V, no invalid BER, duplicate/missing `smpCnt` during continuous transmission, no PTP cycles missing. Exclude operator-commanded TX OFF from packet-loss metric, but retain gap event.
3. Enter/focus-out IA and UA automatically apply on wire while SV/PTP continuously running, no restart/reset (retain PR #288 GUI+capture tests).
4. LOCAL_ONLY, EXTERNAL_ONLY, AUTO candidate-appears/disappears, stable-GM switch and HOLDOVER fault injection. Verify exact PTP frames and source identity, not only UI status; test 4k and 4.8k grids and long timer rollover.
5. ARSAS + DigSubAnalyzer decode and maintain sync; SIPROTEC 5 selected-GM/PTP LOCKED + actual 4I4V readings are final physical acceptance. CI PASS alone is never release PASS.

## Integration discipline

Branch from checkpoint `e820407...`; keep Draft #166 as reference, not blind cherry-pick. Separate PRs for M3.A, B, C, D with portable unit tests before ESP32 build, physically test M3.A before advanced external-source changes. Do not merge to main or publish stable until independent FAT.
