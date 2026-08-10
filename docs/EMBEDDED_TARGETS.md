# Embedded Target Strategy

arstack61850 uses one portable protocol core across host and MCU deployments.
The embedded roadmap deliberately uses more than one board so platform details
cannot leak into IEC 61850 wire semantics.

## Reference roles

### ESP32-P4-ETH — protocol/performance reference

Primary embedded protocol target after Public Alpha.

Use it to prove:

- native 100 Mbps Ethernet integration;
- raw Layer-2 GOOSE and Sampled Values;
- allocation-bounded SV publisher loop;
- MMS server over the software TCP/IP stack;
- reporting runtime;
- timing/jitter instrumentation;
- IEEE 1588/PTP experiments where supported by the selected silicon/ESP-IDF;
- long-duration throughput and fault-injection tests.

PoE is not a development requirement. The low-cost non-PoE board is the
preferred first P4 target; power can be supplied separately during protocol
development.

### Waveshare ESP32-S3-POE-ETH-8DI-8DO — application/I/O reference

Existing real-I/O target used to prove that the same embedded core can become a
small field IED.

Use it to prove:

- 8 isolated digital inputs;
- 8 isolated digital outputs;
- static GGIO-based MMS server model;
- read-only browse/read first;
- controlled output operations only after server semantics are stable;
- static DataSet + URCB reporting for DI changes;
- GOOSE input publication and policy-controlled GOOSE-to-DO mapping;
- PoE deployment and power-cycle safe output behavior.

The W5500/ESP-IDF transport stays in a platform adapter. No W5500 API may enter
the portable protocol core.

### STM32H7 / NXP-class MCU — portability and industrialization target

This is intentionally later. Once the protocol engine is proven on ESP32-P4 and
S3, moving to another MCU should primarily require:

- Ethernet/TCP/raw-frame adapters;
- clocks and synchronization hooks;
- scheduler/RTOS integration;
- memory-capacity profile;
- board-specific I/O.

A protocol semantic rewrite for STM32/NXP would be considered an architecture
failure.

## Development priority

Before Public Alpha:

- approximately 70% effort: ARIEC61850 behavioral parity that makes the host
  C++ repository useful to third-party engineers;
- approximately 30% effort: embedded boundaries that prevent new desktop-only
  debt (bounded encoding, HAL separation, no-RTTI profile, dependency gates).

After Public Alpha:

1. ESP32-P4 raw Ethernet proof;
2. ESP32-P4 SV publisher proof and soak;
3. minimal bounded MMS server foundation;
4. ESP32-S3 8DI/8DO IED application;
5. GOOSE I/O extension;
6. STM32H7/NXP portability proof.

## Non-negotiable protocol boundary

The portable embedded core must not depend on ESP-IDF, FreeRTOS, lwIP,
W5500-specific APIs, STM32 HAL, filesystem, CLI, PCAP, COMTRADE, or host live
engineering workflows.

Platform-specific code provides small contracts for:

- raw Ethernet TX/RX;
- connected TCP byte streams or server socket adaptation;
- monotonic and UTC/PTP-aware clocks;
- optional synchronization/event hooks.

The protocol engine owns IEC 61850 semantics; the platform owns hardware.
