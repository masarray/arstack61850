# ARStack61850 Embedded Runtime Roadmap

## Product direction

ARStack61850 is a portable IEC 61850 instrumentation stack with two complementary deployment classes:

1. **Host / engineering** — Windows and Linux tools for SCL engineering, live discovery, evidence, diagnostics, waveform/scenario configuration, capture integration, and interoperability workflows.
2. **Embedded / realtime runtime** — bounded protocol execution on dedicated hardware for deterministic SV/GOOSE publication, capture, timing evidence, and future synchronized operation.

The host is the engineering brain. Dedicated hardware is the realtime packet and timing authority.

## Process-bus hardware principles

The embedded process-bus path requires a real Ethernet Layer-2 interface. Wireless networking is not a substitute for canonical SV/GOOSE transmission.

Reference platforms should provide, as applicable:

- raw Ethernet frame transmission and reception;
- deterministic interrupt/DMA behavior;
- hardware-assisted timestamps where available;
- sufficient RAM for bounded packet/profile/capture buffers;
- a management/control path that does not become the sample-timing source;
- electrical isolation appropriate to the intended laboratory or field role.

Board-specific pin maps, commercial product identities, and marketing specifications do not belong in the public architecture roadmap. Platform adapters keep those details outside the protocol core.

## Non-negotiable architecture rules

### Embedded core

The embedded core may contain wire codecs, immutable compiled profiles, bounded state machines, and realtime signal/runtime primitives, but it must not depend on:

- desktop socket APIs;
- filesystem or interactive CLI workflows;
- full XML/SCL parsing;
- PCAP/evidence report generation;
- desktop UI frameworks;
- host-only discovery and engineering workflows.

Platform ownership is injected through small HAL/adaptor contracts. RTOS, MAC/PHY, DMA, timestamp, USB/management, and board details remain outside the IEC 61850 semantic core.

### SCL/profile boundary

XML and vendor-specific engineering-file dialect handling stay on the host:

```text
SCL / CID / SCD / IID
        |
        v
smart tolerant host parser
        |
        v
validated immutable profile
        |
        v
embedded runtime
```

The host may normalize representation differences, preserve provenance, quarantine irrelevant extensions, and explain conflicts. Deployment remains fail-closed when mandatory wire semantics are unresolved.

### Realtime boundary

The realtime publisher must not parse XML, rebuild BER per sample, allocate on the hot path, or depend on Windows/Linux scheduling.

Preferred execution model:

```text
validated profile + mutable signal state
                |
                v
       prebuilt packet template
                |
                v
        deterministic scheduler
                |
                v
          Ethernet MAC/DMA
                |
                v
          timing evidence
```

Signal values may change coherently while running. Stream identity/layout changes require STOP -> validate -> arm.

### Memory discipline

Steady-state publisher/subscriber paths should use:

- preallocated or caller-owned buffers;
- bounded capacities;
- no per-frame heap allocation;
- no unbounded queues;
- explicit overflow/drop accounting;
- fixed or bounded metadata structures;
- predictable CPU ownership.

Host convenience models may use dynamic containers at configuration time as long as they do not leak allocation requirements into the realtime path.

## Timing truth hierarchy

Timing claims should progress through increasingly authoritative evidence:

1. sample deadline;
2. realtime task wake;
3. MAC transmit/receive hardware timestamp where supported;
4. independent measurement-point timestamp.

Host arrival time is useful diagnostic information but must not be treated as canonical wire timing evidence.

## Synchronization direction

A future synchronized runtime should separate:

- wire protocol parsing;
- passive synchronization monitoring;
- applicable profile validation;
- hardware clock access;
- offset/delay estimation and servo;
- lock / acquiring / holdover states;
- sample-synchronization field policy;
- timestamp evidence.

Presence of synchronization traffic alone is never proof of lock.

## Process-bus capture direction

The receive side should converge on the same realtime/evidence core used by the publisher:

- promiscuous raw receive;
- hardware timestamps where available;
- bounded capture rings;
- sequence/gap/duplicate/out-of-order accounting;
- SCL expected-vs-observed comparison;
- waveform, RMS, frequency, and phasor reconstruction;
- triggers with bounded pre/post capture;
- evidence export handled by the host.

## Hardware evolution

The current single-port MCU-class reference platform is suitable for proving deterministic publication, receive instrumentation, control-plane separation, and timestamp architecture.

A later precision platform should be specified from measured needs rather than assumptions. Potential requirements include:

- multiple independent process-side physical paths;
- separate management path;
- copper and/or optical interfaces as required by deployment;
- timestamping close to the measurement point;
- disciplined oscillator/clock architecture;
- external switching, tapping, or programmable logic only where measured throughput/timing requirements justify it.

## Delivery order

1. deterministic SV publisher foundation;
2. hardware timing evidence;
3. smart SCL-to-profile compiler;
4. immutable device-profile deployment;
5. live engineering-value control through the GUI;
6. broad interoperability matrix and observed-wire resolver;
7. synchronized clock/profile support;
8. hardware-timestamped process-bus analyzer;
9. precision multi-port hardware when evidence requires it.

## Public repository rule

Public project-owned content remains vendor-neutral. Research findings from commercial implementations may inform engineering principles privately, but public architecture should contain standards, generic engineering patterns, reproducible measurements, and project-owned evidence rather than competitor/product references or UI imitation.
