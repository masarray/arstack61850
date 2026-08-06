# C++ Port Status

## Current milestone

**Phase 3C — offline MMS Initiate, confirmed-service envelopes, invoke routing, and core service codecs are implemented and validated on the published stacked branch.**

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
| ISO Session, Presentation, and ACSE | Complete; PR #9 CI passed |
| MMS Initiate request/response | Implemented |
| Confirmed request/response/error envelopes | Implemented |
| Bounded invoke-ID result routing | Implemented |
| GetNameList | Implemented |
| GetVariableAccessAttributes and recursive type specifications | Implemented |
| Read and Write service codecs | Implemented |
| MMS services mutation smoke/libFuzzer | Complete |
| Active MMS association/client runtime | Disabled |

## Phase 3C behavior

The offline MMS service layer:

- encodes and strictly decodes byte-compatible MMS Initiate request and response PDUs;
- encodes and decodes confirmed request, confirmed response, and confirmed error envelopes;
- classifies top-level MMS PDUs and extracts invoke IDs from raw MMS or Presentation P-DATA;
- routes confirmed results into bounded per-invoke queues while isolating unmatched traffic;
- encodes and decodes VMD-specific, domain-specific, and association-specific object names;
- supports GetNameList request/response paging fields;
- supports GetVariableAccessAttributes with bounded recursive array and structure type specifications;
- supports multi-variable Read requests and mixed success/failure access results;
- supports multi-variable Write requests and per-variable success/failure results; and
- preserves deterministic Presentation context routing without opening an association or network connection.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Clang 17, ASan + UBSan: passed.
- Eleven deterministic Phase 3C regression groups: passed.
- Full repository CTest: 14/14 passed.
- C#-derived 40-byte MMS Initiate request vector: byte-exact pass.
- Initiate response, confirmed envelopes, and confirmed-error round trips: passed.
- ObjectName, GetNameList, attributes/type, Read, Write, and invoke-router matrices: passed.
- Recursive array/structure depth and component limits: passed.
- Local MMS-services libFuzzer: 5,000 runs, passed with no crash artifact.
- GitHub GCC, Clang, and Windows MSVC matrix: passed.
- GitHub ASan/UBSan: passed.
- Nine-corpus libFuzzer workflow including MMS services: passed with no crash artifact.

## Remaining acceptance gates

- Active association lifecycle, timeout, cancellation, and live IED interoperability remain outside this phase.
- Reporting, control, and file services remain Phase 4 work.

## Safety boundary

Phase 3C is an offline deterministic codec and invoke-routing layer. It does not open a TCP socket, connect to an IED, transmit MMS traffic, execute reads or writes against equipment, or enable control operations.
