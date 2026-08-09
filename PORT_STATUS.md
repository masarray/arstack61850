# C++ Port Status

## Current milestone

**Phase 4D-R3 — bounded BRCB operational semantics, association ownership, MMS replay/purge controls, lifecycle handling, and replay-capable recovery state v2 are implemented for review.**

The active Phase 4D work is intentionally carried as a stacked draft-PR series on top of the ESP32-P4/SV work. `main` does not yet represent this milestone.

## Delivered modules

| Area | Status |
|---|---|
| Phase 0 and Phase 1 process-bus foundation | Merged to `main` |
| SCL and COMTRADE engineering formats | Merged to `main` |
| TPKT/COTP and Session/Presentation/ACSE | Merged to `main` |
| MMS Initiate and confirmed services | Merged to `main` |
| Offline DataSet/RCB/report monitoring | Merged to `main` |
| Transport-injected MMS association/runtime | Merged to `main` |
| Persistent RCB subscription runtime | Merged to `main` |
| Built-in Windows/Linux TCP transport | Implemented in Phase 4C stack |
| Live read-only MMS discovery/model parity | Implemented in Phase 4C stack |
| ESP32-P4 BRCB raw-partition backend + geometry/latency probe firmware | Implemented; physical measurement pending |
| BRCB retained history + independent delivery cursor | Phase 4D-R1 / PR #16 |
| Association-aware BRCB reservation/Owner/ResvTms state machine | Phase 4D-R2a / PR #17 |
| Per-request association identity propagated through MMS writes | Phase 4D-R2b / PR #18 |
| MMS RptEna/EntryID/PurgeBuf/ResvTms/Owner operational object bank | Phase 4D-R2c / PR #20 |
| TCP/reset/COTP-DR association-loss lifecycle bridge | Phase 4D-R2d / PR #22 |
| Replay-capable BRCB recovery image v2 with v1 restore | Phase 4D-R3 / PR #23 |

## Phase 4D behavior

- BRCB delivery no longer destroys retained report history.
- The bounded ring has an independent delivery cursor and explicit retained-history size.
- `EntryID` can resume after a retained report; all-zero `EntryID` rewinds to the oldest retained report.
- `PurgeBuf` clears retained history and replay-gap state without rolling EntryID backward.
- Queue overflow remains bounded and records both dropped-report count and a replay-gap condition.
- Reservation ownership uses an opaque stable Owner identity plus a separate ephemeral MMS association ID; it is not coupled to IP addresses or socket handles.
- A second live association cannot steal an owned BRCB, including one presenting the same stable Owner identity.
- `ResvTms=0` releases on association loss; positive `ResvTms` retains the stable Owner until expiry and permits that Owner to reconnect with a new association ID.
- Association loss always disables `RptEna`.
- TCP EOF/socket/local close, runtime reset, and incoming COTP Disconnect Request use the same exactly-once lifecycle bridge.
- MMS static writes carry caller-owned association context without changing legacy non-contextual object callbacks.
- The operational MMS object bank exposes `RptID`, `RptEna`, `DatSet`, `ConfRev`, `PurgeBuf`, `EntryID`, `ResvTms`, and `Owner`.
- Recovery image v2 persists the full retained replay window, delivery cursor, replay-gap flag, EntryIDs, report PDUs, sequence state, next EntryID, and dropped-report counter.
- Recovery image v2 accepts legacy v1 images. A v1 image restores with its original undelivered-only semantics.
- Reboot recovery deliberately returns reporting disabled and does not restore live association identity or Owner connection state.

## Validation completed without physical ESP32-P4 flash evidence

The BRCB hard profile is compiled with GCC and Clang using `-fno-exceptions -fno-rtti`, and its evidence gate currently covers:

- bounded BRCB capture, EntryID progression and BufOvfl behavior;
- retained replay history and independent delivery cursor;
- multi-client ownership, reservation, reconnect and expiry semantics;
- encoded MMS writes crossing BER decode -> dispatcher -> association-aware BRCB control;
- EntryID resume/rewind, invalid EntryID rejection and PurgeBuf;
- TCP/reset/COTP-DR association-loss lifecycle behavior;
- A/B checkpoint generation, torn-write fallback and corruption fallback;
- recovery state v2 preserving delivered history, cursor and replay gap;
- durable purge with monotonic EntryID after reboot;
- backward restore of a legacy v1 recovery image;
- absence of C++ exception runtime symbols in the hard-profile binaries.

## Physical non-volatile storage gate

The ESP32-P4 non-volatile adapter and probe firmware are implemented, but **physical acceptance is not yet claimed**.

Current evidence status remains:

`READY_NOT_MEASURED`

The dedicated ESP32-P4 probe can read the actual partition erase geometry and measure erase/write/read latency using the real `brcb_state` raw partition. Hosted CI cannot replace this evidence.

Before changing the status to hardware-measured/passed, capture and retain:

1. actual `esp_partition_t::erase_size` and accepted A/B/probe geometry from the target board;
2. measured erase/write/read latency records from the destructive isolated probe erase unit;
3. the actual flash-device endurance/P-E rating used to convert checkpoint frequency into a wear budget;
4. an accepted checkpoint interval and worst-case lifetime policy based on that rating;
5. controlled physical power-loss evidence demonstrating committed-bank recovery and torn-write fallback on the real board.

Recovery state v2 can be larger than v1 because delivered replay history is intentionally retained. The checkpoint operation must therefore fail closed if the measured bank geometry cannot hold the configured worst-case retained state; software must not silently discard replay history merely to fit flash.

## Remaining Phase 4D acceptance gates

- Complete the repository-wide CI matrix for the final stacked head and resolve any non-BRCB regression if reported.
- Run the ESP32-P4 flash probe on physical hardware and archive UART geometry/latency evidence.
- Bind the actual flash P/E endurance rating into an explicit checkpoint-frequency/wear policy.
- Run controlled physical power-cut/reboot tests against the A/B journal and recovery-v2 image.
- Review and merge the stacked Phase 4D PRs in dependency order once their parent branches are accepted.

## Deliberately deferred

Mutable BRCB definition/configuration lifecycle for fields such as `OptFlds`, `BufTm`, `TrgOps`, and `IntgPd` is not mixed into the ownership/recovery work. Those fields require their own configuration/versioning rules.

IEC 61850 control-model parity (`ctlModel`, Direct/SBO, enhanced security, `Oper`, `SBO/SBOw`, `Cancel`, command termination, `LastApplError`/`AddCause`, interlock/synchrocheck, and no automatic command retry) is the next major functional phase after the Phase 4D hardware acceptance gate is closed.
