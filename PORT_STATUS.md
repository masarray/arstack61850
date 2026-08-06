# C++ Port Status

## Current milestone

**Phase 3B — offline ISO Session, Presentation, and ACSE association codecs are implemented and validated on the published stacked branch.**

## Delivered modules

| Area | Status |
|---|---|
| BER, Ethernet, VLAN, process-bus, PCAP | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE wire codec and offline runtime | Complete |
| Sampled Values codec and supervision | Complete |
| SCL secure parser and semantic model | Complete; PR #6 CI passed |
| COMTRADE reader and SCL mapping | Complete; PR #7 CI passed |
| TPKT and COTP transport foundation | Complete; PR #8 CI passed |
| ISO Session CN/AC and data-transfer profile | Implemented |
| Presentation CP/CPA context negotiation | Implemented |
| Presentation P-DATA and PDV context routing | Implemented |
| ACSE AARQ/AARE association envelopes | Implemented |
| C# association golden-vector parity | Implemented |
| OSI association mutation smoke/libFuzzer | Complete |
| MMS Initiate and confirmed-service envelopes | Phase 3C, not started |
| Active TCP connection runtime | Disabled |

## Phase 3B behavior

The offline association layer:

- strictly encodes and decodes bounded ISO Session Connect and Accept SPDUs;
- preserves and mirrors the C# session requirements, calling selector, called selector, and context identifiers;
- encodes and decodes Presentation CP and CPA PPDUs with explicit context definitions and negotiation results;
- supports ACSE and MMS presentation contexts with BER transfer syntax;
- encodes and decodes fully-encoded-data PDV lists and the IEC 61850 P-DATA transfer profile;
- structurally encodes and decodes ACSE AARQ and AARE envelopes;
- extracts application context, AP titles, AE qualifiers, association result, diagnostic, and EXTERNAL user information;
- preserves the MMS Initiate request or response as an opaque Phase 3C payload while validating its ACSE and Presentation routing; and
- builds deterministic association responses by mirroring the request session parameters and presentation context ordering.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Nine deterministic Session/Presentation/ACSE regression groups: passed.
- C#-derived 184-byte association request vector: byte-exact pass.
- Deterministic 138-byte association response vector: byte-exact pass.
- Presentation context negotiation and context-ID mirroring: passed.
- P-DATA encode/decode matrix and malformed-envelope rejection: passed.
- Local association libFuzzer: 5,000 runs, passed with no crash artifact.
- GitHub GCC, Clang, and Windows MSVC matrix: passed.
- GitHub ASan/UBSan: passed.
- Eight-corpus libFuzzer workflow including OSI association: passed with no crash artifact.

## Remaining acceptance gates

- Phase 3C MMS Initiate, invoke routing, and confirmed-service envelopes.
- Socket connection, cancellation, timeout, and live IED interoperability remain outside this phase.

## Safety boundary

Phase 3B is an offline deterministic codec and association-envelope layer. It does not open a TCP socket, connect to an IED, send MMS traffic, perform writes or controls, or enable active network transmission.
