# C++ Port Status

## Current milestone

**Phase 1C — GOOSE subscriber supervision and publisher runtime is implemented.**

The C++ stack now covers the deterministic path from MMS values through a complete
GOOSE Ethernet frame and the state machines immediately above that wire codec.

## Delivered modules

| Area | Status |
|---|---|
| BER primitives | Complete |
| Ethernet, VLAN, process-bus header | Complete |
| Classic PCAP reader/writer | Complete |
| IEC 61850 UTC time | Complete |
| MMS Data / AllData codec | Complete |
| GOOSE PDU codec | Complete |
| GOOSE Ethernet frame codec | Complete |
| GOOSE subscriber supervision | Complete |
| GOOSE publisher session/runtime | Complete, offline frame output only |
| Sampled Values | Not started |
| SCL and COMTRADE | Not started |
| TPKT/COTP/ACSE/MMS association | Not started |
| Reporting and control | Not started |
| Simulator and native UI | Not started |

## Phase 1C behavior

Subscriber supervision includes:

- TimeAllowedToLive deadline calculation and one-shot expiry reporting;
- arrival-gap tracking;
- duplicate, retransmission, sequence-gap, sequence-regression, state-change,
  state-jump, and state-regression classification;
- `uint32` counter wraparound;
- expected stream identity validation;
- configuration revision change indication; and
- detection of changed AllData while `stNum` remains unchanged.

Publisher runtime includes:

- initial publication with configured state and sequence counters;
- state-change publication with `stNum` increment and `sqNum = 0`;
- retransmissions with `sqNum` increment and wraparound;
- exponential min-time to max-time scheduling;
- schedule reset after every state change; and
- a deterministic `poll(now)` API that avoids sleeping threads and timing-flaky tests.

## Validation

- GNU C++ 14.2, Release, warnings as errors: passed.
- Clang 17, Release, warnings as errors: passed.
- Four CTest executables passed under both compilers:
  - core foundation;
  - MMS values;
  - GOOSE wire codec; and
  - GOOSE subscriber/publisher runtime.
- Windows MSVC verification is performed by GitHub Actions for every stacked PR.

## Safety boundary

No network adapter is opened by Phase 1C. The publisher runtime only returns encoded
Ethernet-frame bytes. Npcap transmission, background network threads, active GOOSE
publishing, MMS writes, and IED controls remain disabled until PCAP equivalence and
isolated-lab interoperability are completed.
