# ARStack Studio — SV/PTP non-regression baseline (2026-10-18)

## Frozen engineering evidence
Operator's latest 120s ASIX PCAP contained 480,294 canonical 4I+4V SV frames on APPID 0x4000, svID ARSTACK_SV01, 4,000 fps, VLAN 0 PCP 4; **zero** smpCnt discontinuities (120 modulo-4000 wraps), every frame smpSynch=1 LOCAL, and no broadcast diagnostic mirror. There were 120 each of PTP Announce, Sync and matched Follow_Up on domain 0, all from ARStack's own local SOURCE. Correlation between host-received smpCnt=0 frames and PTP timestamp epochs yielded ~+48.9 us median / ~+67.2 us p95 across 120 matched observations; these are **Windows/ASIX host RX offsets**, not actual hardware sampling phase error.

This is strong wire-level evidence for stable local-epoch alignment, **not** a certificate of external GM synchronization, cross-vendor acceptance, or microsecond hardware phase accuracy.

## CI release guardrails
- Native Windows Preview must embed firmware from its own exact commit with phase servo ON, canonical MAC TX timestamp evidence ON, and diagnostic mirror OFF. Never quietly fall back to the old free-running firmware.
- Preserve fail-closed AUTO=0 when clock/sample-phase evidence is absent; qualified SOURCE-local AUTO=1 is allowed; never imply AUTO=2 global time traceability without evidence.
- Independently measure GPTimer sample-epoch phase (4us implementation threshold) and MAC SV transmission publication latency (nonzero is expected, admission range currently [-4us,+250us]); do not conflate the two.
- Never modify canonical EtherType, APPID 0x4000, svID, VLAN, confRev, ASDU 4I+4V payload, sample-counter continuity or PTP profile as collateral of UI work.
- Every START must use a fresh snapshot of all eight displayed waveform setpoints: IA IB IC IN UA UB UC UN, preserving signed angle millidegrees, magnitude scaling, enable flags and quality. A bare START that can reuse old board setpoints is forbidden.
- GUI angles are user setpoints, not independent firmware readback. **Do not claim actual phase injection matches until a same-run SET transaction, board SHOW phase_mdeg, and decoded SV waveform agree**.
- Default PTP Source/AUTO, Phasor ON and Wave ON; the operator should not encounter additional dialog gates for normal PTP ON and SMV Start.

## Follow-up acceptance
With GUI Ia=-30°, Ib=-150°, Ic=+90°, capture a new immediately adjacent SV waveform *after* starting with that exact table. Verify the board's per-channel phase_mdeg (e.g. IA=-30000), then compare LS-fitted raw SV instantaneous waveform phase to the requested values. Repeat after STOP/START and reconnect. Verify PTP AUTO local acquisition and smpCnt continuity remain intact.

This baseline intentionally does not claim external IED timing lock or IED compatibility without actual subscriber evidence.
