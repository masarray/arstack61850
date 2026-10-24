# M3D.1 — Cross-role EMAC ownership gate

Follow-up to M3C.2. The SOURCE status `ar_ptp_source_is_running()`
indicates verified fresh outbound Announce/Sync evidence, **not** that the
underlying SOURCE worker owns the EMAC PTP timestamp hardware. A SOURCE task
can be actively starting, holding over or stopping with `is_running=false`.

Use the SOURCE worker's atomic `g_ptp_started` as an explicit lifetime claim
via `ar_ptp_source_runtime_active()`, matching the RECEIVER's
`ptp_receiver_runtime_active()`. The facade now rejects all cross-role
START and config changes while either worker is active, including a SOURCE
worker without valid fresh packet evidence, or while RECEIVER cleanup is
pending. The same role-ownership predicate is used for configuration,
startup and status routing. A separate nonblocking atomic START/config
transaction guard prevents overlapping control callers from both passing
the idle check before a worker has claimed the EMAC.

This closes false 'source idle' observations without changing PTP freshness,
clock authority, SMV phase/encoder, release images or PTP protocol timing.
The measured `ar_ptp_lab_is_running()` API retains its READY semantics;
the newly exported SOURCE active query is explicitly **not** a clock lease.

Portable regression enumerates all ownership combinations. Both guarded
firmware role builds are the integration gate. Physical ESP32-P4, external
GM, PTP lock and relay/PPS egress acceptance are still outstanding.
