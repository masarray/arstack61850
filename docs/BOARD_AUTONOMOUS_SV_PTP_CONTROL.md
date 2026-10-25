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
| Physical Ethernet link drops | Ethernet DISCONNECTED event immediately latches explicit `ETHERNET_LINK_DOWN` SV STOP; the separate 400-consecutive-driver-TX-failure guard remains for faults even when PHY reports UP | Ethernet callback sets a nonblocking atomic PTP teardown request; task polls within 5 ms and owns EMAC cleanup; after a verified LAN fault, boot PTP autostart is latched OFF until reboot; explicit operator PTP START may re-enable the selected role | Reports actual fault/stop reason once available; **never** auto-resumes a physical fault |
| Ethernet returns after fault | Remains STOPPED until fresh verified operator START | Reconfiguration/start must satisfy existing PTP policy | Readback first; never auto-START SV. PTP SOURCE, TIME_RECEIVER, and MONITOR remain STOPPED after the LAN-loss fault until explicit operator restart |
| Board power cycle | No automatic restored SV RUN from PC draft | Compiled isolated-lab boot policy; not a claim of external GM lock | Check new boot ID, firmware compatibility and verified scenario before START |

Scope: isolated process-bus laboratory only. This deliberately does **not** persist a RUNNING command to flash/NVS or restart an injector after a power loss, nor does it disable the canonical TX fail-closed protection, PTP lease validity checks, or actual operator STOP.

The firmware advertises `SV-LAN-AUTONOMY-V1` instead of the obsolete `SESSION-LEASE` capability. Old Studio/firmware combinations must not be silently treated as compatible. Heartbeat is **diagnostic** for the publisher lifetime. START still requires fresh board-authorized scenario token/hash plus an active control session, whereas **ongoing transmission** never waits on a PC heartbeat. Compatibility should be verified against the shipped paired Studio/firmware after applying this change.

## Field evidence — 25 October 2026

User-supplied independent NIC capture report (firmware build `83439a4a15f186b0`, **before** this change) recorded approximately 1231.679 seconds, 4,926,580 SV frames, 3,999.89 frames/s, zero missing/duplicates/reordered sample counts, zero malformed frames, and 3,693 PTP Layer 2 packets. It ended `INVALID_CAPTURE` because the publisher reported STOPPED before the requested 3600 seconds. The report does NOT identify the actual STOP cause or prove USB was lost: `PUBLISHER_STOP` in SVSYNC is a consequence, not a causal classification. Full `SVLIFE` and `SVTXFAULT` around STOP remain necessary.

## Current release decision and follow-on work

The earlier **M3.H.2 RC1 physical-acceptance checklist below is historical**, superseded for the **official M3.H.3 Stable Lab release** of 25 October 2026. The owner accepted the practical 19-minute/4.7M-frame capture and verified GUI-close continuity/reconnect adoption rather than treating a 3,600-second test or expensive external timing equipment as a mandatory product-release gate.

For future development, concentrate on **multiple SV streams on one ESP32-P4 and SCL-based virtual Merging Unit simulation**. Existing Ethernet link fail-closed behavior and autonomous publishing remain non-negotiable regression invariants. Normal engineering debugging with Wireshark/Npcap and user-submitted relay/BCU feedback is welcome, not a prerequisite for the app to be usable.

See [the M3.H.3 official release](releases/PROCESSBUS_AUTONOMOUS_M3H3_OFFICIAL_20261025.md) and [Multi-SV + SCL roadmap](PROCESS_BUS_MULTI_SV_SCL_NEXT_PHASE.md).
