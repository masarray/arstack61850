# ARStack61850 North Star

> **ARIEC61850 understands IEC 61850. ARStack61850 measures and executes IEC 61850 in real time.**

ARStack61850 is the real-time, hardware-aware evolution of ARIEC61850. The goal is not merely to port protocol code to C++ or to build another desktop packet viewer. The long-term goal is an **open, evidence-driven IEC 61850 instrumentation platform** for reliable Sampled Values, GOOSE, PTP, commissioning, closed-loop testing, and process-bus measurement.

The quality bar is defined by professional instrumentation principles rather than by imitation of any commercial product: dedicated measurement hardware, SCL-centric engineering, deterministic execution, hardware timestamping, explicit synchronization state, reproducible evidence, and standards-backed interoperability.

## 1. Product thesis

A normal desktop application sees packets after they have crossed a NIC, host transport, OS driver, capture framework, scheduler, and application runtime. That is useful for many engineering tasks, but it is not enough to make measurement-grade claims about process-bus timing.

ARStack moves the timing-critical path into dedicated hardware:

```text
                    ENGINEERING / CONTROL PLANE
┌──────────────────────────────────────────────────────────────────────┐
│ Host application                                                     │
│                                                                      │
│ SCL engineering   profile compiler   waveform/fault editor           │
│ evidence/report   automation         long-term storage / PCAP        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ versioned compiled configuration
                               │ high-speed isolated management link
                               v
                    REAL-TIME INSTRUMENT PLANE
┌──────────────────────────────────────────────────────────────────────┐
│ ARStack hardware                                                     │
│                                                                      │
│ RTOS              DMA-aware Ethernet       hardware timestamps       │
│ PTP clock/servo   deterministic SV/GOOSE   bounded capture/trigger   │
│ sequence/quality  timing statistics        evidence metadata         │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ process / station bus
                               v
                         Relay / MU / IED
```

The host is the engineering brain and visualization surface. The hardware is the **time and packet truth sensor**.

## 2. What makes ARStack different from ARIEC61850

ARIEC61850 remains valuable as a portable protocol and engineering reference. ARStack adds capabilities that must be designed around real-time hardware rather than ordinary application timing:

- RTOS-native execution and explicit task/core ownership;
- fixed/preallocated hot paths with no uncontrolled heap allocation;
- Ethernet DMA/ring awareness and explicit overrun/drop accounting;
- hardware RX/TX timestamps rather than host-arrival timestamps;
- PTP-disciplined hardware time and explicit lock/holdover state;
- deterministic sample scheduling and deadline evidence;
- SCL-compiled immutable runtime profiles;
- process-bus-specific measurement, trigger, and evidence pipelines;
- closed-loop regression and reproducible benchmark artifacts.

Protocol intelligence should be reused from ARIEC61850 and related ARStack components where appropriate. Timing truth must come from ARStack hardware.

## 3. Industry design principles

Publicly documented instrumentation and real-time test systems converge on several recurring principles. ARStack adopts the principles without embedding vendor or product references in the repository.

### 3.1 Dedicated hardware matters

Precision process-bus work benefits from dedicated network interfaces, hardware timestamping, deterministic compute, and isolation between the engineering host and the measured network.

**ARStack implication:** precision mode must place timing/capture in dedicated hardware rather than treating the host OS as the measurement clock.

### 3.2 SCL is the engineering source of truth

Professional IEC 61850 workflows are configuration-centric. IEC 61850-6 exists to exchange interoperable system and IED configuration between engineering tools.

**ARStack implication:** standards-facing publishers/subscribers must be SCL-driven. Hard-coded APPID/MAC/VLAN/dataset values are acceptable only for bring-up or explicitly named compatibility profiles.

### 3.3 Process-bus analysis is more than packet decoding

A serious analyzer must combine protocol semantics, sequence integrity, signal reconstruction, timing statistics, SCL comparison, redundancy awareness, triggers, and exportable evidence.

### 3.4 Real-time testing must support abnormal conditions

Controlled packet loss, delay, duplication, reordering, quality changes, synchronization loss, and waveform/fault scenarios should be first-class reproducible test features after the canonical deterministic path is proven.

### 3.5 Evidence and conformance must remain separate

A decoded frame or visually correct waveform is not a conformance certificate.

**ARStack implication:** packet correctness, SCL match, timing evidence, synchronization evidence, redundancy behavior, relay interoperability, and formal conformance are independent gates.

A vendor-neutral research summary is maintained in [`docs/GLOBAL_BENCHMARK.md`](docs/GLOBAL_BENCHMARK.md).

## 4. Standards baseline

ARStack must track standards explicitly rather than use “IEC 61850” as a generic label.

