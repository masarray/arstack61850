# ARIEC61850 C# to C++ Migration Plan

## Decision

A full C++ conversion is feasible, but this repository is approximately 66,000 lines of
C# and contains protocol codecs, asynchronous network state machines, active IED control,
raw Ethernet publishing, simulation, and several WPF applications. It must be migrated as
a compatibility program rather than by mechanical source translation.

## Target architecture

```text
ariec61850_core       BER, MMS values, Ethernet, GOOSE, SV, SCL, PCAP
ariec61850_transport  TCP/TPKT/COTP, Npcap/libpcap abstraction, timers
ariec61850_client     ACSE/MMS association, discovery, read/write, file services
ariec61850_reporting  DataSet/RCB/report receive state machines
ariec61850_control    typed control planning, SBO/direct execution, termination
ariec61850_simulation deterministic IED model and MMS server
ariec61850_cli        command-line engineering tools
ariec61850_desktop    new native UI; do not attempt a line-for-line WPF translation
```

## Recommended implementation choices

- Language: C++20 now, with a later option to move selected APIs to C++23.
- Build: CMake with Visual Studio and Ninja generators.
- Ownership: RAII, value types, `std::unique_ptr`, `std::shared_ptr` only where ownership
  is genuinely shared.
- Byte handling: `std::span<const uint8_t>` at boundaries and owning `std::vector<uint8_t>`
  in durable models.
- Errors: typed exceptions for malformed offline data; explicit result/error objects for
  network and control workflows.
- Async: one association receive loop with explicit routing, matching the current design.
- UI: rebuild the desktop shell after the core is stable. WPF XAML is not portable to C++.
- Tests: keep C# as the oracle and generate golden vectors for every codec and state machine.

## Order of work

### Phase 0 — foundation (included in this package)

- CMake, compiler warnings, CTest.
- BER, Ethernet/VLAN, process-bus framing, PCAP.
- GOOSE retransmission timing.

### Phase 1 — deterministic process-bus codecs

- IEC 61850 UTC time and MMS Data values used by GOOSE/SV.
- GOOSE PDU parser/builder.
- Sampled Values ASDU, payload, frame parser/builder.
- Process-bus stream diagnostics and sequence supervision.
- PCAP golden vectors against existing C# output.

### Phase 2 — engineering file formats

- SCL models/parser/workspace/export.
- COMTRADE reader and channel mapping.
- Expected-vs-observed engineering profiles.

### Phase 3 — MMS transport and data model

- TPKT and COTP.
- Presentation and ACSE association.
- MMS PDU envelope, data codec, read/write, name lists, variable attributes.
- Single receive pump and invoke-ID routing.

### Phase 4 — reporting, control, and file service

- DataSet and RCB inventory.
- Report subscriptions, GI, persistent monitoring, value projection.
- Guarded IEC 61850 control including Direct/SBO normal/enhanced security.
- CommandTermination, LastApplError, Cancel, timeout, association-loss cleanup.
- MMS file directory and fault-record transfer.

### Phase 5 — simulator and applications

- Deterministic simulation runtime and read-only MMS server.
- CLI command parity.
- Native desktop applications using a shared presentation layer.
- Npcap transport on Windows; keep raw Ethernet optional.

## Acceptance gates

A module is not considered converted merely because it compiles. It must pass:

- C++ unit tests corresponding to the original C# tests.
- C# versus C++ byte-for-byte golden vectors for encoders.
- Parse-equivalence tests for captured PCAP/SCL/COMTRADE samples.
- malformed/truncated-input tests and fuzzing for decoders.
- cancellation, timeout, disconnect, and cleanup tests for state machines.
- isolated-lab interoperability before enabling active control or publishing.

## Primary risks

1. WPF is a rewrite, not a direct port.
2. Async socket cancellation and the single MMS receive pump are more error-prone in C++.
3. Active control requires exact preservation of sequencing and cleanup semantics.
4. SCL export contains rich object graphs and should not be ported before core value models.
5. A one-shot automated translator would create superficially similar code without
   protocol-level confidence.
