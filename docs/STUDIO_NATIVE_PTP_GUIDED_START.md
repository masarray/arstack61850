# Native ARStack Studio — acknowledged PTP start

This implementation improves the existing **Qt/C++/QML ARStack Studio Windows
application**. It does not create a web control surface, a CLI for operators,
or an independent firmware flasher.

The Timing panel submits one *Start configured PTP* intent through the
SmartSessionController. DeviceController then enforces the following
acknowledgment boundary over the existing generation-scoped serial worker:

1. Select SOURCE, RECEIVER or MONITOR; await the matching
   `PTPROLE role=<role> ... running=0` readback.
2. Apply bounded timing settings; await `PTP configuration accepted`.
3. Send START; await `PTP start accepted`.
4. Request live status; require `PTP status=RUNNING` and exact requested role.

Qt event-loop timers bound each phase; there are no thread sleeps, blocking
reads, retries without acknowledgment, or extra owners of the serial port.
STOP, disconnect and session generation changes disarm pending starts.
When a failed phase may already have queued START, a STOP is queued on
the same ordered transport.

Deterministic native session harness regressions cover successful ACK
order, config rejection, cancellation and wrong role/generation.

**Limitations**: firmware-reported RUNNING/LOCKED is not independently measured
PTP grandmaster accuracy, PPS, MAC timestamp phase, or receiver acceptance.
No changes to the stable firmware, embedded PTP Draft stack, public installer
or `smpSynch AUTO` promotion. An independently tested Windows installer with
matching firmware remains a release gate; this source change is not itself a
published release.
