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

- [x] MMS UTC time and recursive Data/AllData codec.
- [x] GOOSE wire codec and offline runtime supervision.
- [x] Sampled Values PDU/frame codec, quality, payload, counter, and stream diagnostics.
- [x] Synthetic PCAP equivalence, sanitizer, mutation, and libFuzzer hardening.
- [ ] Real IED or vendor-simulator GOOSE/SV capture accepted.
- [ ] Real-time SV publisher timing-health validation.

## Phase 2 — engineering file formats

### Phase 2A — SCL foundation

- [x] Bounded XML/SCL parser with DTD/entity rejection.
- [x] IED, DataSet, FCDA, GOOSE, SV, report-control, and Communication models.
- [x] Edition detection and DataTypeTemplates resolution.
- [x] DataSet binding, APPID/MAC/VLAN normalization, conflict diagnostics.
- [x] SCL deterministic mutation smoke and libFuzzer corpus.
- [ ] Mutable SCL workspace and standards-aware export.

### Phase 2B — COMTRADE foundation

- [x] CFG identity, channel, frequency, sample-rate, timestamp, file-type, and time-multiplier parsing.
- [x] ASCII DAT analog/digital decoding.
- [x] BINARY, BINARY32, and FLOAT32 DAT decoding.
- [x] Analog `a * raw + b` scaling and packed digital status words.
- [x] Multi-rate timestamp fallback and bounded record layout arithmetic.
- [x] Default voltage/current phase channel mapping.
- [x] SCL Sampled Values to COMTRADE channel mapping with confidence diagnostics.
- [x] C#-derived ASCII and binary fixtures.
- [x] Read-only COMTRADE human/JSON inspection CLI.
- [x] COMTRADE mutation smoke and LLVM libFuzzer corpus.

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

- [x] GCC and Clang C++20 warnings-as-errors local validation through Phase 2B.
- [x] Clang ASan/UBSan local COMTRADE regression pass.
- [x] C#-derived COMTRADE ASCII and binary fixtures.
- [x] Full stacked-branch GCC/Clang/MSVC and security workflows through Phase 2B.
- [ ] Controlled physical-lab process-bus capture evidence.
- [ ] C# and C++ executable-oracle comparison in one CI job.

## Safety gate

SCL and COMTRADE tooling is read-only. Process-bus interoperability tooling operates on
saved PCAP files. Active Npcap transmission, real-time SV scheduling, MMS write/control,
and IED operation remain outside the enabled runtime surface until separate physical-lab,
timing, cancellation, and explicit transmit authorization gates are complete.
