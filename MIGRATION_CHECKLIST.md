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
- [x] BRCB retained-history replay, EntryID resume/rewind, PurgeBuf, replay-gap/overflow recovery, association ownership, ResvTms/Owner lifecycle, and recovery image v2 with legacy v1 restore.

### Phase 4C — built-in TCP and live read-only discovery

- [x] Non-blocking Windows Winsock and POSIX TCP `MmsByteTransport` implementation.
- [x] IPv4/IPv6 resolution, TCP_NODELAY, keepalive, partial-send, and peer-close handling.
- [x] Deadline and cancellation-aware connect/send/receive readiness waits.
- [x] Bounded live GetNameList pagination for domains, variables, and DataSets.
- [x] Optional GetVariableAccessAttributes type probes.
- [x] Optional GetNamedVariableListAttributes DataSet directory reads.
- [x] Optional read-only RCB attribute inventory/state probes.
- [x] Read-only `ariec61850_live_discover` human/JSON CLI.
- [x] Discovery service allowlist regression proving no MMS Write request is emitted.

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
- [ ] Pagination continuation evidence accepted on a target that requires more than one page.

### Phase 4D-R — BRCB operational semantics

- [x] Retained bounded history with independent delivery cursor.
- [x] EntryID replay/resume and rewind-to-oldest semantics.
- [x] PurgeBuf and explicit replay-gap handling.
- [x] Association-aware reservation/ownership with ResvTms reconnect/expiry behavior.
- [x] MMS BRCB operational object exposure and association-context propagation.
- [x] TCP/reset/COTP-DR association-loss lifecycle bridge.
- [x] Recovery image v2 preserves retained replay history/cursor/gap and restores legacy v1.
- [~] Physical ESP32-P4 BRCB raw-partition geometry/latency/endurance/power-loss evidence remains a separate NVM gate.

### Phase 4D-C — guarded IEC 61850 Control Model parity

- [x] C1 association-aware, default-deny guarded-control safety state machine.
- [x] C1 Direct normal, SBO normal, Direct enhanced, and SBO enhanced lifecycle semantics.
- [x] C1 immutable selected sequence, ownership, timeout, Cancel, association-loss cleanup, and ctlNum 1..255 wrap.
- [x] C2 exact live-TypeSpecification `Oper`, `SBOw`, and `Cancel` structure builder.
- [x] C2 typed SPC/DPC/integer/unsigned/floating/step control values and conservative vendor-field rejection.
- [x] C2 exact origin, ctlNum, T, optional operTm, Test, and two-bit Check construction with golden MMS Data evidence.
- [x] C3 enhanced-security CommandTermination and LastApplError object correlation.
- [x] C3 standard ControlError and AddCause 0..27 mapping with raw unknown-code preservation.
- [x] C4 live ctlModel/GVAA/GetNameList/CF-timeout discovery over the production MMS association runtime.
- [x] C4 Direct/SBO/SBOw/Oper/Cancel orchestration, asynchronous application-error grace window, non-fatal enhanced termination timeout, and no automatic command retry.
- [x] C1-C4 integrated into the normal `ARIEC61850::core` build and GCC/Clang/MSVC CTest matrix.
- [x] C5 guarded live-interoperability harness implemented with read-only default, explicit arm token, typed values, status-before/after reads, deterministic control-Write counting, JSON evidence, Wireshark filter hint, and offline safety self-test.
- [x] C5 dedicated GCC/Clang/MSVC harness CI and build artifact workflow implemented.
- [x] C5 tested-simulator acceptance is recorded for Direct normal, SBO normal, Direct enhanced, and SBO enhanced; the complementary locally retained subset includes hashed JSON/PCAP for SBO enhanced SelectWithValue/Cancel, negative LastApplError/AddCause, Direct enhanced Oper/positive CommandTermination, exact Write counts, and state restoration.
- [~] Association-loss, multi-client contention, physical-IED, and broader multi-vendor control cases remain pending.

### Later Phase 4 work

- [x] MMS FileDirectory `[77]` codec with root/nested paths, attributes, `moreFollows`, and `continueAfter`.
- [x] Bounded FileDirectory pagination with deterministic order-preserving deduplication and no-progress detection.
- [x] MMS FileOpen/FileRead/FileClose `[72-74]` codecs with signed Integer32 FRSM parity.
- [x] Caller-owned streaming sink runtime with byte/read/block/diagnostic bounds and advertised-size validation.
- [x] Best-effort FileClose after read, validation, sink, and observed cancellation failures without replacing the primary error.
- [x] Precise file/file-non-existent rooted-backslash fallback before the first FileRead only.
- [x] Host list/download CLI, JSON evidence, dedicated regression suite, mutation smoke, and libFuzzer corpus.
- [x] Controlled live FileDirectory and small-file download with successful FileClose and local hash evidence.
- [x] Controlled live forced block-limit failure with zero local bytes and successful FileClose cleanup.
- [ ] Multi-page physical FileDirectory continuation evidence.
- [ ] Multi-vendor file-service acceptance and ESP32 device validation.
- [ ] Dynamic DataSet define/delete parity and full dynamic RCB lifecycle parity where still missing from the C# surface.

## Phase 5 — simulator and applications

- [ ] Deterministic IED simulation and read-only MMS server integration parity.
- [ ] CLI/application parity and broader Windows Npcap abstraction where still required.
- [ ] Native Engineering Workbench, simulator, discovery, and SV publisher UI parity where product scope still requires it.

## Safety gate

SCL, COMTRADE, PCAP, and the Phase 4C/4C.1 discovery surface remain read-only. MMS file
download is also remote-content read-only but has an active FileOpen/FileRead/FileClose resource
lifecycle, so the host CLI requires an explicit remote path/destination and controlled-lab
authorization. Phase 4D-C live control is an explicit, authorization-gated API and the C5
interoperability harness is read-only by default; it refuses a control Write without the exact
laboratory arm token. RCB mutation, control, dynamic DataSet mutation, remote file mutation,
Npcap transmission, and real-time process-bus scheduling retain separate explicit safety gates.
Hosted CI does not constitute physical IED interoperability or IEC 61850 conformance certification.
