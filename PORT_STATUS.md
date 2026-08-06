# C++ Port Status

## Current milestone

**Phase 4C — built-in non-blocking TCP transport and bounded live read-only discovery are implemented for review and CI validation.**

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
| Built-in Windows/Linux TCP socket transport | Implemented in Phase 4C |
| Live read-only name/type/DataSet/RCB discovery | Implemented in Phase 4C |
| Physical IED interoperability acceptance | Pending |

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

## Phase 4C behavior

- `TcpMmsByteTransport` uses non-blocking Winsock/POSIX sockets with bounded readiness polling.
- Connect, send, and receive honor absolute deadlines and `std::stop_token` cancellation.
- IPv4/IPv6 resolution, TCP_NODELAY, keepalive, partial sends, arbitrary receive chunking, and peer-close detection are built in.
- Live discovery performs bounded GetNameList pagination for domains, variables, and named-variable lists.
- Optional type, DataSet-directory, and RCB-state probes use only GetVariableAccessAttributes, GetNamedVariableListAttributes, and Read.
- The live discovery result retains partial evidence and diagnostics when optional probes fail.
- `ariec61850_live_discover` provides human and JSON summaries.

## Validation

- Phase 4B baseline: GCC, Clang, MSVC, ASan/UBSan, 16/16 CTest, and reporting fuzz evidence passed.
- Phase 4C adds a POSIX loopback TCP round-trip and cross-platform cancellation smoke test.
- Phase 4C scripted discovery covers domain/variable/DataSet inventory, type evidence, DataSet directory, RCB state, and pagination abuse rejection.
- Every scripted discovery request is decoded and checked against the read-only service allowlist; MMS Write is rejected.
- Full GitHub GCC/Clang/MSVC and security workflow evidence is required before Phase 4C is accepted.

## Remaining acceptance gates

- Controlled physical IED association and discovery evidence across multiple vendors.
- Long-model pagination, slow-response, disconnect, reconnect, and soak testing.
- BRCB EntryID resume, purge/buffer-overflow recovery, and dynamic DataSet lifecycle.
- Direct/SBO control, enhanced-security termination, and MMS file/fault-record transfer.

## Safety boundary

The live discovery API and CLI are read-only and do not construct MMS Write requests. The TCP
transport is general-purpose but performs no operation by itself; applications must explicitly
select and authorize any state-changing runtime. Control services, dynamic DataSet mutation,
and file services remain outside the Phase 4C live discovery surface.
