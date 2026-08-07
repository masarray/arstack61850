# ARIEC61850 C++ Migration Plan

The migration follows deterministic protocol foundations first, then engineering formats,
association/runtime behavior, live read-only interoperability, and finally active services and
applications. The C# repository remains the behavioral oracle until same-target laboratory
comparison is accepted.

## Completed foundation

- Phase 0: C++20/CMake, strict warnings, BER, Ethernet/VLAN, PCAP.
- Phase 1: MMS Data/UTC, GOOSE, Sampled Values, evidence and fuzzing.
- Phase 2: read-only SCL and COMTRADE.
- Phase 3: TPKT/COTP, Session, Presentation, ACSE, MMS Initiate and confirmed services.
- Phase 4A: DataSet/RCB inventory, InformationReport decoding, continuity monitoring.
- Phase 4B: transport-injected association and persistent report-subscription runtime.

## Current delivery — Phase 4C and 4C.1

### Phase 4C

- built-in non-blocking Windows/Linux TCP transport;
- deadline/cancellation-aware connect, send, and receive;
- bounded live domain, variable, DataSet, type, and RCB discovery;
- strict read-only service allowlist.

### Phase 4C.1

- C#-compatible `live-ied-model-v1` hierarchy;
- IED/LD/LN/DO/DA references, FC, type, DataSet, and RCB evidence;
- conservative identity and CDC inference;
- deterministic manifest/fingerprint;
- C#↔C++ parity checker;
- repeated physical read-only interoperability runner and evidence format.

### Acceptance work still required

- GCC/Clang/MSVC and Security/Evidence workflow run on PR #14;
- full branch CTest;
- reachable IED/vendor-simulator run;
- primary-vendor ten-cycle and timeout/reconnect evidence;
- same-target C# and C++ parity review;
- large-model pagination evidence when available.

## Next implementation priority after Phase 4C.1 acceptance

### Phase 4D — guarded control foundation

- Direct Operate and Select-Before-Operate state machines;
- normal and enhanced security;
- select, operate, cancel, ownership, timeout, and command termination;
- LastApplError and AddCause;
- default-deny authorization, operator confirmation, and full audit trail;
- no automatic command retry.

Phase 4D must not begin physical command testing until Phase 4C.1 transport/model identity and
same-target read-only parity are accepted.

### Later work

- BRCB EntryID resume, replay, purge, and overflow recovery;
- dynamic DataSet lifecycle;
- MMS file and fault-record transfer;
- deterministic IED simulator/read-only server;
- CLI parity, Windows Npcap abstraction, and native applications.
