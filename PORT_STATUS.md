# C++ Port Status

## Current milestone

**Phase 4A — offline DataSet/RCB inventory, InformationReport parsing, and bounded report monitoring are implemented and locally validated.**

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
| MMS reporting mutation smoke/libFuzzer | Implemented; GitHub CI pending |
| Active MMS association/client runtime | Disabled |

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

## Local validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Eleven deterministic Phase 4A reporting groups: passed.
- Full repository CTest: 15/15 passed.
- C# exact OptFlds/report-item shape: passed.
- Local MMS-reporting libFuzzer: 5,000 runs, no crash artifact.

## Remaining acceptance gates

- GitHub GCC, Clang, and Windows MSVC matrix for Phase 4A.
- GitHub ASan/UBSan and ten-corpus libFuzzer workflow.
- Live association lifecycle, timeout, cancellation, RCB enable/reservation, GI, and IED interoperability remain outside this phase.
- Direct/SBO control and file services remain later Phase 4 work.

## Safety boundary

Phase 4A consumes saved bytes and supplied discovery/read evidence only. It does not open
a socket, connect to an IED, enable or reserve an RCB, trigger GI, execute a live read or
write, or perform a control operation.
