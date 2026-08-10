# ARStack61850 reference product architecture

This document defines the near-term reference products and the long-term architecture direction for ARStack61850.

The project goal is not to build one board-specific demo. The goal is a smart, portable IEC 61850 stack whose protocol core, deterministic wire behavior, configuration model, QA evidence, and application-facing APIs remain reusable across desktop systems, ESP32-class MCUs, and later industrial MCU/MPU targets.

## Design rule: one core, multiple products

The same portable C++ protocol core must serve all reference products.

Platform-specific code is restricted to small HAL/adaptation layers for:

- monotonic/UTC clocks;
- raw Ethernet transmit/receive;
- TCP byte streams/listeners;
- GPIO/relay/digital-I/O access;
- persistent configuration storage;
- task/thread scheduling;
- optional USB/serial, Wi-Fi, RS485, and Ethernet management transports.

Application code may select different protocol subsets and capacities, but it must not fork or reimplement IEC 61850 wire codecs per board.

## Short-term reference topology

```text
                         ARStack61850 PC Workbench
                    Windows CLI first, GUI/workbench later
                                   |
                    management/control protocol
                     USB serial / TCP / Wi-Fi
                 +-----------------+-----------------+
                 |                                   |
                 v                                   v
       ESP32-P4 Process-Bus Injector         ESP32-S3 I/O IED
       Waveshare ESP32-P4-ETH                Waveshare Relay-6CH
                 |                                   |
          wired Ethernet                         6 relay outputs
                 |                               isolated RS485
                 v                                   |
        IEC 61850 process bus                 physical I/O / sensors
       SV first, then GOOSE
                 |
                 +-------> Wireshark / test IED / future S3 wired IED
```

The PC is the engineering/control plane. The P4 is the deterministic process-bus traffic engine. The S3 is the I/O/IED execution node. The portable C++ core remains common to all three.

## Product A — Windows reference CLI / workbench

### First role

Provide a hardware-independent reference environment for the exact same stack used by embedded targets.

Initial executable direction:

```text
ariec61850_sv_loopback.exe
```

Required modes:

1. `software` — publisher -> in-memory raw-Ethernet adapter -> decoder/verifier;
2. `pcap` — publisher -> PCAP -> reopen/independent verification;
3. `live-npcap` — later, raw Layer-2 transmit/capture through a selected Windows Ethernet adapter;
4. `device-control` — later, configure and control the P4 injector and S3 IED.

Required test/fault modes should grow to include normal stream, drop, duplicate, corruption, late scheduling, counter wrap, rate change, dataset/payload profiles, and replayable scenarios.

The Windows implementation is a reference/oracle and engineering tool, not a separate protocol implementation.

## Product B — ESP32-P4 process-bus injector

### Reference hardware

Waveshare ESP32-P4-ETH.

### Primary role

A PC-controlled IEC 61850 process-bus injector/generator for laboratory, commissioning-assistance, interoperability, regression, and education use.

### First accepted slice

- native ESP32-P4 EMAC/RMII;
- ESP-IDF as primary platform;
- Layer-2 Sampled Values publisher;
- first profile: 50 Hz / 80 samples-per-cycle / 4,000 sample instants per second;
- deterministic synthetic 8-channel payload;
- untagged first proof;
- APPID/MAC/svID configurable at application level;
- caller-owned frame buffers;
- no publisher-generated catch-up burst;
- CI-produced flashable firmware.

### Injector evolution

The P4 should become a scenario-driven engine rather than a hard-coded publisher.

Planned capabilities:

- SV waveform/scenario profiles;
- magnitude, phase, frequency, harmonic, DC offset and transient scripting;
- COMTRADE-to-SV playback;
- configurable APPID, svID, destination MAC, VLAN and sample profile;
- controlled drop/duplicate/corrupt/jitter fault injection;
- GOOSE publisher and subscriber test modes;
- PCAP replay/compare workflows where technically appropriate;
- start/stop/arm/trigger commands from the PC;
- runtime telemetry for frame counts, encode/TX failures, missed/coalesced ticks, lateness and heap watermark;
- synchronized triggering and PTP/time synchronization only after separate timing evidence exists.

### Control-plane rule

The management/control plane must be separated conceptually from the process-bus data plane.

For early development, USB serial is preferred for injector commands because it cannot consume or perturb the Ethernet process-bus path. TCP or other management transports can be added later behind the same command model.

No control-plane transport is allowed to change IEC 61850 process-bus wire behavior in the portable core.

## Product C — ESP32-S3 I/O IED

### First reference hardware

Waveshare ESP32-S3-Relay-6CH, SKU 26756.

The board provides six relay outputs, isolated RS485, Wi-Fi/Bluetooth, USB, Pico-compatible expansion, wide-range industrial DC input, and isolation/protection around the relay/control sections.

The current 6CH board does not provide onboard wired Ethernet. Therefore its first IEC 61850 proof is deliberately separated into two stages.

### Stage C1 — static MMS I/O IED over IP/Wi-Fi

Use ESP-IDF and the portable TCP/MMS/server subset to prove a real I/O-backed IED model without waiting for a wired-Ethernet expansion.

Initial surface:

