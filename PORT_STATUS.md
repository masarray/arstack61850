# C++ Port Status

## Current milestone

**Phase 2B — COMTRADE CFG/DAT parsing and SCL signal mapping is implemented and validated on the published stacked branch.**

## Delivered modules

| Area | Status |
|---|---|
| BER, Ethernet, VLAN, process-bus, PCAP | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE wire codec and offline runtime | Complete |
| Sampled Values codec and supervision | Complete |
| Synthetic GOOSE/SV PCAP equivalence | Complete |
| ASan/UBSan, mutation smoke, and libFuzzer | Complete through COMTRADE |
| SCL secure parser and semantic model | Complete; PR #6 CI passed |
| COMTRADE CFG model/parser | Implemented |
| COMTRADE ASCII DAT | Implemented |
| COMTRADE BINARY/BINARY32/FLOAT32 DAT | Implemented |
| Analog scaling and digital status words | Implemented |
| Multi-rate timestamp fallback | Implemented |
| Default electrical channel mapping | Implemented |
| SCL Sampled Values to COMTRADE binding | Implemented |
| Read-only COMTRADE inspection CLI | Implemented |
| Real IED receive-only interoperability | Pending physical capture |
| Active GOOSE/SV transmission | Disabled |

## Phase 2B behavior

The COMTRADE reader:

- parses station/device/revision metadata and analog/digital channel definitions;
- validates bounded configuration, channel, sample-rate, and data sizes;
- decodes ASCII, 16-bit BINARY, BINARY32, and FLOAT32 records;
- applies engineering scaling as `a * raw + b`;
- decodes digital channels from ASCII fields or packed 16-bit binary words;
- applies the COMTRADE time multiplier and falls back to a piecewise multi-rate schedule
  when timestamps are missing or non-monotonic;
- creates default voltage/current phase mappings; and
- maps non-quality/non-timestamp SCL Sampled Values entries to COMTRADE analog channels
  using semantic quantity/phase matching followed by deterministic ordered fallback.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Nine COMTRADE regression groups: passed.
- C#-derived ASCII 40-sample fixture: passed.
- C#-derived BINARY 80-sample fixture: passed.
- Read-only COMTRADE JSON CLI smoke: passed.
- GitHub GCC, Clang, and Windows MSVC matrix: passed.
- GitHub ASan/UBSan: passed.
- Six-corpus libFuzzer workflow including COMTRADE: passed with no crash artifact.

## Remaining acceptance gates

- Cross-language executable-oracle comparison remains pending.
- Active replay/transmission remains outside this phase.
