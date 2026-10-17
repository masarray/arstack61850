# M3B.1 — Canonical receiver clock generation fence

This checkpoint adds measured TIME_RECEIVER **EMAC-clock** authority without
claiming SV/PPS alignment or GPS-grade traceability.

The portable `PtpReceiverClockAuthority` runs only in the 5-ms receiver worker.
It issues a nonzero generation token on readiness transitions, grandmaster
clockIdentity/port change, LOCKED acquisition/loss, phase step, actuation failure
and STOP/START. Source switches or EMAC steps observed with a stale LOCKED state
remain blocked until an observed unlocked/relocked discipline cycle.

The C PTP status snapshot now exposes `timing_authority_generation` together
with `timing_authority_clock_usable`. SOURCE reports its existing nonzero SOURCE
generation only while ready. RECEIVER reports the portable worker-owned lease;
MONITOR cannot report a usable lease. A clock lease is a prerequisite for later
SV epoch binding, not its substitute.

Acceptance: portable host regression (GM switch, GM port change, stale lock,
step, actuation failure, holdover, stop/restart); strictly warned compile;
guarded SOURCE firmware compile including receiver translation unit.
PTP/SMV ISR, GUI and normal firmware role defaults are unchanged. Receiver
smpCnt=0 phase binding and physical ESP32-P4 validation remain future work.
`smpSynch` AUTO stays fail-closed (0) until an independent SV phase proof exists.