- static compile-time IED model;
- MMS TCP server;
- TPKT/COTP/session/presentation/ACSE association;
- MMS Initiate;
- `GetNameList`;
- `GetVariableAccessAttributes`;
- `Read`;
- bounded, explicitly authorized control/write path for relay outputs;
- six physical relay channels mapped through a small static I/O data model, initially GGIO-style generic I/O unless a more specific application model is intentionally selected;
- physical output state readback where electrically/firmware-available;
- local safety defaults: outputs OFF after invalid configuration/recovery unless a documented application policy says otherwise;
- PC CLI interoperability tests and reproducible CI server simulations.

Wi-Fi is acceptable for this functional MMS/server proof. It is not used as evidence of deterministic station-bus or protection-grade GOOSE behavior.

### Stage C2 — wired Ethernet IED

For station-bus/GOOSE work, add a supported wired Ethernet path through the network HAL.

Candidate directions:

- external SPI Ethernet controller such as W5500 if a clean, electrically suitable expansion is selected for the Relay-6CH platform; or
- a Waveshare ESP32-S3 industrial Ethernet I/O variant with onboard W5500 for the wired reference build.

The protocol core and IED model must remain unchanged when switching from Wi-Fi/TCP proof to wired Ethernet. Only the network and board-I/O adapters should change.

### IED evolution

After the static read/control IED is stable:

- DataSet support;
- URCB reporting;
- bounded GOOSE publisher/subscriber;
- command/control model refinement;
- input expansion and status/event timestamps;
- RS485/Modbus gateway points where useful;
- SCL-driven static model generation;
- BRCB after buffering/reservation semantics are proven;
- persistent configuration with versioned schema and safe rollback.

## Shared PC-to-device control model

The P4 injector and S3 IED should expose one versioned management model to the PC instead of ad-hoc command strings per firmware.

The command model should be transport-independent and support at least:

- device identity/capabilities;
- firmware/core versions;
- get/set configuration;
- start/stop/arm/trigger;
- scenario/profile selection;
- runtime statistics;
- fault injection controls where applicable;
- health/diagnostics;
- structured error responses;
- configuration export/import;
- explicit protocol-version negotiation.

Early transport may be line-delimited JSON over USB serial for development speed. The schema should be defined independently so the same commands can later travel over TCP/WebSocket/USB without changing device behavior.

## QA ladder

Every feature should advance through explicit evidence levels.

### Q0 — pure portable unit/regression

- codecs;
- state machines;
- capacity/error paths;
- golden vectors;
- sanitizers/fuzzing where applicable.

### Q1 — host-system simulation

- Windows/Linux executable;
- deterministic virtual clock;
- in-memory transport;
- independent wire oracle;
- PCAP artifacts;
- injected drops/duplicates/corruption/late events.

### Q2 — target cross-compile

- real ESP-IDF target compiler/linker;
- board application links;
- image/footprint report;
- flash manifest produced.

### Q3 — flashable artifact

- complete CI artifact;
- checksums;
- bootloader/partition/app images;
- board/profile metadata;
- flashing instructions.

### Q4 — physical functional acceptance

- board boots;
- required interfaces operate;
- packets/associations observed by another device;
- I/O actually changes/readbacks;
- short soak accepted.

### Q5 — physical timing/interoperability acceptance

- sustained traffic;
- jitter/drop evidence;
- multi-vendor or independent-tool interoperability;
- long soak;
- synchronization evidence where claimed.

No software-only or cross-compile result may be promoted to Q4/Q5 language.

## Long-term target — smart portable IEC 61850 platform

The long-term product is a coherent IEC 61850 platform, not a collection of protocol demos.

Target capability families:

### Process bus

- SV publisher/subscriber;
- GOOSE publisher/subscriber;
- VLAN/priority;
- stream supervision;
- scenario/replay tooling;
- deterministic embedded runtimes;
- synchronization/PTP integration on hardware that can support it properly.

### Station bus / MMS

- client and server association;
- discovery and typed data model;
- read/write/control;
- DataSets;
- URCB/BRCB reporting;
- file service;
- robust reconnect/recovery;
- authorization/safety boundaries.

### Engineering

- SCL parse/validate/edit/generate;
- static embedded-model generation from SCL;
- COMTRADE integration;
- PCAP evidence/analysis;
- live discovery and parity checking;
- deterministic simulator/virtual IED;
- engineering workbench.

### Portable runtime profiles

- desktop Windows/Linux;
- ESP32-P4 process-bus profile;
- ESP32-S3 I/O IED profile;
- later STM32/NXP/other industrial MCU targets;
- future MPU/Linux target if large dynamic models or heavier engineering services require it.

## Quality bar

"Portable" means the IEC 61850 core does not know whether it runs on WinSock, lwIP, ESP-IDF, W5500, or another vendor HAL.

"Smart" means configuration, model generation, scenario generation, diagnostics, evidence collection, and interoperability workflows reduce engineering effort while remaining deterministic and inspectable.

"Top global" is treated as an engineering quality target, not a marketing claim. The evidence required includes clean architecture, standards-aware behavior, bounded resource use, deterministic tests, fuzz/sanitizer coverage, reproducible firmware, physical interoperability, timing measurements, documentation, long-soak results, and eventually independent/certification evidence for any conformance level that is claimed.
