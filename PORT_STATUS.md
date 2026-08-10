# C++ Port Status

## Current milestone

**Phase 4D-C5 — guarded IEC 61850 Smart Control is integrated for controlled laboratory interoperability: live `ctlModel`/TypeSpecification discovery, Direct/SBO normal and enhanced sequencing, exact `Oper`/`SBOw`/`Cancel` structures, CommandTermination correlation, LastApplError/AddCause diagnostics, and a read-only-by-default evidence harness.**

The integration preserves Phase 4C live discovery and the Dynamic DataSet/reporting work already merged to `main`. Buffered-report recovery and ESP32 process-bus work remain separate concerns and are not used as evidence for MMS control behavior.

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
- LastApplError retains raw ControlError/AddCause values while mapping the standard ControlError 0..3 and AddCause 0..27 names.
- Positive Oper-only CommandTermination requires an exact `CO/Oper` reference.
- Live TypeSpecification is authoritative for `ctlVal`, origin, `ctlNum`, `T`, `Test`, `Check`, and optional `operTm`; unknown vendor fields are rejected rather than guessed.
- DPC uses MMS network bit order (`Off=01`, `On=10`).
- `Check` is exactly two bits: bit 0 synchrocheck and bit 1 interlock-check.
- Normal SBO Select is a Read of `SBO`; SBO enhanced SelectWithValue is exactly one `SBOw` Write.
- `Cancel` is built from the exact retained selected sequence.
- Stale InformationReports are drained before `SBOw` or `Oper`; reports arriving during the confirmed exchange remain available for application-error/termination correlation.
- Missing CommandTermination expires as a control timeout and does not automatically fault an otherwise healthy MMS association.
- **No automatic command retry is performed.**

## C5 interoperability harness

`ariec61850_control_interop_probe` is the controlled live-test harness.

- Read-only discovery is the default.
- A control Write is rejected unless the exact `--arm IEC61850-LAB-CONTROL` token is supplied.
- The operator must provide an explicit object, action, and typed value.
- Supported actions are `operate`, `select-operate`, and `select-cancel`.
- SPC, DPC, integer, unsigned, floating-point, and step-position values are bound conservatively from live type evidence.
- Origin category/identifier, Test, synchrocheck, and interlock-check are explicit.
- Live status is read before and after the action when the IED exposes a usable ST/MX reference.
- Every control Write reference is counted and retained in JSON evidence.
- JSON evidence includes `ctlModel`, CDC, status, Write count/list, report-observation counts, completion, raw ControlError/AddCause, and mapped AddCause.
- The tool prints a Wireshark MMS/TCP filter for JSON-to-PCAP reconciliation.
- Offline `--self-test` verifies the read-only default and arm gate without network access.
- A standalone CMake profile, Windows build helper, and cross-platform CI workflow are provided.

The laboratory procedure and evidence bundle are defined in `docs/CONTROL_INTEROP_RUNBOOK.md`.

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
- integration of C1-C4 sources into the normal `ARIEC61850::core` build graph and CTest targets;
- C5 harness offline safety gate and cross-platform build profile.

## Physical interoperability acceptance

The software path and live harness are ready for controlled interoperability testing, but **physical IED control acceptance is not claimed** until retained evidence exists for:

1. read-only live `ctlModel` and GVAA discovery for a known controllable object with `mmsControlWriteCount=0`;
2. Direct normal `Oper` with exactly one command Write and observed process/status feedback;
3. SBO normal `SBO -> Oper` plus explicit `Cancel` and selection-timeout behavior;
4. Direct enhanced `Oper -> CommandTermination` positive and a safe negative LastApplError/AddCause path;
5. SBO enhanced `SBOw -> Oper -> CommandTermination` plus ownership/contention behavior;
6. association loss during selection and while waiting for enhanced termination;
7. interlock/synchrocheck behavior only where the lab setup can exercise it safely;
8. packet capture and JSON/event evidence proving no automatic command retry.

An individual profile may move from `SOFTWARE_READY_FOR_LAB` to `LAB_INTEROP_PASSED` only when its retained JSON and PCAP evidence agree.

## Safety and claim boundary

- Live inventory and raw `ctlModel` probes are read-only.
- Mutating control requires explicit intent, an exact object root, live type discovery, and the harness authorization gate.
- The implementation and deterministic tests are not an IEC 61850 conformance certificate.
- No hosted CI result is treated as proof of a physical IED command.
- Sampled Values, ESP32 hardware transmission, and BRCB persistence remain outside Smart Control acceptance.
