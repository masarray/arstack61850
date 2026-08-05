# C++ Port Status

## Current milestone

**Phase 1D — Sampled Values core codec and supervision is implemented and CI-validated.**

The C++ stack now covers deterministic MMS data, GOOSE wire/runtime behavior, and the
core Sampled Values path from ASDU/APDU encoding through Ethernet framing and `smpCnt`
supervision.

## Delivered modules

| Area | Status |
|---|---|
| BER primitives | Complete |
| Ethernet, VLAN, process-bus header | Complete |
| Classic PCAP reader/writer | Complete |
| IEC 61850 UTC time | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE PDU and Ethernet frame codecs | Complete |
| GOOSE subscriber and publisher runtime | Complete, offline frame output only |
| Sampled Values ASDU/APDU/frame codec | Complete |
| Sampled Values counter and stream supervision | Complete |
| Sampled Values quality and generic payload inspection | Complete |
| Captured SV PCAP equivalence | Pending |
| Decoder fuzzing | Pending |
| SCL and COMTRADE | Not started |
| TPKT/COTP/ACSE/MMS association | Not started |
| Reporting and control | Not started |
| Simulator and native UI | Not started |

## Phase 1D behavior

Sampled Values wire support includes:

- application-tag-0 SAV PDU encoding and decoding;
- multi-ASDU sequence handling and exact `noASDU` consistency checks;
- optional dataset reference, reference time, sample rate, and sample mode fields;
- complete Ethernet/VLAN/process-bus frame handling for EtherType `0x88BA`;
- strict rejection of malformed BER, invalid UTC time, wrong EtherType, trailing APDU
  data, and impossible process-bus length declarations; and
- C#-derived byte-for-byte APDU and complete Ethernet-frame vectors.

Runtime diagnostics include:

- sample-counter initialization, increment, configurable wrap, continuity, gaps,
  duplicates, out-of-order traffic, and trusted restart classification;
- stream identity and configuration-revision checks with statistics;
- IEC 61850 quality-word encoding/decoding; and
- vendor-neutral big-endian 32-bit payload views without inventing channel semantics.

## Validation

The final Phase 1D branch passed the complete GitHub Actions matrix:

- GNU C++ / Release / warnings as errors: passed;
- Clang / Release / warnings as errors: passed;
- Windows MSVC / Release / `/W4 /WX`: passed; and
- five CTest executables passed on every platform:
  - core foundation;
  - MMS values;
  - GOOSE wire codec;
  - GOOSE subscriber/publisher runtime; and
  - Sampled Values codec and supervision.

The initial MSVC run identified narrowing construction of `optional<uint16_t>` inside
test fixtures. Explicit `uint16_t` values fixed the portability issue without changing
the library API or wire behavior.

## Safety boundary

Sampled Values support currently parses and returns encoded bytes only. No adapter is
opened, and no real-time publisher thread or raw Ethernet transmission is enabled.
PCAP equivalence, sanitizer/fuzz coverage, timing validation, and isolated-laboratory
interoperability remain required before active process-bus transmission.
