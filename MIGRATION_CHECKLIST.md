# ARIEC61850 C++ Migration Checklist

Status legend: `[x]` implemented and regression-tested, `[~]` implemented but awaiting
external/CI evidence, `[ ]` not started. The C# repository remains the behavioral oracle
until laboratory interoperability is complete.

## Phase 0 — portable deterministic foundation

- [x] C++20 static library and CMake build.
- [x] Strict compiler warnings promoted to errors.
- [x] GitHub Actions matrix: GCC, Clang, and Windows MSVC.
- [x] BER primitives, Ethernet/VLAN/process-bus framing, and classic PCAP.

## Phase 1 — deterministic process-bus core

### Phase 1A — MMS data foundation

- [x] IEC 61850 UTC time.
- [x] Recursive MMS Data model and AllData BER codec.

### Phase 1B — GOOSE wire codec

- [x] GOOSE PDU and Ethernet/VLAN/process-bus frame codecs.
- [x] Dataset-count validation and C#-derived golden vectors.

### Phase 1C — GOOSE runtime

- [x] Subscriber TTL/state/sequence/identity supervision.
- [x] Offline publisher session and retransmission runtime.

### Phase 1D — Sampled Values

- [x] ASDU and multi-ASDU SAV PDU codec.
- [x] Ethernet frame codec for `0x88BA`.
- [x] Counter, stream, quality, and generic payload diagnostics.
- [x] C#-derived PDU and complete Ethernet-frame golden vectors.

### Phase 1E — evidence hardening

- [x] Synthetic C#-derived GOOSE and SV PCAP corpus.
- [x] Read-only PCAP equivalence analyzer and JSON CLI.
- [x] Exact process-bus decode/re-encode comparison.
- [x] Canonical PCAP packet/timestamp round-trip comparison.
- [x] PCAP `snaplen` and packet-length allocation bounds.
- [x] Explicit regression test for malicious PCAP allocation lengths.
- [x] CMake sanitizer option and local ASan/UBSan pass.
- [x] Deterministic BER/GOOSE/SV/PCAP mutation smoke suite.
- [x] LLVM libFuzzer targets and seed corpora.
- [~] GitHub ASan/UBSan and libFuzzer workflow; awaiting branch CI.
- [x] Isolated-lab checklist, report template, and read-only runner scripts.
- [ ] Real IED or vendor-simulator GOOSE capture accepted.
- [ ] Real IED or vendor-simulator Sampled Values capture accepted.
- [ ] C# and C++ executable-oracle comparison in one CI job.
- [ ] Real-time SV publisher timing-health validation.

**Phase 1 status: deterministic core and automated synthetic evidence are complete;
physical interoperability and active-transport validation remain.**

## Phase 2 — engineering file formats

- [ ] SCL object model, parser, validation, workspace, and export.
- [ ] COMTRADE CFG/DAT reader, scaling, and mapping.

## Phase 3 — MMS transport and association

- [ ] TPKT and COTP.
- [ ] Presentation and ACSE.
- [ ] MMS envelopes, invoke routing, name lists, attributes, read, and write.

## Phase 4 — reporting, control, and file service

- [ ] DataSet/RCB inventory and report monitoring.
- [ ] Direct and SBO control state machines.
- [ ] Enhanced-security termination/error handling.
- [ ] MMS file and fault-record transfer.

## Phase 5 — simulator and applications

- [ ] Deterministic IED simulation and read-only MMS server.
- [ ] CLI parity and Windows Npcap abstraction.
- [ ] Native Engineering Workbench, simulator, discovery, and SV publisher UI.

## Current acceptance evidence

- [x] GCC C++20 warnings-as-errors build through Phase 1E local subset.
- [x] Clang C++20 warnings-as-errors build through Phase 1E local subset.
- [x] Clang ASan/UBSan local regression pass.
- [x] Synthetic GOOSE/SV PCAP exact equivalence.
- [x] Deterministic mutation smoke across more than 500 mutated inputs.
- [~] Full GCC/Clang/MSVC and security workflow on GitHub; awaiting branch CI.
- [ ] Controlled physical-lab capture evidence.

## Safety gate

All interoperability tooling is receive-only and operates on saved PCAP files. Active
Npcap transmission, real-time SV scheduling, MMS write/control, and IED operation
remain outside the enabled runtime surface until physical-lab evidence, timing,
cancellation, and explicit transmit authorization are complete.
