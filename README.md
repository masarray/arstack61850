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

### Phase 1A–1C — MMS and GOOSE

- IEC 61850 UTC time and recursive MMS Data/AllData codec.
- Complete GOOSE PDU and Ethernet frame codecs.
- Subscriber TTL, state, sequence, identity, and wraparound supervision.
- Deterministic offline publisher runtime with exponential retransmission scheduling.

### Phase 1D — Sampled Values

- Sampled Values ASDU and multi-ASDU SAV PDU codec.
- Ethernet/VLAN/process-bus frame codec for EtherType `0x88BA`.
- `smpCnt` continuity, wrap, gap, duplicate, out-of-order, and restart tracking.
- Stream identity/configuration supervision, quality-word support, and generic
  `seqOfData` inspection.

### Phase 1E — evidence hardening

- C#-derived synthetic PCAP containing one GOOSE and one Sampled Values frame.
- Read-only `ariec61850_pcap_interop_check` tool with JSON output.
- Exact captured-frame decode/re-encode comparison and canonical PCAP round-trip checks.
- PCAP allocation hardening using validated `snaplen`, included length, and original length.
- Clang AddressSanitizer and UndefinedBehaviorSanitizer workflow.
- LLVM libFuzzer targets and seed corpora for BER, GOOSE, Sampled Values, and PCAP.
- Deterministic mutation smoke tests that run on every compiler, including MSVC.
- Isolated-laboratory checklist, report template, and Windows/Linux runner scripts.

No Phase 1 runtime opens Npcap, raw sockets, or a network interface. Physical IED
interoperability remains pending until real captures are collected and accepted.

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

### Read-only PCAP interoperability check

```powershell
./scripts/run-lab-check.ps1 -Pcap ./captures/device-session.pcap
```

See `LAB_INTEROP_CHECKLIST.md` before using a physical IED and
`MIGRATION_CHECKLIST.md` for the detailed progress ledger.
