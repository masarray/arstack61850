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

### Phase 3A — TPKT and COTP foundation

- [x] RFC 1006 TPKT encode/decode with exact length validation.
- [x] Incremental TPKT stream decoder for fragmented/coalesced byte streams.
- [x] COTP CR, CC, DT, DR, and ER TPDU decoding.
- [x] C#-compatible default Connection Request vector.
- [x] TPDU-size negotiation and C1/C2 TSAP selector mirroring.
- [x] Data TPDU segmentation with EOT and TPDU-number handling.
- [x] Bounded COTP reassembly by bytes, fragments, and empty non-final fragments.
- [x] Deterministic golden-vector and abuse-path regression tests.
- [x] OSI transport mutation smoke and LLVM libFuzzer corpus.

### Phase 3B — session, presentation, and ACSE

- [x] ISO Session Connect/Accept SPDU codec with bounded parameter parsing.
- [x] Session data-transfer profile with optional Give Tokens prefix.
- [x] Presentation CP/CPA mode, selector, context-definition, and result codecs.
- [x] ACSE and MMS presentation-context negotiation with BER transfer syntax.
- [x] Fully-encoded-data PDV and P-DATA context routing.
- [x] Structured ACSE AARQ/AARE application-context and identity envelopes.
- [x] EXTERNAL user-information handling with opaque MMS Initiate payload boundary.
- [x] C#-compatible 184-byte request and deterministic 138-byte response vectors.
- [x] Session-parameter and presentation-context mirroring in association responses.
- [x] OSI association mutation smoke and LLVM libFuzzer corpus.

### Phase 3C — MMS envelopes and services

- [x] MMS Initiate request/response with C# byte-exact request parity.
- [x] Confirmed request, response, and structured error envelopes.
- [x] Top-level PDU classification and invoke-ID extraction from MMS or P-DATA.
- [x] Bounded per-invoke and unmatched-result routing.
- [x] VMD, domain, and association-specific ObjectName codecs.
- [x] GetNameList request/response and continuation handling.
- [x] GetVariableAccessAttributes and bounded recursive TypeSpecification codecs.
- [x] Multi-variable Read with direct data and per-result access failures.
- [x] Multi-variable Write with per-variable success/failure results.
- [x] MMS-services mutation smoke and LLVM libFuzzer corpus.

## Phase 4 — reporting, control, and file service

### Phase 4A — offline reporting foundation

- [x] DataSet and BRCB/URCB inventory from supplied discovery evidence.
- [x] GetNamedVariableListAttributes request/response and member normalization.
- [x] InformationReport unconfirmed-PDU codec with per-item failures.
- [x] OptFlds-driven report header, inclusion, value, data-reference, and reason mapping.
- [x] C# exact report-shape compatibility regression.
- [x] RCB attribute read-plan and typed state/availability mapping.
- [x] Sequence, configuration, DataSet, overflow, and segmentation continuity tracking.
- [x] Bounded offline report stream/frame retention.
- [x] MMS-reporting mutation smoke and LLVM libFuzzer corpus.

### Later Phase 4 work

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
- [x] Full stacked-branch GCC/Clang/MSVC and security workflows through Phase 3A.
- [x] Full stacked-branch GCC/Clang/MSVC and security workflows through Phase 3B.
- [x] Full stacked-branch GCC/Clang/MSVC and security workflows through Phase 3C.
- [x] PR #6 through PR #10 squash-merged sequentially to `main`.
- [x] Full main-based branch GCC/Clang/MSVC and security workflows through Phase 4A.
- [ ] Controlled physical-lab process-bus capture evidence.
- [ ] C# and C++ executable-oracle comparison in one CI job.

## Safety gate

SCL and COMTRADE tooling is read-only. Process-bus interoperability tooling operates on
saved PCAP files. Active Npcap transmission, real-time SV scheduling, MMS write/control,
and IED operation remain outside the enabled runtime surface until separate physical-lab,
timing, cancellation, and explicit transmit authorization gates are complete.
