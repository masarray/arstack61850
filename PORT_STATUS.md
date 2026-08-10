# C++ Port Status

## Current milestone

**Phase 4D-C5 — guarded IEC 61850 Smart Control is integrated on the current branch for review and controlled laboratory interoperability testing.**

The implementation preserves the existing Phase 4C live discovery and Phase 4D Dynamic DataSet/reporting work from `main`. Buffered-report recovery and ESP32 process-bus work remain separate concerns and are not used as evidence for MMS control behavior.

## Delivered modules

| Area | Status |
|---|---|
| Phase 0 and Phase 1 process-bus foundation | Merged to `main` |
| SCL and COMTRADE engineering formats | Merged to `main` |
| TPKT/COTP and Session/Presentation/ACSE | Merged to `main` |
| MMS Initiate and confirmed services | Merged to `main` |
| Built-in Windows/Linux TCP transport | Merged to `main` |
| Live read-only MMS discovery/model parity | Merged to `main` |
| Dynamic DataSet lifecycle | Merged to `main` |
| Guarded Dynamic RCB live-trial planner | Merged to `main` |
| Guarded Direct/SBO control safety core | Integrated from Phase 4D-C1 / PR #25 |
| Live-type Oper/SBOw/Cancel MMS structure builder | Integrated from Phase 4D-C2 / PR #27 |
| CommandTermination and LastApplError/AddCause correlation | Integrated from Phase 4D-C3 / PR #28 |
| Live control discovery/session over `MmsAssociationRuntime` | Integrated from Phase 4D-C4 / PR #29 |
| Guarded physical interoperability harness and read-only inventory probes | Integrated from Phase 4D-C5 / PR #30 |

## Smart Control behavior

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

## Deterministic validation

Dedicated strict profiles cover:

- GCC and Clang `-fno-exceptions -fno-rtti` guarded-control safety semantics;
- default-deny authorization and revoked authorization between Select and Oper;
- second-client takeover rejection, selection expiry, association-loss cleanup, Cancel ownership, and `ctlNum` wrap;
- exact live-TypeSpecification control-value binding and golden MMS Data bytes for Oper;
- DPC network bit order, StepPosition, and conservative analogue structures;
- exact `origin`, `ctlNum`, `T`, `Test`, and two-bit `Check` construction;
- LastApplError layouts, unknown raw-code preservation, and object correlation;
- positive and negative enhanced CommandTermination behavior;
- live descriptor discovery using `ctlModel`, GVAA, GetNameList, CF timeouts, and ST/MX status-reference priority;
- Direct one-write behavior, SBO Select to Operate, SBOw asynchronous application-error handling, AutoSelect, stale-selection Cancel, and enhanced termination wait;
- production `MmsAssociationControlTransport` strict compile validation;
- integration of C1-C5 sources into the normal `ARIEC61850::core` build graph and CTest targets.

## Physical interoperability acceptance

Physical IED control acceptance is not claimed until evidence is retained from an isolated, non-operational laboratory IED or simulator for:

1. read-only live `ctlModel` and GVAA discovery for a known controllable object;
2. Direct normal `Oper` with exactly one command Write and observed process/status feedback;
3. SBO normal `SBO -> Oper` plus explicit `Cancel` and selection-timeout behavior;
4. Direct enhanced `Oper -> CommandTermination` positive and a safe negative LastApplError/AddCause path;
5. SBO enhanced `SBOw -> Oper -> CommandTermination` plus ownership/contention behavior;
6. association loss during selection and while waiting for enhanced termination;
7. interlock/synchrocheck behavior only where the lab setup can exercise it safely;
8. packet capture and deterministic event log proving no automatic command retry.

## Safety and claim boundary

- Live inventory and raw `ctlModel` probes are read-only.
- Mutating control requires explicit command intent, an exact object root, live type discovery, and the harness authorization gate.
- The implementation and deterministic tests are not an IEC 61850 conformance certificate.
- No hosted CI result is treated as proof of a physical IED command.
- Sampled Values, ESP32 hardware transmission, and BRCB persistence remain outside Smart Control acceptance.
