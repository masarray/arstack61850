# ARStack Studio — SMV/PTP non-regression baseline

Physical checkpoint (2026-10-17, 120s ASIX/Npcap): 480,294 canonical VLAN
priority-tagged SV frames, APPID 0x4000, svID ARSTACK_SV01,
4000 nominal FPS with 0 smpCnt gaps, duplicates or reordering, 120
counter wraps; 8 quality words/channel GOOD and AUTO smpSynch=1 LOCAL in
all captured SV frames. Diagnostic mirror OFF. Domain-0 two-step PTP
had 120 Announce, 120 Sync, 120 matched Follow_Up; SOURCE local clock
class 248. Host-arrival correlation placed smpCnt=0 near integer PTP
seconds with median +49 microseconds and stable relation for 120 epochs.

That +49us is *not a measurement of hardware sample phase*; Npcap,
Windows and USB Ethernet add variable latency. A real PTP subscription,
peer-delay exchange, independent PPS and IED acceptance have not been
certified. Never replace measured AUTO=1 with an unconditional forced
flag; never automatically advertise globally traceable smpSynch=2.

Mandatory anti-regression gates for every Studio release:

1. Preserve tested M3C.6 SOURCE local AUTO qualification, phase tracking,
   timestamp evidence, failure revocation and canonical 4000-FPS stream.
   GUI-only fixes must not change the firmware protocol engine or PTP.
2. Every operator START must atomically enqueue the *current displayed*
   FREQ, eight channel SET/ENABLE commands, CT shape and then START.
   Do not use a bare START on the basis of a cached pendingApply flag.
   Fail on queue exhaustion; never report a rejected live edit as applied.
3. End-to-end angle encoding at 1 A, scale 1000:
   Ia -30 degrees => SET IA 1000 -30000 0
   Ib -150 degrees => SET IB 1000 -150000 0
   Ic +90 degrees => SET IC 1000 90000 0.
   Do not silently replace with default 0/-120/+120 degree pattern.
4. Default PTP SOURCE ON only once a verified ESP32-P4 is ready;
   smpSynch AUTO; user explicit OFF cancels any automatic restart.
   Phasor and Wave docks visible by default.
5. Voltage above Current, with both tables occupying the editor height.
   Shared buttons have centered content, light hover with dark captions,
   and a single unchanged-size checkbox indicator when hovered.
6. Windows Native Preview must package firmware from the **same commit**
   with phase servo ON, canonical hardware MAC TX timestamp evidence ON,
   and diagnostic mirror OFF. Reject outdated firmware bundles.
7. CI: PTP Core GCC/Clang/MSVC, ESP32-P4 SV+PTP SOURCE/RECEIVER,
   Studio Qt/operator workspace contracts, Windows Lifecycle and Native
   Preview must all pass before any public merge/release candidate.

This baseline documents the best known functioning stream, not third-party
subscriber certification or independent microsecond hardware accuracy.
