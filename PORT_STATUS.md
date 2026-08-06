# C++ Port Status

## Current milestone

**Phase 3A — offline RFC 1006 TPKT and COTP transport framing is implemented and validated on the published stacked branch.**

## Delivered modules

| Area | Status |
|---|---|
| BER, Ethernet, VLAN, process-bus, PCAP | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE wire codec and offline runtime | Complete |
| Sampled Values codec and supervision | Complete |
| SCL secure parser and semantic model | Complete; PR #6 CI passed |
| COMTRADE reader and SCL mapping | Complete; PR #7 CI passed |
| TPKT frame codec | Implemented |
| Incremental TPKT stream decoder | Implemented |
| COTP CR/CC/DT/DR/ER codec | Implemented |
| TPDU-size and TSAP negotiation | Implemented |
| COTP segmentation and bounded reassembly | Implemented |
| OSI transport mutation smoke/libFuzzer | Complete |
| Session, Presentation, and ACSE | Not started |
| Active TCP connection runtime | Disabled |

## Phase 3A behavior

The offline transport layer:

- encodes and strictly decodes RFC 1006 TPKT frames;
- rejects unsupported versions, non-zero reserved octets, undersized lengths, and exact-length mismatches;
- incrementally extracts complete TPKT frames from fragmented or coalesced TCP-style byte streams;
- decodes COTP Connection Request, Connection Confirm, Data, Disconnect Request, and Error TPDUs;
- validates the COTP length indicator and complete variable-parameter TLVs;
- preserves the C# default Connection Request wire vector;
- mirrors C1/C2 TSAP selectors in Connection Confirm and never selects a TPDU size larger than the peer proposal;
- segments Data TPDUs according to the negotiated TPDU capacity; and
- reassembles EOT sequences under explicit byte, fragment, and empty-fragment limits.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Nine deterministic TPKT/COTP regression groups: passed.
- C#-derived TPKT, CR, CC, and Data golden vectors: passed.
- Incremental stream fragmentation/coalescing matrix: passed.
- Bounded segmentation/reassembly and abuse guards: passed.
- Local transport libFuzzer: 5,000 runs, passed with no crash artifact.
- GitHub GCC, Clang, and Windows MSVC matrix: passed.
- GitHub ASan/UBSan: passed.
- Seven-corpus libFuzzer workflow including OSI transport: passed with no crash artifact.

## Remaining acceptance gates

- Phase 3B ISO session, Presentation, and ACSE association codecs.
- Socket connection, cancellation, timeout, and live IED interoperability remain outside this phase.

## Safety boundary

Phase 3A is an offline deterministic codec and byte-stream reassembly layer. It does not open a TCP socket, connect to an IED, send MMS traffic, perform writes or controls, or enable any active network transmission.
