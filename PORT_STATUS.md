# C++ Port Status

## Current milestone

**Phase 4B — transport-injected association lifecycle and persistent report-subscription runtime are implemented and validated on the published stacked branch.**

## Delivered modules

| Area | Status |
|---|---|
| Phase 0 and Phase 1 process-bus foundation | Merged to `main` |
| SCL and COMTRADE engineering formats | Merged to `main` |
| TPKT/COTP and Session/Presentation/ACSE | Merged to `main` |
| MMS Initiate and confirmed services | Merged to `main` |
| DataSet and RCB discovery inventory | Implemented |
| GetNamedVariableListAttributes directory codec | Implemented |
| InformationReport codec and exact frame mapper | Implemented |
| RCB state and availability evidence mapper | Implemented |
| Sequence and segmentation continuity tracker | Implemented |
| Bounded offline report monitor | Implemented |
| MMS reporting mutation smoke/libFuzzer | Complete |
| Transport-injected MMS association/client runtime | Implemented |
| Persistent RCB subscription runtime | Implemented |
| Built-in TCP socket transport | Not included |

## Phase 4A behavior

The offline reporting layer:

- groups supplied named-variable and named-variable-list discovery evidence into DataSet, BRCB, and URCB inventory;
- encodes and decodes GetNamedVariableListAttributes service tag 12 and normalizes DataSet member references;
- strictly decodes Unconfirmed-PDU InformationReport access results;
- follows IEC 61850 OptFlds ordering for report headers, inclusion bits, values, data references, and reasons;
- preserves per-value DataAccessError results;
- maps supplied RCB Read responses into typed state with conservative availability confidence;
- detects sequence gaps, duplicates, wraps, resets, configuration/DataSet changes, buffer overflow, and segmentation discontinuity; and
- retains only configured numbers of streams and recent frames.

## Phase 4B behavior

- TPKT/COTP, Session, Presentation, ACSE, and MMS Initiate are orchestrated as one bounded lifecycle.
- Confirmed service responses are routed by invoke ID while InformationReports remain deliverable.
- Deadline and cancellation failures produce explicit fault/cancel events.
- A selected RCB is re-probed before writes and occupied RCBs are not taken over.
- URCB reservation, optional DatSet/TrgOps/OptFlds writes, RptEna, GI, polling, stop, and cleanup retry are stateful.
- Lost associations preserve cleanup-required evidence instead of pretending the RCB was disabled.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Eleven deterministic Phase 4A reporting groups: passed.
- Full repository CTest: 16/16 passed through Phase 4B.
- C# exact OptFlds/report-item shape: passed.
- Local MMS-reporting libFuzzer: 5,000 runs, no crash artifact.
- GitHub GCC, Clang, and Windows MSVC matrix: passed.
- GitHub ASan/UBSan: passed.
- Ten-corpus libFuzzer workflow including MMS reporting: passed with no crash artifact.

## Remaining acceptance gates

- Built-in platform TCP transport and physical IED interoperability remain separate acceptance work.
- BRCB EntryID resume, purge/buffer-overflow recovery, dynamic DataSet lifecycle, Direct/SBO control, and file services remain later work.

## Safety boundary

Phase 4B can perform association and RCB Read/Write operations only through an explicitly
injected `MmsByteTransport`. The core does not provide or auto-enable a socket backend. It
never takes over an occupied RCB and only cleans up state touched by the current runtime.
Control operations, dynamic DataSet mutation, and file services are not exposed.
