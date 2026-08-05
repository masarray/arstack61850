# C++ Port Status

## Current milestone

**Phase 1E — automated process-bus evidence and security hardening is implemented.**

The deterministic MMS, GOOSE, and Sampled Values layers now have synthetic PCAP
comparison, sanitizer coverage, mutation smoke tests, libFuzzer harnesses, and a
receive-only lab evidence workflow.

## Delivered modules

| Area | Status |
|---|---|
| BER, Ethernet, VLAN, process-bus, PCAP | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE wire codec and offline runtime | Complete |
| Sampled Values codec and supervision | Complete |
| Synthetic GOOSE/SV PCAP equivalence | Complete |
| Read-only PCAP interoperability checker | Complete |
| PCAP allocation-length hardening | Complete |
| ASan/UBSan build and regression suite | Complete locally; CI verification pending |
| Deterministic mutation smoke suite | Complete |
| LLVM libFuzzer targets and seed corpus | Implemented; CI verification pending |
| Isolated-lab runner/checklist/report | Complete |
| Real IED receive-only interoperability | Pending physical capture |
| C# executable-oracle CI comparison | Pending original C# executable/project integration |
| Active GOOSE/SV transmission | Disabled |

## Phase 1E behavior

The PCAP evidence analyzer:

- reads classic Ethernet PCAP without opening a network adapter;
- identifies GOOSE and Sampled Values frames;
- decodes and re-encodes each process-bus frame;
- requires exact byte-for-byte equivalence;
- verifies packet order, frame bytes, and microsecond-normalized timestamps through a
  canonical PCAP write/read cycle; and
- produces human-readable or JSON evidence suitable for a controlled lab report.

Security hardening includes:

- rejection of zero or excessive PCAP `snaplen`;
- validation that included packet length does not exceed `snaplen` or the original
  packet length before allocation;
- ASan/UBSan instrumentation;
- deterministic decoder mutation tests on all compilers; and
- libFuzzer entry points for BER, GOOSE, Sampled Values, and PCAP.

## Local validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Synthetic PCAP exact equivalence: passed.
- Deterministic mutation smoke: passed and found the PCAP allocation issue now covered
  by an explicit regression test.

## Remaining acceptance gates

- GitHub sanitizer and libFuzzer jobs must pass on the published branch.
- At least one controlled capture from a real or vendor-simulated IED must pass the
  checker and be documented using `LAB_INTEROP_REPORT_TEMPLATE.md`.
- Real-time Sampled Values timing health and active transmission require a separate,
  approved laboratory plan.
