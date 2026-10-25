# ESP32-P4 autonomous publishing — field P0 contract

The ESP32-P4 owns the IEC 61850 SV and PTP data plane. ARStack Studio owns **operator commands and observability**, not the continuous eligibility to transmit.

## Transitions

| Event | ESP32-P4 SV | ESP32-P4 PTP | Studio on reconnect |
| --- | --- | --- | --- |
| Verified operator START + successful CFG commit/readback, physical Ethernet UP | RUNNING at configured grid | Unchanged according to board PTP role | Reads real state with SHOW; never re-START just to discover |
| Studio closes, crashes, USB COM15 temporarily disappears or heartbeat expires | **Continue RUNNING** with last verified RAM scenario | **Continue** in currently configured role | UI says disconnected; board is NOT asserted STOPPED |
| Studio returns with a verified same board/boot | Unchanged until operator explicitly commands | Unchanged | Reconcile via identity + SHOW / SVLIFE + PTP readback, no guessed READY/STOP or unsolicited profile replacement |
| User clicks SV STOP | Explicit STOP suppresses SV publisher | Unchanged (independent PTP control) | Verified STOP readback; GUI STOP is not treated as USB disconnect |
| User clicks PTP STOP | Unchanged | Explicit PTP STOP | Verified PTP stopped readback |
| Physical Ethernet link drops | Ethernet DISCONNECTED event immediately latches explicit `ETHERNET_LINK_DOWN` SV STOP; the separate 400-consecutive-driver-TX-failure guard remains for faults even when PHY reports UP | Ethernet callback sets a nonblocking atomic PTP teardown request; task polls within 5 ms and owns EMAC cleanup; the existing boot PTP supervisor may restore SOURCE after a genuine link-up cycle under its bounded recovery policy | Reports actual fault/stop reason once available; **never** auto-resumes a physical fault |
| Ethernet returns after fault | Remains STOPPED until fresh verified operator START | Reconfiguration/start must satisfy existing PTP policy | Readback first; never auto-START SV. Boot-supervised PTP SOURCE may recover after a qualifying LAN link cycle, independent of SV |
| Board power cycle | No automatic restored SV RUN from PC draft | Compiled isolated-lab boot policy; not a claim of external GM lock | Check new boot ID, firmware compatibility and verified scenario before START |

Scope: isolated process-bus laboratory only. This deliberately does **not** persist a RUNNING command to flash/NVS or restart an injector after a power loss, nor does it disable the canonical TX fail-closed protection, PTP lease validity checks, or actual operator STOP.

The old `SESSION-LEASE` capability name is retained for wire-protocol interoperability with existing Studio versions; its heartbeat is now **diagnostic** for the publisher lifetime. START still requires fresh board-authorized scenario token/hash plus an active control session, whereas **ongoing transmission** never waits on a PC heartbeat. Compatibility should be verified against the shipped paired Studio/firmware after applying this change.

## Field evidence — 25 October 2026

User-supplied independent NIC capture report (firmware build `83439a4a15f186b0`, **before** this change) recorded approximately 1231.679 seconds, 4,926,580 SV frames, 3,999.89 frames/s, zero missing/duplicates/reordered sample counts, zero malformed frames, and 3,693 PTP Layer 2 packets. It ended `INVALID_CAPTURE` because the publisher reported STOPPED before the requested 3600 seconds. The report does NOT identify the actual STOP cause or prove USB was lost: `PUBLISHER_STOP` in SVSYNC is a consequence, not a causal classification. Full `SVLIFE` and `SVTXFAULT` around STOP remain necessary.

Do not relabel the M3.H.2 RC1 release as stable until a **new exact-head firmware+GUI** physical acceptance demonstrates:
1. Start in Studio, then disconnect USB/close/crash Studio for at least 10 minutes while independent NIC captures uninterrupted SV/PTP.
2. Reconnect Studio; verified `RUNNING` should appear without replaying START, and no change in smpCnt phase trajectory should be seen.
3. Explicit operator SV STOP works promptly while PTP remains in its role; explicit PTP STOP works independently.
4. Unplug Ethernet in isolated test network. SV must stop fail-closed, PTP must tear down promptly; SV must not resume implicitly on replug. Any bounded, policy-authorized PTP SOURCE recovery must be recorded separately.
5. Run a 3600-second uninterrupted receiving-NIC SV test, then verify independent ARSAS/DigSubAnalyzer receive and decode with APPID/svID/smpCnt evidence.
