# C++ Port Status

## Current milestone

**Phase 4D-C4 — guarded IEC 61850 control-model software parity is implemented for review: live `ctlModel`/TypeSpecification discovery, Direct/SBO normal and enhanced sequencing, exact `Oper`/`SBOw`/`Cancel` structures, CommandTermination correlation, and LastApplError/AddCause diagnostics.**

The active Phase 4D work is intentionally carried as a stacked draft-PR series. `main` does not yet represent this milestone.

Sampled Values / ESP32-P4 process-bus work is developed and validated on a separate branch/thread. Its hardware success is not used as evidence for MMS control behavior.

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
| BRCB retained history + independent delivery cursor | Phase 4D-R1 / PR #16 |
| Association-aware BRCB reservation/Owner/ResvTms state machine | Phase 4D-R2a / PR #17 |
| Per-request association identity propagated through MMS writes | Phase 4D-R2b / PR #18 |
| MMS RptEna/EntryID/PurgeBuf/ResvTms/Owner operational object bank | Phase 4D-R2c / PR #20 |
| TCP/reset/COTP-DR association-loss lifecycle bridge | Phase 4D-R2d / PR #22 |
| Replay-capable BRCB recovery image v2 with v1 restore | Phase 4D-R3 / PR #23 |
| Guarded Direct/SBO control safety core | Phase 4D-C1 / PR #25 |
| Live-type Oper/SBOw/Cancel MMS structure builder | Phase 4D-C2 / PR #27 |
| CommandTermination + LastApplError/AddCause correlation | Phase 4D-C3 / PR #28 |
| Live control discovery/session over MmsAssociationRuntime | Phase 4D-C4 / PR #29 |

## Phase 4D-C control behavior

- `ctlModel` values 0..4 match the C# oracle: status-only, Direct normal, SBO normal, Direct enhanced, and SBO enhanced.
- Control roots are Data Objects (`LD/LN.DO`); service-leaf references are rejected as roots.
- The control safety planner is association-aware and authorization is **default deny**.
- `ctlNum` is monotonic in the range 1..255 and wraps to 1; zero is never auto-generated.
- SBO retains an immutable selected sequence across `ctlVal`, origin, `ctlNum`, `T`, optional `operTm`, `Test`, synchrocheck, and interlock-check.
- A second association cannot operate or cancel another association's selection.
- Selection timeout, association loss, value/check mismatch, or authorization loss fail closed.
- Direct normal and SBO normal complete at the accepted MMS service boundary.
- Direct enhanced and SBO enhanced remain pending until a correlated CommandTermination arrives.
- Ordinary ST/MX process reports cannot complete an enhanced command.
- LastApplError retains the raw ControlError/AddCause values while mapping the standard ControlError 0..3 and AddCause 0..27 names.
- Positive Oper-only CommandTermination requires an exact `CO/Oper` reference.
- The live TypeSpecification is the source of truth for `ctlVal`, origin, `ctlNum`, `T`, `Test`, `Check`, and optional `operTm`; unknown vendor fields are rejected rather than guessed.
- DPC uses MMS network bit order (`Off=01`, `On=10`).
- `Check` is exactly two bits: bit 0 synchrocheck and bit 1 interlock-check.
- Normal SBO Select is a Read of `SBO`; SBO enhanced SelectWithValue is exactly one `SBOw` Write.
- `Cancel` is built from the exact retained selected sequence.
- Before `SBOw` or `Oper`, stale InformationReports are drained; reports arriving during the confirmed exchange remain available for application-error/termination correlation.
- Missing CommandTermination expires as a control timeout and does not automatically fault an otherwise healthy MMS association.
- **No automatic command retry is performed.**

## Control validation completed without issuing a physical command

Dedicated strict profiles currently cover:

- GCC and Clang `-fno-exceptions -fno-rtti` guarded-control safety semantics;
- default-deny authorization and revoked authorization between Select and Oper;
- second-client takeover rejection, selection expiry, association-loss cleanup, Cancel ownership, and `ctlNum` wrap;
- exact live-TypeSpecification control-value binding and golden MMS Data bytes for Oper;
- DPC network bit order, StepPosition and conservative analogue structures;
- exact `origin`, `ctlNum`, `T`, `Test`, and two-bit `Check` construction;
- LastApplError standard/ctlObj-omitted layouts, unknown raw-code preservation, and object correlation;
- positive/negative enhanced CommandTermination behavior;
- live descriptor-discovery orchestration using `ctlModel`, GVAA, GetNameList, CF timeouts, and ST/MX status-reference priority;
- Direct one-write behavior, SBO Select -> Oper, SBOw asynchronous application-error window, AutoSelect, stale-selection Cancel, and enhanced termination wait;
- production `MmsAssociationControlTransport` strict compile validation;
- integration of C1-C4 sources into the normal `ARIEC61850::core` build graph and normal CTest targets.

## Remaining control acceptance gates

The software path is ready for controlled interoperability testing, but **physical IED control acceptance is not yet claimed**.

Before any production-control or conformance claim, run on an isolated/non-operational laboratory IED or simulator and retain evidence for:

1. live `ctlModel` and GVAA discovery for a known controllable object;
2. Direct normal `Oper` with exactly one command Write and observed process/status feedback;
3. SBO normal `SBO -> Oper` plus explicit `Cancel` and selection-timeout behavior;
4. Direct enhanced `Oper -> CommandTermination` positive and at least one safe negative LastApplError/AddCause path;
5. SBO enhanced `SBOw -> Oper -> CommandTermination` plus ownership/contention behavior;
6. association loss during selection and while waiting for enhanced termination;
7. interlock/synchrocheck behavior only where the lab setup can exercise it safely;
8. packet capture and deterministic event log proving no automatic command retry.

## BRCB / non-volatile storage boundary

BRCB recovery-v2 software and the ESP32-P4 raw-partition adapter/probe remain a separate persistence concern from Sampled Values hardware transmission and from MMS control.

A board successfully booting/flashing another ESP32-P4 application does not by itself prove the BRCB flash geometry, erase/write/read latency, endurance budget, or controlled power-loss recovery. Those NVM measurements remain an independent hardware gate until their dedicated probe evidence is archived.

## Safety / claim boundary

- The C4 branch implements software parity and deterministic validation; it is **not** an IEC 61850 conformance certificate.
- No physical IED command is claimed from hosted CI.
- Live control must remain explicit, authorization-gated, and laboratory-scoped until interoperability evidence is accepted.
- Sampled Values / ESP32-P4 development remains outside this control branch.
