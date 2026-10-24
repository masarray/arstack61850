# ARStack Studio SV–PTP no-regression baseline — 18 Oct 2026

This document freezes observed evidence and operator-visible behavior without calling host-receiver timing a hardware accuracy certificate.

## Known-good evidence

The user's independent 120-second ASIX/Npcap capture demonstrated: 480,294 canonical SV frames (svID ARSTACK_SV01, APPID 0x4000), no diagnostic mirror, no smpCnt gaps/duplicates, 120 counter wraps, all smpSynch=1 under SOURCE + AUTO. PTP domain 0 carried 120 Announce, 120 Sync, and 120 matched Follow_Up. Host-relative smpCnt=0 to PTP epoch estimate median +48.9us, P95 +67.2us. This includes frame-publication and capture path delay; it is NOT a hardware sample-clock phase-error guarantee.

## Non-regression gates

1. Native Preview firmware must match the exact GUI commit and include PTP phase servo, MAC TX timing evidence, and broadcast mirror disabled. Never regress to a v0.1.1 bundle built from the old main firmware.
2. Local smpSynch=1 is qualified by actual sample timing; neither PTP visibility nor manual metadata suffices. Do not assert global smpSynch=2 for a local class-248 SOURCE.
3. At 50 Hz / 80 SPC, canonical SV rate is 4000 fps and smpCnt wraps 3999→0, with no healthy-stream discontinuities.
4. Every START flushes editor debounces and sends the eight current table setpoints as an ordered FREQ/SET/ENABLE/SHAPE/START batch; serial phase encoding is signed millidegrees (Ia −30.00° -> -30000). No silent reuse of stale ESP32-P4 setpoints.
5. Editing live phase-link behavior must use the same ListModels as the visible table and enqueue all affected phases. Do not claim on-wire equality without PCAP and timestamp-matched setpoints.
6. PTP SOURCE has exactly one owner for the verified-session default ON behavior; user OFF cancels it. Do not add duplicate startup timers, modal barriers, or write PTP/SMV clock state from presentation.
7. GUI: readable brand/logo, consistently centered buttons, dark text for pale hover, only one stable checkbox indicator, vertical voltage/current tables with bounded heights; preserve Windows DPI 100/150/175 acceptance.
8. GUI-only improvements must not alter Ethernet SV frames, PTP hardware epoch lease, timestamp/ISR behavior, wire quality, or smpCnt logic.

## Next evidence for release

Studio Qt CI, Windows Lifecycle and exact-head Native Preview must pass. After installation, inspect serial FREQ/SET phase commands and independent decoded waveforms for Ia −30°, Ib −150°, Ic +90° and the requested Ua/Ub/Uc. Capture >120 seconds after AUTO becomes local 1, include STOP/START and PTP SOURCE fault/recovery. Confirm no missing counters and stable epoch alignment. Physical synchronization accuracy and cross-vendor subscriber acceptance are separate from host timing screening.
