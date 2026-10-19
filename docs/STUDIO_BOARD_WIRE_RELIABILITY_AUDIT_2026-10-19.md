# ARStack Studio / ESP32-P4: field reliability audit (2026-10-19)

## Scope and evidence
Baseline: GUI head `f3e445616e9e7d8ba10e787ba4248a1a0396e557`, ESP32-P4 firmware build `7dc488c2c49bde03`. User confirms the former false firmware prompt has stopped. Source inspection, not a fresh physical PCAP. **A green software CI is not proof of on-wire equivalence or PTP lock within 10 seconds.**

## Source-of-truth pipeline
- QML `currentModel` and `voltageModel` are the only START table snapshots. `startScenarioSnapshot()` flushes pending editors. `DeviceController::startWithScenario()` validates 8 rows and queues `FREQ`, eight `SET`, eight `ENABLE`, `SHAPE CT`, then `START`.
- The serial worker grants all-or-none **queue admission** for this ordered batch. Firmware applies individual serial commands to `SvLiveSignalBank`, publishes generation per command, and the 4kHz publisher snapshots the signal state before generating its waveform and patching 64 payload bytes (eight contiguous 32-bit values and 32-bit quality flags).
- Queue admission is **not** a device commit ACK and cannot certify each transmitted SV payload. Engineering units are quantized into signed wire counts and millidegrees. A real PCAP decode and comparison is required to claim GUI=wire.
- SV sample counter follows runtime rate/modulus, and skipped ticks revoke SOURCE LOCAL evidence. NIC/Npcap arrival jitter must not be confused with proved missing SV frames.

## Confirmed software bug fixed in this patch
- `SignalMatrix.qml` checkbox called `markScenarioDirty()`, setting `pendingApply=true`, **before** testing `!pendingApply` to send live `ENABLE`. Thus a running-board checkbox edit never sent the new state in the normal clean-draft path.
- `Main.qml` frequency text edit had the same inverted ordering, preventing live frequency transmission.
- Both now capture live eligibility **before** marking draft dirty, send the command, then clear pending only if admitted. Failed admission retains a pending draft. Regression source gates protect both paths.
- This corrects *command delivery intent*. A queued command still needs physical readback / PCAP proof.

## Additional remaining risks (not certified / not silently patched)
1. **Live writes not atomic across 4I+4V:** firmware SET, ENABLE, QUALITY, FREQ update independent generations; a packet emitted between writes can represent a mixed state. A transactional `SCENARIO BEGIN/COMMIT/ABORT` with generation, full validation and ACK/readback is needed for strong GUI=wire guarantees (especially while RUNNING). Do not call the current serial batch an atomic firmware transaction.
2. **No definitive applied-generation attestation** on GUI: a 'queued' notification is not a positive device ACK. A typed `appliedGeneration`, current waveform setpoint snapshot and sequence-correlated ACK should be connected to the UI. Preserve single serial worker ownership.
3. **PTP AUTO is conditional:** firmware preflight phase steering, SOURCE lease, genuine EMAC TX descriptors, and three clean live epochs gate `smpSynch=1`. Field success in <=10 seconds is still not established. Never replace `smpSynch=0` with a forced LOCAL bit.
4. **Board discovery:** last verified port is only a priority hint; semantic `IDENTIFY` is mandatory. On any failure show UNIDENTIFIED/UNKNOWN, never `firmware required` unless an incompatible identity was positively verified.
5. **Performance budget:** 4000 SV frames/s with continuous smpCnt, zero firmware TX failures, no hot-path allocations/logging, bounded CPU/heap and no GUI frame stalls while USB telemetry is active.

## Mandatory hardware acceptance
1. Cold launch 10 times and reopen 10 times with board on same/changed COM; exact build must be semantically verified without false firmware prompt. Unplug/replug must never auto-START.
2. Use non-default, asymmetric values on **all eight** channels, including nonzero neutrals, positive and negative phase angles, different quality words and enable masks. START, capture >=120s PCAP and compare every decoded sample value and quality against fixed-point quantization and expected sine/CT shaping, NOT only GUI phasor preview.
3. While RUNNING, edit frequency 50->60->50 and disable/re-enable each channel; collect firmware `Live signal generation ... committed` and pcap waveform to verify changes and boundary behavior.
4. Measure STOP => zero further outgoing SV after allowed transport tail; START/STOP cycle continuity and every 4000-frame modulo epoch. Compare transmitter counters with NIC capture counters before assigning missing-sample blame.
5. For SOURCE + AUTO, capture preflight prior to START, first 15s `SV/PTP AUTO` logs and first 120s SV+PTP PCAP; qualify LOCAL only with fresh MAC pair evidence and phase under bound. Time-to-LOCAL <=10s is a target, not a verified achievement.
6. Include invalid setpoint, queue-full, serial-disconnect, PTP stop/link-down, cold firmware build mismatch, COM renumber and 1h sustained injection tests. Every failure must go fail-closed; no silent profile changes.

## Status
This PR fixes two proven live-edit command-path bugs and introduces CI guards. Full hardware system-level 'smart / reliable / optimized' signoff remains **OPEN** until the above evidence is collected.