| Area | Standards / references | ARStack role |
|---|---|---|
| Sampled Values mapping | IEC 61850-9-2 Ed.2.1 | Canonical L2 SV encode/decode and field-presence behavior |
| Configuration / SCL | IEC 61850-6 Ed.2.2 | Host-side configuration source and interoperability contract |
| Time synchronization | IEC/IEEE 61850-9-3 | PTP power-profile validation and synchronization policy |
| Digital instrument transformer interface | IEC 61869-9 | Modern MU/digital-instrument-transformer profile family |
| SAMU context | IEC 61869-13 | Stand-alone merging-unit use cases |
| Conformance | IEC 61850-10 Ed.2.1 | Test/evidence methodology and claim discipline |
| Relay functional interoperability | IEC TS 60255-216-1 | Protection functions using SV/GOOSE/time sync |
| Redundancy | IEC 62439-3 | PRP/HSR observation, publication and validation roadmap |
| IEC 61850 security | IEC 62351-6 | Security-aware future protocol/tooling boundary |

Profile labels are not interchangeable:

1. **Generic IEC 61850-9-2 / SCL-driven** — exact selected SCL stream drives the output.
2. **IEC 61869-9** — only when its constraints are explicitly implemented and evidenced.
3. **Legacy 9-2LE compatibility** — an explicit compatibility mode, never silently promoted to the generic standard.

## 5. Core architecture principles

### 5.1 Separate facts, expectations, claims and synchronization

- **wire facts** — what was actually received/transmitted/timestamped;
- **configured expectations** — what SCL/profile says should happen;
- **profile claims** — what compatibility/conformance conclusion is justified;
- **synchronization evidence** — what is known about PTP lock and clock quality.

Unknown stays unknown. A heuristic must never silently become a standards claim.

### 5.2 Keep SCL/XML out of the realtime hot path

The host parses SCD/CID/ICD/IID and compiles a compact immutable profile. The embedded device validates version/length/checksum, builds fixed packet templates, then executes them deterministically.

```text
SvPublisherProfile
  schemaVersion
  profileFamily
  destinationMac
  appId
  vlanId / vlanPriority
  svId
  confRev
  nofAsdu
  SmvOpts field-presence policy
  dataSetReference
  samplingBasis / sampleRate
  sampleCounterPolicy
  synchronizationPolicy
  channels[]
    semantic
    basicType
    byteOffset
    scale
    qualityPolicy
```

### 5.3 One hot path, no surprise work

In deterministic mode:

- no XML parsing;
- no string formatting;
- no logging per packet;
- no unbounded queues;
- no hidden heap allocation;
- no dynamic frame re-encoding when a fixed template can be patched;
- no dependency on desktop scheduling for sample cadence.

### 5.4 Measurement timestamp hierarchy

Never collapse these timestamps into one metric:

```text
T0  scheduled sample deadline
T1  realtime publisher wake / ready time
T2  EMAC hardware TX timestamp
T3  ARStack analyzer hardware RX timestamp
T4  external reference capture timestamp
T5  host/OS capture arrival timestamp
```

`T5` is useful for capture-path diagnostics but is not device-TX timing truth.

### 5.5 `smpSynch` must be truthful

```text
no valid synchronization evidence -> unsynchronized
acquiring / unlocked              -> unsynchronized
measured valid local sync         -> profile-defined local indication
measured valid global/PTP lock    -> profile-defined global indication
holdover                          -> explicit policy + evidence
```

Compatibility overrides may exist for relay-readability tests, but they must be visibly labeled and must never be reported as synchronization evidence.

## 6. Product family north star

### 6.1 ARStack SV Injector

Target capabilities:

- SCL stream import and exact profile compilation;
- 50/60 Hz and profile-defined sampling;
- 4I+4V and arbitrary SCL-bound datasets;
- waveform/fault/state sequencer;
- COMTRADE playback;
- quality/test/simulation bit manipulation;
- controlled packet loss, delay, duplication, reordering and sync-loss scenarios;
- deterministic multi-stream publishing;
- hardware TX timestamp statistics;
- PTP-disciplined publishing;
- reusable relay-oriented test scenarios;
- machine-readable evidence export.

### 6.2 ARStack Process Bus Analyzer

Target capabilities:

- passive SV/GOOSE/PTP discovery;
- per-stream hardware RX timestamps;
- packet interval/jitter histograms and P50/P95/P99/max;
- sequence gap, duplicate and out-of-order detection;
- waveform, RMS, frequency, phasor and harmonics;
- SCL expected-vs-observed comparison;
- quality/test/simulation diagnostics;
- PTP source/domain/offset/path-delay/lock diagnostics;
- PRP/HSR duplicate-path and asymmetry analysis;
- triggers and bounded pre/post-event capture;
- PCAP/COMTRADE/evidence reports;
- cross-device time-correlated capture.

### 6.3 ARStack Digital Substation Probe

- portable or DIN-rail operation;
- isolated management plane;
- continuous health/event monitoring;
- remote evidence capture;
- station/process-bus diagnostics without making the engineering host the measurement endpoint.

### 6.4 ARStack Real-Time Test Runtime

A longer-term deterministic environment for multi-IED tests, abnormal traffic injection, merging-unit simulation, and closed-loop protection testing.

## 7. Hardware roadmap

### Stage A — current ESP32-P4 development platform

