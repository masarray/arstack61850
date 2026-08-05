# ARIEC61850 C++ Migration Checklist

Status legend: `[x]` implemented and regression-tested, `[~]` partially implemented,
`[ ]` not started. The C# repository remains the behavioral oracle until laboratory
interoperability is complete.

## Phase 0 — portable deterministic foundation

- [x] C++20 static library and CMake build.
- [x] Strict compiler warnings promoted to errors.
- [x] GitHub Actions matrix: GCC, Clang, and Windows MSVC.
- [x] BER identifier, length, TLV, integer, boolean, float, string, and UTC-time primitives.
- [x] MAC address, VLAN, and Ethernet frame codec.
- [x] IEC 61850 process-bus header and APDU framing.
- [x] Classic PCAP Ethernet reader/writer.
- [x] GOOSE retransmission schedule.

**Phase 0 status: complete.**

## Phase 1 — deterministic process-bus codecs and runtime

### Phase 1A — MMS data foundation

- [x] IEC 61850 8-byte UTC time with quality byte.
- [x] Recursive MMS Data value model.
- [x] MMS AllData BER encoder/decoder.

### Phase 1B — GOOSE wire codec

- [x] GOOSE PDU model and BER codec.
- [x] GOOSE Ethernet/VLAN/process-bus frame codec.
- [x] Dataset-count and malformed-input validation.
- [x] C#-compatible byte-for-byte golden vectors.

### Phase 1C — GOOSE supervision and publisher runtime

- [x] Subscriber TTL, sequence, state, identity, and wraparound supervision.
- [x] Publisher session and exponential retransmission state machine.
- [x] Stable state timestamps and offline encoded-frame output.

### Phase 1D — Sampled Values

- [x] Sampled Values ASDU model.
- [x] Multi-ASDU SAV PDU model.
- [x] Application-tag-0 BER encoder/decoder.
- [x] Exact `noASDU` consistency validation.
- [x] Optional `datSet`, `refrTm`, `smpRate`, and `smpMod` support.
- [x] Sampled Values Ethernet/VLAN/process-bus frame codec (`0x88BA`).
- [x] `smpCnt` policy and configurable-wrap tracker.
- [x] Continuous, normal-wrap, gap, duplicate, out-of-order, and restart classification.
- [x] Stream identity and configuration-revision supervision.
- [x] Aggregate sequence and missing-sample statistics.
- [x] IEC 61850 Sampled Values quality-word helper.
- [x] Vendor-neutral generic `seqOfData` word inspection.
- [x] Trailing-byte preservation for unknown payload shapes.
- [x] C#-derived PDU and complete Ethernet-frame golden vectors.
- [x] Malformed BER, UTC-time, EtherType, and length rejection tests.

### Phase 1E — evidence hardening

- [ ] Captured GOOSE and SV PCAP equivalence tests from real or simulated IED traffic.
- [ ] C# and C++ executable-oracle comparison in one CI job.
- [ ] Sanitizer jobs.
- [ ] BER, GOOSE, and SV decoder fuzzing corpus.
- [ ] Isolated-laboratory GOOSE and SV interoperability.
- [ ] Real-time SV publisher timing-health validation.

**Phase 1 status: MMS, GOOSE, and Sampled Values deterministic core layers complete;
evidence hardening and active-transport validation remain.**

## Phase 2 — engineering file formats

- [ ] SCL object model.
- [ ] SCL parser and validation diagnostics.
- [ ] SCL workspace, profile extraction, and export.
- [ ] COMTRADE CFG/DAT reader.
- [ ] COMTRADE channel scaling and mapping.
- [ ] Expected-versus-observed engineering profiles.

## Phase 3 — MMS transport and association

- [ ] TPKT framing.
- [ ] COTP connection and data TPDU.
- [ ] Presentation layer.
- [ ] ACSE association and release.
- [ ] MMS confirmed/unconfirmed PDU envelopes.
- [ ] Invoke-ID routing with one receive pump.
- [ ] Name lists and variable-access attributes.
- [ ] MMS read/write services.

## Phase 4 — reporting, control, and file service

- [ ] DataSet and RCB inventory.
- [ ] Buffered and unbuffered report parsing.
- [ ] Report subscription, GI, and persistent monitoring.
- [ ] Direct and SBO control state machines.
- [ ] Enhanced-security CommandTermination and LastApplError.
- [ ] Cancel, timeout, disconnect, and cleanup behavior.
- [ ] MMS file directory and fault-record transfer.

## Phase 5 — simulator and applications

- [ ] Deterministic IED simulation runtime.
- [ ] Read-only MMS server baseline.
- [ ] CLI parity.
- [ ] Windows Npcap transport abstraction.
- [ ] Native desktop UI architecture.
- [ ] Engineering Workbench UI.
- [ ] IED Simulator UI.
- [ ] Discovery and SV Publisher UI.

## Current acceptance evidence

- [x] GCC C++20 build with warnings as errors through Phase 1D.
- [x] Clang C++20 build with warnings as errors through Phase 1D.
- [~] Windows MSVC build and CTest for Phase 1D; verified after this branch is pushed.
- [x] MMS C#-derived golden vector.
- [x] GOOSE PDU and Ethernet-frame golden vectors.
- [x] Deterministic GOOSE subscriber/publisher runtime suite.
- [x] Sampled Values PDU and Ethernet-frame golden vectors.
- [x] Sampled Values counter, supervisor, quality, and payload-diagnostics suite.
- [ ] Captured process-bus PCAP equivalence.
- [ ] Fuzzing and sanitizers.
- [ ] Isolated-lab IED interoperability.

## Safety gate

GOOSE and Sampled Values publisher components return encoded bytes only. Active Npcap
transmission, real-time SV scheduling, MMS write/control, and IED operation remain
outside the enabled runtime surface until PCAP equivalence, timeout/cancellation,
timing, fuzzing, and isolated-laboratory tests pass.
