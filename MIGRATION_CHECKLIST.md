# ARIEC61850 C++ Migration Checklist

Status legend: `[x]` implemented and regression-tested, `[~]` implemented but awaiting
external/CI or physical-lab evidence, `[ ]` not started. The C# repository remains the
behavioral oracle until laboratory interoperability is complete.

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
- [x] Allocation-bounded SV publisher runtime with caller-owned Ethernet buffer, successful-TX counter progression, and no-catch-up pacing.
- [x] Platform-neutral raw-Ethernet/clock HAL plus ESP-IDF `esp_eth_transmit()` adapter.
- [~] ESP32-P4 ESP-IDF cross-compile acceptance for the first SV transmit slice.
- [ ] Real IED or vendor-simulator GOOSE/SV capture accepted.
- [~] Real-time SV publisher timing-health validation: deterministic host/CI pacing implemented; physical ESP32-P4 evidence pending.
- [ ] Exception-free embedded codec build; the first ESP-IDF trial temporarily enables C++ exceptions for shared legacy validation/convenience APIs while the publisher hot path remains `noexcept`.

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

## Phase 4 — reporting, live discovery, control, and file service

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

### Phase 4B — association and subscription runtime

- [x] Transport-injected TPKT/COTP/Session/Presentation/ACSE/MMS lifecycle.
- [x] Negotiated TPDU and MMS association limits.
- [x] Deadline and `std::stop_token` cancellation paths.
- [x] Bounded invoke-ID allocation and confirmed-result routing.
- [x] InformationReport delivery while confirmed requests are pending.
- [x] RCB re-probe and occupied-control takeover protection.
- [x] URCB reservation, RptEna enable/disable, optional GI, and touched-state cleanup.
- [x] Persistent report polling and Phase 4A monitor ingestion.
- [x] Cleanup-required evidence and retry path after association loss.
- [ ] BRCB EntryID resume, purge decisioning, and buffer-overflow recovery.

### Phase 4C — built-in TCP and live read-only discovery

- [~] Non-blocking Windows Winsock and POSIX TCP `MmsByteTransport` implementation.
- [~] IPv4/IPv6 resolution, TCP_NODELAY, keepalive, partial-send, and peer-close handling.
- [~] Deadline and cancellation-aware connect/send/receive readiness waits.
- [~] Bounded live GetNameList pagination for domains, variables, and DataSets.
- [~] Optional GetVariableAccessAttributes type probes.
- [~] Optional GetNamedVariableListAttributes DataSet directory reads.
- [~] Optional read-only RCB attribute inventory/state probes.
- [~] Read-only `ariec61850_live_discover` human/JSON CLI.
- [~] Discovery service allowlist regression proving no MMS Write request is emitted.

### Phase 4C.1 — live-model parity and physical read-only interoperability

- [x] C#-compatible `live-ied-model-v1` output schema.
- [x] Logical Device / Logical Node / Data Object / Data Attribute hierarchy.
- [x] IEC 61850 reference and functional-constraint normalization.
- [x] Direct and nested TypeSpecification mapping to discovered attributes.
- [x] Conservative IED identity and logical-device alias inference.
- [x] CDC inference with explicit confidence.
- [x] DataSet and BRCB/URCB evidence in the live model.
- [x] Deterministic canonical manifest and fingerprint.
- [x] C#↔C++ parity comparison script.
- [x] Reconnect-cycle physical read-only evidence runner for Windows/Linux.
- [x] Automated stability, warning-policy, coverage, and optional parity gates.
- [x] Bounded GO/SV/SG/LG deep reader using exact live MMS NameList variables only.
- [x] Machine-readable control-block runtime attribute/value/status overlay excluded from structural parity fingerprint.
- [x] GCC/Clang strict validation, sanitizer/libFuzzer smoke, and Windows MSVC matrix accepted on the feature branch.
- [x] Controlled physical IED read-only evidence accepted: OCR7SR12 RCB contention probe and SGCB 5/5 deep read.
- [x] Integrated `live_discover --control-block-values` physical OCR7SR12 run accepted: 1/1 SGCB complete, 5/5 attributes read, diagnostics=0, pending-value warning cleared.
- [x] C# and C++ same-IED OCR7SR12 structural/type/runtime comparison accepted with zero blocking findings.
- [x] Primary-vendor OCR7SR12 ten-cycle acceptance: 10/10 stable discovery cycles, full 286/286 RCB and control-block gates, 3/3 StableProceed contention cycles, and 13/13 fresh associations.
- [x] Controlled OCR7SR12 timeout/recovery evidence accepted: healthy baseline, post-association response withholding, client request timeout observed, fresh direct recovery, and identical `934b555dff76a46f` structural fingerprint before/after recovery.
- [x] Physical GetNameList pagination accepted on OCR7SR12: 9 queries, 4 paginated queries, 88 continuation requests; largest sequence 48 pages / 4,758 names / 47 continuations with final `moreFollows=false`.

### Later Phase 4 work

- [ ] Direct and SBO control state machines.
- [ ] Enhanced-security termination/error handling.
- [ ] MMS file and fault-record transfer.

## Phase 5 — simulator and applications

- [ ] Deterministic IED simulation and read-only MMS server.
- [ ] CLI parity and Windows Npcap abstraction.
- [ ] Native Engineering Workbench, simulator, discovery, and SV publisher UI.

## Safety gate

SCL, COMTRADE, PCAP, and the Phase 4C/4C.1 live discovery surface are read-only. Active RCB
mutation, control, dynamic DataSet mutation, file transfer, Npcap transmission, and real-time SV
scheduling require separate explicit APIs, authorization, and physical-laboratory acceptance.