Purpose: prove the real-time architecture cheaply and openly.

- dual-core RTOS runtime;
- internal EMAC + RMII PHY;
- IEEE1588v2-capable EMAC;
- USB control/debug;
- one process-bus Ethernet port;
- suitable for injector and mirrored/TAP-output analyzer development.

The current one-port development board is **not** the final answer for a transparent inline two-port measurement instrument.

### Stage B — ARStack Precision hardware

Recommended direction:

- at least two independent process-side network ports;
- copper + optical options where practical;
- hardware timestamp at ingress/egress as close to the MAC/PHY as possible;
- dedicated oscillator / PTP-disciplined clock domain;
- optional programmable logic or deterministic switch/TAP front-end for transparent forwarding, duplication, line-rate filtering and multi-port timestamping;
- real-time processor for RTOS/control/protocol execution;
- high-speed isolated management link to host;
- process network electrically/logically separated from the engineering host;
- local nonvolatile evidence buffer;
- secure boot/update path.

### Stage C — redundancy / multi-network instrument

- dual-LAN PRP;
- HSR-aware capture/injection;
- multiple independent station/process networks;
- multi-device clock correlation;
- hardware acceleration when MCU-only execution no longer provides adequate margin.

## 8. Evidence-first quality model

Minimum evidence bundle for a deterministic SV run:

```text
profile.json
runtime-config.json
firmware build/version/commit
hardware identity/revision
PTP/clock state
per-second counters
scheduled-vs-TX hardware timing statistics
RX/reference timing statistics when available
sequence integrity summary
PCAP/PCAPNG
optional COMTRADE
machine-readable verdict + human report
```

A future hardware CI bench should be able to reject a build automatically when thresholds regress, for example:

- unexplained `smpCnt` gap;
- TX/RX queue overflow;
- missed realtime deadline;
- P99 hardware timing error above project threshold;
- PTP unlock during a locked test;
- SCL/profile mismatch;
- wrong APPID/VLAN/MAC/dataset ordering;
- PRP/HSR path asymmetry outside test policy.

Thresholds must be evidence-based and profile-specific, not invented globally.

## 9. Current proven state

Current bench observations have demonstrated:

- ESP32-P4 rev 1.3 + RMII PHY link bring-up;
- raw Ethernet transmission to a host capture path;
- IEC 61850 Sampled Values frames decoded by an independent packet capture stack;
- GPTimer + FreeRTOS notification scheduling at 4000 samples/s for 50 Hz / 80 samples-per-cycle;
- zero device-side TX failures and zero missed timer notifications in the observed stable runs;
- balanced reconstructed 50 Hz three-phase waveform on the diagnostic stream;
- continuous sequence integrity in the analyzer;
- clear evidence that a host USB-Ethernet capture path can distort/batch individual arrival timestamps even while average throughput remains about 4000 fps.

These are **bench observations, not formal conformance evidence**.

Current limitations:

- canonical multicast/VLAN capture needs validation on a trusted measurement path;
- hardware TX timestamps are not yet wired into the ARStack evidence loop;
- PTP clock discipline/61850-9-3 is not implemented yet;
- SCL-driven compiled profiles are not yet deployed to the ESP runtime;
- 60 Hz / 4800 samples/s evidence is still pending;
- multi-stream and PRP/HSR are future gates;
- engineering scaling/profile semantics still require standards-backed profile work.

See [`PROJECT_HANDOFF.md`](PROJECT_HANDOFF.md), issue #21, issue #24 and the current embedded PR for active implementation state.

## 10. Development gates

### Gate P2 — deterministic single-stream evidence

- sustained 4000 fps then 4800 fps;
- zero unexplained sequence gaps;
- bounded queue behavior;
- EMAC hardware TX timestamp integration;
- 60 s evidence runs;
- independent capture/reference evidence.

### Gate P3 — standards-first profiles + PTP

- host SCL -> immutable `SvPublisherProfile`;
- exact field-presence policy from SCL/selected profile;
- profile validation/golden frames;
- hardware PTP clock enable/read/adjust;
- passive PTP monitor + IEC/IEEE 61850-9-3 validator;
- servo state machine (unlocked/acquiring/locked/holdover);
- `smpSynch` driven by measured state;
- profile + timing evidence export.

### Gate P4 — professional injector/analyzer workflow

- fault/scenario engine and COMTRADE playback;
- hardware-timestamped receive analyzer;
- trigger engine;
- SCL expected-vs-observed diagnostics;
- reusable test procedures and automated evidence reports.

### Gate P5 — precision hardware

- dual-port / multi-port front-end;
- transparent or TAP-style measurement path;
- stronger oscillator and timestamp architecture;
- PRP/HSR and multi-network support;
- independently validated measurement uncertainty and interoperability.

## 11. Decision rule

When architecture choices conflict, prefer:

```text
standards truth
    > measured hardware evidence
    > deterministic architecture
    > interoperability evidence
    > usability
    > convenience hacks
```

This file is the project constitution. New features should strengthen that hierarchy, not bypass it.