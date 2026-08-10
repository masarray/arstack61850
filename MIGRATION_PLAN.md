# ARIEC61850 C++ Migration Plan

The migration follows deterministic protocol foundations first, then engineering formats,
association/runtime behavior, live read-only interoperability, bounded active-service semantics,
and finally physical interoperability plus application parity. The C# repository remains the
behavioral oracle until same-target laboratory comparison is accepted.

## Completed foundation

- Phase 0: C++20/CMake, strict warnings, BER, Ethernet/VLAN, PCAP.
- Phase 1: MMS Data/UTC, GOOSE, Sampled Values, evidence and fuzzing.
- Phase 2: read-only SCL and COMTRADE foundations.
- Phase 3: TPKT/COTP, Session, Presentation, ACSE, MMS Initiate and confirmed services.
- Phase 4A: DataSet/RCB inventory, InformationReport decoding, continuity monitoring.
- Phase 4B: transport-injected association and persistent report-subscription runtime.
- Phase 4C/4C.1: built-in Windows/Linux TCP, live read-only discovery/model projection,
  C#↔C++ structural/type parity, repeated physical read-only interoperability, timeout/recovery,
  RCB/control-block deep-read evidence, and large-model GetNameList pagination.

## Completed software tranche — Phase 4D-R

BRCB operational semantics are implemented and regression-tested:

- bounded retained report history with an independent delivery cursor;
- EntryID replay/resume and rewind-to-oldest;
- PurgeBuf and replay-gap/overflow handling;
- association-aware reservation/Owner/ResvTms lifecycle;
- association identity propagation through MMS writes;
- TCP/reset/COTP-DR association-loss cleanup;
- replay-capable recovery image v2 with legacy-v1 restore.

Physical BRCB raw-partition geometry, latency, endurance budgeting and controlled power-loss
acceptance remain a separate ESP32-P4 NVM evidence gate.

## Current delivery — Phase 4D-C5 guarded control interoperability

### C1 — safety state machine

- `ctlModel` 0..4 parity with the C# oracle;
- Direct normal, SBO normal, Direct enhanced and SBO enhanced lifecycle semantics;
- association-aware ownership and default-deny authorization;
- immutable selected sequence across ctlVal/origin/ctlNum/T/operTm/Test/Check;
- selection timeout, association-loss cleanup and explicit Cancel;
- ctlNum 1..255 wrap with zero excluded;
- no automatic command retry.

### C2 — exact MMS control structures

- live TypeSpecification-driven `Oper`, `SBOw` and `Cancel` construction;
- conservative SPC/DPC/integer/unsigned/floating/step-position binding;
- exact origin, ctlNum, T, optional operTm, Test and two-bit Check fields;
- DPC network-bit-order parity and golden MMS Data vectors;
- unknown vendor-specific fields are rejected instead of guessed.

### C3 — enhanced-security completion and diagnostics

- correlated CommandTermination handling;
- LastApplError decoding with standard ControlError and AddCause 0..27 mapping;
- raw unknown-code preservation;
- exact controlled-object correlation;
- ordinary ST/MX process reports cannot complete an enhanced command.

### C4 — live guarded control session

- live ctlModel Read and exact Oper/SBOw/Cancel GVAA discovery;
- domain variable discovery and CF sboTimeout/operTimeout reads;
- Direct Operate, SBO Read -> Oper, SBOw -> Oper and explicit Cancel orchestration;
- asynchronous application-error grace window;
- enhanced commands remain pending until correlated CommandTermination;
- missing termination is a control timeout, not an automatic association fault;
- C1-C4 are integrated into `ARIEC61850::core` and the normal GCC/Clang/MSVC CTest matrix.

### C5 — controlled live interoperability harness

- `ariec61850_control_interop_probe` is read-only by default;
- any live control Write requires the exact `--arm IEC61850-LAB-CONTROL` token plus an explicit
  object, action and value;
- supported deliberate lab actions are `operate`, `select-operate`, and `select-cancel`;
- status-before/status-after reads are captured when a suitable ST/MX object exists;
- all control Write references are counted for deterministic no-retry evidence;
- optional JSON evidence records ctlModel, CDC, status, Write list/count, report observations,
  completion, raw ControlError/AddCause and mapped AddCause;
- Wireshark filter guidance lets JSON evidence be reconciled with PCAP/PCAPNG;
- a standalone CMake profile, Windows PowerShell build helper and GCC/Clang/MSVC CI artifacts
  make the harness directly usable in a laboratory.

Current software state:

`SOFTWARE_READY_FOR_LAB`

Physical/simulator evidence is still required before changing a tested profile to:

`LAB_INTEROP_PASSED`

The required procedure is documented in `docs/CONTROL_INTEROP_RUNBOOK.md`.

## C5 laboratory acceptance order

1. Read-only live ctlModel/GVAA/status discovery with zero control Writes.
2. Direct normal: exactly one Oper Write plus status/process feedback.
3. SBO normal: SBO Read -> exactly one Oper Write; separately Select -> Cancel.
4. Direct enhanced: exactly one Oper Write -> correlated CommandTermination.
5. SBO enhanced: one SBOw Write -> one Oper Write -> correlated CommandTermination.
6. Safe negative LastApplError/AddCause case where the simulator/lab IED can provide it.
7. Association loss during SBO selection and enhanced-termination wait.
8. Reconcile JSON Write counts/references with retained PCAP and prove no automatic retry.

Physical command testing must remain on an isolated/non-operational laboratory IED or simulator.
Hosted CI does not issue a physical command and is not IEC 61850 conformance certification.

## Next implementation priorities after accepted C5 evidence

1. MMS file and fault-record transfer parity.
2. Dynamic DataSet define/delete and the remaining dynamic RCB lifecycle parity.
3. Mutable SCL workspace/export and live-discovery-to-SCL reconstruction parity.
4. Deterministic IED simulator/server integration needed for repeatable multi-client regression.
5. CLI/application and native engineering-tool parity where product scope requires it.

Sampled Values / ESP32-P4 process-bus development is intentionally maintained separately from
this MMS-control tranche.
