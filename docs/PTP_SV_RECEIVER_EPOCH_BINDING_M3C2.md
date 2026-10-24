# M3C.2 — External RECEIVER PTP-second ↔ SV smpCnt=0 association

This extends the guarded experimental SV/PPS software-phase observer from
LAB_SOURCE to TIME_RECEIVER **only if** the canonical receiver lease is
usable (selected GM, measured discipline LOCKED, nonzero generation).
MONITOR and unlocked/holdover receiver states cannot arm the SV epoch.

The *same* source-scoped lease (role, generation, selected GM clockIdentity
and port) is checked on both sides of the START-time EMAC PTP read, each
subsequent bracketed GPTimer/PTP observation, and control-plane handoff.
Source/GM switch, phase step, unlock/relock, stop, model restart or publisher
slot/TX discontinuity revokes any previously accepted PI steering. A new
external lease requires explicit SV STOP/START to re-arm from its PTP second.

The independent `SvPtpPhaseAuthority` establishes the integer PTP second
of the last ISR-published smpCnt=0 deadline; a first observation is only a
baseline. Only consecutive epoch-generation/second continuity may arm the
existing bounded, at-most-one-tick ISR slew distributor. The callback never
reads PTP or locks; all IOCTL, source checks and computations remain in the
low-rate control-plane observer. RECEIVER arm requires the **experimental
phase-servo flag**, preserving existing nonexperimental receiver behavior.

**Limits:** software bracket/epoch correlation is not external PPS GPIO
capture, timestamped wire egress or IEC/IEEE conformance evidence. Global
traceability is not inferred from mere clock lock. No path promotes AUTO
smpSynch: it stays fail-closed (0), until independent hardware timestamp
evidence and qualification are completed.

Software gates: portable receiver lease/phase tests, previous regressions,
single-job guarded ESP32-P4 SOURCE/RECEIVER compile (no broad Draft
matrices). Still needed: silicon GM discipline, external PPS and MAC capture,
multi-minute drift/fault/recovery runs and release qualification.
