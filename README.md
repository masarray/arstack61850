# ARIEC61850 C++ Port

Incremental C++20 migration of the ARIEC61850 C# protocol stack. The C# implementation
remains the behavioral oracle until cross-language comparison and isolated-laboratory
interoperability are complete.

## Implemented

### Phase 0 — portable foundation

- CMake-based C++20 static library.
- BER TLV and primitive codecs.
- MAC, VLAN, Ethernet, IEC 61850 process-bus, and classic PCAP codecs.
- GOOSE retransmission schedule.

### Phase 1A–1E — deterministic process-bus core

- IEC 61850 UTC time and recursive MMS Data/AllData codec.
- Complete GOOSE PDU/frame codecs and offline publisher/subscriber supervision.
- Sampled Values ASDU, multi-ASDU PDU, Ethernet frame, payload, quality, counter, and
  stream supervision.
- C#-derived synthetic PCAP equivalence evidence.
- Sanitizer, deterministic mutation-smoke, and LLVM libFuzzer coverage.
- Receive-only isolated-laboratory tooling.

### Phase 2A — SCL foundation

- Bounded, dependency-free XML/SCL reader.
- IED, DataSet, FCDA, GOOSE, Sampled Values, report-control, and communication mapping.
- SCL edition detection, type-template resolution, DataSet binding, diagnostics, and
  SCL/XML fuzzing.

### Phase 2B — COMTRADE foundation

- CFG parsing for IEEE C37.111-style 1991/1999/2013 records.
- ASCII, BINARY, BINARY32, and FLOAT32 DAT readers.
- Analog engineering scaling (`a * raw + b`) and packed digital status decoding.
- Timestamp multiplier and multi-rate fallback scheduling.
- Default `Va/Vb/Vc/Vn/Ia/Ib/Ic/In` channel mapping.
- Deterministic SCL Sampled Values to COMTRADE channel binding.
- Read-only `ariec61850_comtrade_inspect` human/JSON CLI.
- C#-derived ASCII and binary fixtures, mutation smoke, sanitizer, and libFuzzer corpus.

### Phase 3A — MMS transport foundation

- RFC 1006 TPKT frame encode/decode with strict version, reserved-octet, and length validation.
- Incremental TPKT stream framing for fragmented and coalesced TCP byte streams.
- COTP Connection Request, Connection Confirm, Data, Disconnect Request, and Error TPDU decoding.
- Default C#-compatible CR vector, TPDU-size negotiation, and TSAP selector mirroring.
- COTP Data segmentation by negotiated TPDU size and bounded EOT reassembly.
- Fragment-count, byte-count, and empty non-final fragment abuse guards.
- C#-derived golden vectors, deterministic mutation smoke, sanitizer, and libFuzzer coverage.

### Phase 3B — MMS association codecs

- Bounded ISO Session Connect/Accept SPDU and data-transfer profile codecs.
- Presentation CP/CPA context definitions, negotiation results, selectors, and PDV routing.
- ACSE and MMS context negotiation using BER transfer syntax.
- Structured ACSE AARQ/AARE application context, AP-title, AE-qualifier, result, and diagnostic parsing.
- EXTERNAL user-information handling with MMS Initiate retained as an opaque Phase 3C payload.
- Deterministic association-response construction with session and context mirroring.
- Byte-exact C# request/response golden vectors, mutation smoke, sanitizer, and libFuzzer coverage.

### Phase 3C — MMS Initiate and core confirmed services

- Byte-compatible MMS Initiate request/response codecs.
- Confirmed request, response, and structured error envelopes with invoke-ID extraction.
- Bounded per-invoke result routing for offline client/session orchestration.
- VMD, domain, and association-specific object-name codecs.
- GetNameList and GetVariableAccessAttributes with recursive type specifications.
- Multi-variable Read and Write codecs with per-access success/failure results.
- Raw MMS and Presentation P-DATA input/output paths.
- Golden vectors, deterministic mutation smoke, sanitizer, and dedicated libFuzzer coverage.

No enabled runtime opens TCP sockets, Npcap, raw sockets, or a network interface. Physical IED
interoperability and all active transmission remain gated.

## Build

### Windows — Visual Studio

```powershell
cmake -S . -B build -A x64 -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Linux — GCC or Clang

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Sanitizers

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitizers \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DARIEC61850_ENABLE_SANITIZERS=ON
cmake --build build-sanitizers --parallel
ctest --test-dir build-sanitizers --output-on-failure
```

## Read-only tools

### Inspect a COMTRADE record

```powershell
.\build\Release\ariec61850_comtrade_inspect.exe .\record.cfg --json
```

### Analyze a saved process-bus PCAP

```powershell
.\scripts\run-lab-check.ps1 -Pcap .\captures\device-session.pcap
```

See `LAB_INTEROP_CHECKLIST.md` before using a physical IED and
`MIGRATION_CHECKLIST.md` for the detailed progress ledger.
