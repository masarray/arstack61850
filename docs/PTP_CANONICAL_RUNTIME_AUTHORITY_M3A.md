# M3A.1 — Canonical PTP Clock Authority & Receiver START/STOP Fencing

This milestone composes one role-tagged timing-authority snapshot from the
existing SOURCE readiness/fault generation and the M3B.1 RECEIVER worker-owned
lease. This is an **EMAC PTP clock** lease, **not** PPS-to-`smpCnt=0` phase
binding or evidence of precise SV egress. `sv_phase_qualified` remains false.

The public `ar_ptp_lab_get_timing_authority()` returns a compact SOURCE,
TIME_RECEIVER or MONITOR snapshot: role, generation, usable, selected GM
identity/port and independently qualified global traceability. One private
facade status read is the only input; it is the same path as GUI/serial status,
but without verbose per-observation logging.

Safety:
- SOURCE generation is double-checked around live status retrieval. A
  stop/restart or missing/changed source generation revokes readiness.
- RECEIVER/MONITOR worker busy (pending/start/cleanup) is distinct from
  runtime *ready*. A pending cleanup blocks a new SOURCE START/configure.
- A receiver STOP request revokes exposed clock usability immediately; the
  worker finalizes hardware rollback later. Snapshot cannot resurrect a
  stale LOCKED bit while STOP/cleanup is pending.
- RECEIVER clock readiness requires genuine selected GM, measured LOCKED,
  nonzero generation and worker READY, not a status field alone.
- SOURCE lab time is not declared globally traceable; MONITOR is read-only.
- No heap, locks or ETH IOCTL are added in the high-frequency SMV ISR.
  Control-plane facade and 5 ms receiver worker remain distinct.

Acceptance: strict portable `ptp_canonical_authority_tests` covers all role
claims and invalid/stale evidence. Guarded ESP32-P4 firmware compilation
builds the facade plus receiver worker, preserving standard runtime defaults.
Physical board, GPS/PTP lock, MAC timestamp egress, RECEIVER phase discipline
and global smpSynch promotion are not yet accepted.
