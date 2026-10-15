# Native Studio PTP readback authority

This refines ARStack Studio's existing C++ guided-start transaction, not a
web UI or a second serial-control implementation.

The former implementation accepted a matching role readback without requiring
the firmware's explicit `PTPROLE accepted` first. It also accepted
`PTP status=RUNNING` using a stale configured role. Periodic QML refresh
could interleave extra SHOW commands.

Now the native transaction requires an explicit role acceptance, matching
stopped role readback, configuration acceptance, START acceptance, then
role-matched PTP2 runtime evidence and a RUNNING runtime status. Bounded Qt
timeouts, session-generation revocation and STOP/rollback are retained.
The independent presentation refresh is disabled only while START is pending.

The Timing panel labels receiver LOCKED as board-reported, not proof of
independently calibrated PTP/PPS/GM timing or SV synchronization. Tests include
stale early readback, missing PTP2 evidence, role mismatch, and successful
ordered ACKs. M3 hardware acceptance remains pending.

No release, embedded firmware source, or stable NSIS/Inno installer is
modified. The existing preview workflow must again build matched firmware and
Windows installer from the exact new PR head.
