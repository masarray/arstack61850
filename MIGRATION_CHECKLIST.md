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

- [x] GOOSE PDU model.
- [x] GOOSE PDU BER encoder/decoder.
- [x] GOOSE `numDatSetEntries` consistency validation.
- [x] GOOSE Ethernet/VLAN/process-bus frame encoder/decoder.
- [x] GOOSE C#-compatible byte-for-byte golden vectors.
- [x] GOOSE malformed/truncated-input regression tests.

### Phase 1C — GOOSE supervision and publisher runtime

- [x] Subscriber TimeAllowedToLive deadline supervision.
- [x] One-shot expiry events and expiration statistics.
- [x] First/retransmission/duplicate classification.
- [x] Sequence gap and sequence regression detection.
- [x] State change, state jump, and state regression detection.
- [x] `stNum` and `sqNum` 32-bit wraparound behavior.
- [x] Expected control-block, dataset, and configuration-revision checks.
- [x] Changed AllData without `stNum` increment detection.
- [x] Publisher session with C#-compatible state/sequence behavior.
- [x] Publisher retransmission runtime with exponential scheduling.
- [x] Schedule reset and `sqNum = 0` on state changes.
- [x] Stable state timestamp across retransmissions.
- [x] Offline encoded-frame output with no raw-network activation.

### Phase 1D — Sampled Values and evidence hardening

- [ ] Sampled Values ASDU model.
- [ ] Sampled Values APDU and Ethernet frame codec.
- [ ] Sampled Values sequence supervision and stream diagnostics.
- [ ] Captured PCAP equivalence tests from real or simulated IED traffic.
- [ ] Decoder fuzzing corpus for BER, GOOSE, and SV.

**Phase 1 status: MMS and GOOSE wire/runtime layers complete; Sampled Values and
capture/fuzz evidence remain.**

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

- [x] GCC C++20 build with warnings as errors.
- [x] Clang C++20 build with warnings as errors.
- [x] Windows MSVC build and CTest through Phase 1B.
- [x] Windows MSVC build and CTest for Phase 1C.
- [x] MMS C#-derived golden vector.
- [x] GOOSE PDU C#-derived golden vector.
- [x] GOOSE complete Ethernet frame golden vector with VLAN and process-bus header.
- [x] Malformed BER and GOOSE count-mismatch rejection.
- [x] Deterministic subscriber and publisher runtime regression suite.
- [ ] C# and C++ executable oracle comparison in one CI job.
- [ ] Sanitizer jobs and fuzzing.
- [ ] Isolated-lab IED interoperability.

## Safety gate

The Phase 1C publisher is an offline state machine that returns encoded bytes. Active
Npcap transmission, MMS write/control, and IED operation remain outside the enabled
runtime surface until timeout, cancellation, disconnect cleanup, PCAP equivalence, and
isolated-lab tests pass.
