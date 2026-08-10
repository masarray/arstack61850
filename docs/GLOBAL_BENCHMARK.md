# Global benchmark: vendor-neutral IEC 61850 instrumentation principles

This note records the public standards and industry design lessons used to shape the ARStack61850 north star. The repository intentionally avoids naming or comparing against commercial brands/products. The goal is to preserve useful architectural lessons without creating a competitor-reference document.

Research snapshot: **August 2026**.

## 1. Industry benchmark principles

Publicly documented high-end digital-substation instruments and real-time test platforms repeatedly converge on the same architectural patterns:

| Capability class | Recurring industry pattern | ARStack lesson |
|---|---|---|
| Dedicated measurement hardware | timing and capture are moved out of a normal engineering PC; network and management planes are isolated | the host should not be the timing instrument |
| SCL-centric engineering | system configuration drives stream identity, mapping, expectations and tests | standards-facing workflows must be SCL-driven |
| Process-bus analysis | SV/GOOSE/PTP, signal reconstruction, sequence integrity, timing, redundancy and recording are analyzed together | packet decode alone is not enough |
| Deterministic real-time execution | time-critical publishing/subscribing is isolated from ordinary desktop scheduling | realtime work belongs on deterministic hardware/runtime |
| Hardware timestamping | IEEE 1588/PTP capable timestamp units are used near MAC/PHY boundaries | device timing truth must come from hardware timestamps |
| Controlled abnormal-condition testing | packet impairment, quality changes, sync loss, waveform/fault scenarios and closed-loop tests are reproducible | impairment must be a first-class scenario feature, not an ad-hoc packet hack |
| Evidence and validation | configuration expectations, measured facts and compliance claims remain separate | every claim needs reproducible evidence |

## 2. Why dedicated hardware changes the product

A host capture stack is affected by interface buffering, host transport, drivers, capture libraries and OS scheduling. A dedicated instrument can timestamp and classify traffic before those layers distort timing observations.

ARStack target split:

```text
Host                               Dedicated instrument
UI / SCL / reports       <---->    packet timing / capture / execution
storage / automation               process-bus network
```

This separation is the foundation for reliable injector and analyzer behavior.

## 3. Why SCL must drive interoperability

IEC 61850-6 exists to support interoperable exchange of system/IED configuration. Therefore the canonical ARStack workflow should be:

```text
SCD/CID/ICD/IID
    -> parse + validate
    -> select stream/control block
    -> bind DataSet and communication address
    -> compile immutable runtime profile
    -> deploy to realtime hardware
```

Hard-coded MAC/APPID/VLAN/dataset values are acceptable only for bring-up or explicitly labeled compatibility tests.

## 4. What a serious process-bus analyzer must measure

A useful analyzer should eventually cover:

1. packet/framing correctness;
2. sequence continuity, duplicates and out-of-order traffic;
3. waveform reconstruction, RMS, frequency, phasor and quality;
4. hardware RX timing and jitter statistics;
5. expected-vs-observed SCL comparison;
6. PTP source/domain/offset/path-delay/lock state;
7. PRP/HSR duplicate-path and asymmetry behavior;
8. triggerable bounded capture and evidence export.

The engineering surface should speak both raw IEC 61850 fields and electrical quantities.

## 5. Deterministic injector lessons

A professional injector architecture should support:

- deterministic scheduling independent of the desktop host;
- preallocated/fixed hot paths;
- SCL/profile-driven framing and field presence;
- waveform/fault/scenario engines;
- COMTRADE playback;
- controlled packet loss, delay, duplication and reordering;
- quality/test/simulation changes;
- controlled synchronization loss;
- multi-stream scaling with hardware acceleration when MCU margin is no longer sufficient;
- hardware TX timestamp evidence for actual on-wire timing.

## 6. Standards research that constrains the architecture

### IEC 61850-9-2 Ed.2.1

Defines the specific communication service mapping for Sampled Values over Ethernet/link layer, based on the abstract SV model in IEC 61850-7-2.

**ARStack rule:** canonical SV framing and ASDU field behavior must be standards/profile driven, not viewer driven.

Official reference:
- https://webstore.iec.ch/en/publication/66549

### IEC 61850-6 Ed.2.2

Defines the SCL exchange format intended to support compatible data exchange between system and IED engineering tools.

