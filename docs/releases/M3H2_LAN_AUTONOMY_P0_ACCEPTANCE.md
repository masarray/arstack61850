# P0 — autonomous SV/PTP over LAN without Studio

**Lab engineering candidate. Not Public Stable.** This change intentionally replaces the obsolete 6-second serial/USB output lease with **board-owned runtime**.

## Evidence driving this change

Physical independent NIC capture, 25 October 2026, on firmware build `83439a4a15f186b0`, COM15:
- 1,231.679 seconds / **4,926,580** SV frames observed, measured ~**3,999.89 fps**;
- **zero** missing/duplicate/reordered `smpCnt`, malformed frames or counter discontinuities;
- PTP had 1,231 Announce, 1,231 Sync and 1,231 Follow_Up frames;
- capture was flagged **INVALID_CAPTURE** because the transmitter stopped after ~20 minutes, prior to the requested 3,600 seconds. Last Studio telemetry indicates `running=false`, `SVSYNC reason=PUBLISHER_STOP`, PTP SOURCE still running, device missed/TXfail 0. **This does not identify why STOP was commanded**; the attached PCAP/CSV and report do not provide the authoritative board `SVLIFE stop_cause`.

## Ownership model

| Event | SV output | PTP output/clock | Studio behavior |
| --- | --- | --- | --- |
| Verified operator START with LAN link up | RUNNING after firmware CFG commit/readback | Independent configured PTP task | Device confirms fresh board readback |
| Studio UI closes, COM cable unplugged, USB resets, serial heartbeat lost | **Continue RUNNING** | **Continue RUNNING** while LAN up | No implicit STOP; no assumed remote state |
| Operator explicitly presses SV STOP in Studio while connected | STOP with operator cause | Unchanged PTP clock/publisher; separate PTP STOP is available | Verify SV STOP ACK and stopped readback |
| Operator explicitly stops PTP | SV unchanged | PTP STOP | Verify PTP STOP ACK and stopped readback |
| Physical Ethernet link loss or Ethernet driver STOP | Immediate fail-closed SV STOP, recorded as `ETHERNET_LINK_DOWN` | Suppress PTP wire TX and revoke SOURCE READY; preserve internal EMAC clock for requalification | Show PHY/board diagnostics on next reconnect |
| LAN restored after loss | Remains STOPPED; **no unrequested auto-START** until verified operator START | PTP wire TX resumes from same timebase and must newly qualify Announce + Sync/Follow_Up | Re-read authoritative profile, SV and PTP state |
| ESP32-P4 firmware reboot | STOPPED (RAM session reset) | Startup policy config applies | Treat new boot_id as a new session; never invent RUNNING |
| Actual 400 consecutive canonical TX failures | Fail-closed STOP, `RUNTIME_FAULT` | Independent PTP safety/clock logic | Report TX burst first/last error |

**Important:** a GUI that opens in an idle local state does **not** automatically STOP a running ESP32. It **adopts the board's verified readback**. A user-commanded STOP explicitly stops output. Replaying an old STOP intention automatically after a disconnect is prohibited because it can unexpectedly shut down an independently publishing board.

No autonomous SV broadcast occurs immediately on firmware boot without explicit verified START: this candidate deliberately avoids uncommanded laboratory process-bus injection. Once started, the board may continue without Studio.

## Safety and protocol

- New firmware identity capability: `SV-LAN-AUTONOMY-V1`. `SESSION-LEASE` no longer advertises a safety/output lease. Pair new Studio and firmware from the same SHA.
- `HEARTBEAT` is retained as **diagnostic**, not authority. `SVLIFE lease_ms=0 watchdog=0` is intentional.
- `SVLAN owner=BOARD physical_link=UP|DOWN output=RUNNING|STOPPED studio_control=OPTIONAL` appears in SHOW.
- `STOP` remains high-priority and explicit; startup/commit requires the same token, hash, boot ID and profile readback validation as before.
- PHY down stops SV without waiting for 400 failed frame attempts; TX failures unrelated to PHY down still stop fail-closed at the existing threshold.
- PTP task suspends outbound work while PHY down and invalidates old source freshness; after link recovery it must rebuild qualification with new successful packets.

## Bench acceptance before merging to a stable public release

1. With LAN UP and physical receiver capture active, START and confirm real 0x88BA at ~4,000fps and PTP 0x88F7.
2. **Close Studio without pressing STOP** and leave COM disconnected for at least 15 minutes; independent PCAP must show uninterrupted `smpCnt`, real SV and PTP.
3. Unplug only **USB**, not LAN: same uninterrupted physical PCAP expectation. Replug USB, reopen Studio and confirm displayed RUNNING from fresh board SHOW without sending START or STOP.
4. Press **SV STOP** while Studio connected: confirm actual SV ceases and board records `OPERATOR_STOP`; PTP retains its independent state. Press PTP STOP separately and verify 0x88F7 ceases.
5. START again, then disconnect Ethernet: verify SV stops promptly, board shows `ETHERNET_LINK_DOWN`, and PTP does not continue sending. Reconnect LAN: verify SV stays STOPPED until a deliberate START, while SOURCE recovers only after fresh PTP evidence.
6. Run **3,600 seconds** with Studio connected and another 3,600 seconds headless if practical; attach full unfiltered NIC PCAPNG, continuity report and firmware SVLIFE/SVTXFAULT records. Confirm actual receiver acceptance in ARSAS and DigSubAnalyzer.
7. Check no PTP clock/phase claim is asserted solely from successful packet counts. Physical PTP alignment still requires independent instrumentation.

**Do not close [P0 issue #324](https://github.com/masarray/arstack61850/issues/324) or relabel M3.H.2 RC1 stable based on CI or screenshots.**