**ARStack rule:** SCL is the engineering configuration source; the host compiles it into deterministic runtime profiles.

Official reference:
- https://webstore.iec.ch/en/publication/103863

### IEC TS 61850-6-3:2025

Defines a machine-processable rule format for validation of IEC 61850 XML/SCL files.

**ARStack opportunity:** future SCL validation should evolve toward rule-driven validation rather than accumulating undocumented heuristics.

Official reference:
- https://webstore.iec.ch/en/publication/79695

### IEC/IEEE 61850-9-3:2016

Defines the PTP profile for power utility automation.

**ARStack rule:** seeing PTP traffic is not synchronization evidence. `smpSynch` must derive from measured hardware-clock state.

Official reference:
- https://webstore.iec.ch/en/publication/24998

### IEC 61869-9:2016

Defines digital interface requirements for instrument-transformer measurements based on IEC 61850 and network time synchronization.

**ARStack rule:** IEC 61869-9 is a distinct profile family, not a synonym for generic IEC 61850-9-2 or legacy 9-2LE.

Official reference:
- https://webstore.iec.ch/en/publication/24663

### IEC 61869-13:2021

Covers stand-alone merging units and references IEC 61869-9 for digital output format.

**ARStack opportunity:** future MU/SAMU simulator workflows should be designed against sourced product-family requirements rather than generic packet shapes.

Official reference:
- https://webstore.iec.ch/en/publication/28359

### IEC 61850-10 Ed.2.1

Defines conformance-testing techniques for clients, servers, sampled-value devices and engineering tools, including measurement techniques for declared performance parameters.

**ARStack rule:** evidence bundles and performance claims should be designed toward formal testability. “Works with our analyzer” is not conformance.

Official reference:
- https://webstore.iec.ch/en/publication/108858

### IEC TS 60255-216-1:2025

Covers protection functions using digital communication, including SV subscriptions, GOOSE and time synchronization, with functional interoperability/testing concerns.

**ARStack opportunity:** relay-oriented scenario libraries should ultimately test function behavior, not only network behavior.

Official reference:
- https://webstore.iec.ch/en/publication/77735

### IEC 62439-3

Defines PRP and HSR zero-recovery redundancy mechanisms.

**ARStack rule:** a high-end process-bus analyzer should eventually understand duplicate paths, path asymmetry and redundancy correctness rather than blindly de-duplicate traffic.

Official reference:
- https://webstore.iec.ch/en/publication/64423

### IEC 62351-6

Specifies security for protocols based on/derived from IEC 61850, including IEC 61850-9-2 context.

**ARStack rule:** security belongs in both protocol/tool boundaries and instrument management/update design.

Official reference:
- https://webstore.iec.ch/en/publication/63742

## 7. Current development-platform fit

The current ESP32-P4 target exposes an IEEE1588v2-capable Ethernet MAC and is suitable for proving:

- deterministic single-port SV/GOOSE publishing;
- PTP slave/monitor experiments;
- hardware-clock timing evidence;
- mirrored/TAP-output analyzer development;
- host-controlled real-time test endpoints.

It does **not** make the current one-port board a complete transparent inline precision instrument. The precision hardware roadmap therefore remains multi-port, timestamp-centric and capable of adding a deterministic network/TAP front-end.

## 8. Product positioning derived from the research

> **Open real-time IEC 61850 instrumentation — SCL-driven execution, hardware-timestamped measurement, deterministic injection, and reproducible evidence.**

Where ARStack should aim to be unusually strong:

- transparent evidence and reproducibility;
- open protocol/runtime layers;
- deterministic RTOS design visible to reviewers;
- clear separation of hardware timing vs host-capture timing;
- SCL expected-vs-observed comparison;
- profile claims backed by explicit evidence;
- low-cost development hardware that can graduate into precision hardware;
- automation-friendly machine-readable reports;
- hardware CI/regression testing;
- a shared core for injector and analyzer.

## 9. Repository policy

This repository intentionally avoids commercial brand/product comparison tables, competitor names, screenshots, logos, and vendor marketing links. External research may inform design decisions, but the public repository records only vendor-neutral engineering principles and primary standards references.

Revisit the standards and industry principles periodically. This document is directional architecture research, not a market-comparison document.