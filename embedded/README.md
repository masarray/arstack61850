# arstack61850 Embedded Profile

This directory defines the MCU portability boundary for arstack61850.

## Reference target

The first reference MCU is **ESP32-class hardware**. The initial product goals are intentionally small:

1. a simple IEC 61850 I/O IED, using MMS over TCP plus a bounded static model; or
2. an IEC 61850-9-2 Sampled Values publisher using raw Layer-2 Ethernet.

STM32/NXP-class industrial MCUs can be added later without changing the protocol engine boundary.

## Architecture rule

`embedded_core` contains wire protocol and deterministic protocol-state code only. It must not depend on host tooling.

Allowed direction:

```text
application / ESP-IDF / FreeRTOS / lwIP / Ethernet HAL
                         |
                         v
                 ARIEC61850::embedded_core
                         |
        +----------------+----------------+
        |                |                |
      MMS/OSI          GOOSE             SV
        |                |                |
        +----------------+----------------+
                         |
                     BER/Ethernet
```

Host-only facilities remain outside this build profile:

- native Windows/Linux TCP transport;
- live discovery CLI;
- JSON/manifest output;
- PCAP capture/evidence tooling;
- COMTRADE;
- SCL XML parsing;
- filesystem access;
- desktop diagnostics/UI.

## Current compiler contract

The embedded profile currently enforces:

- C++20;
- no RTTI by default (`-fno-rtti` or `/GR-`);
- warnings as errors;
- full-link smoke of every source in the embedded source list;
- no host-only source in the embedded target.

`-fno-exceptions` is **not yet enabled**. Existing BER/MMS malformed-input paths still use C++ exceptions. This is explicit technical debt, not an accepted final state. New embedded work should not add new exception-dependent architecture when a bounded result/status API is practical.

The next embedded-hardening milestone is to introduce non-throwing result APIs for the wire codecs, migrate the ESP32-facing path to them, and then turn `ARIEC61850_EMBEDDED_NO_EXCEPTIONS=ON` into a CI requirement.

## Memory and allocation direction

The final MCU path should be deterministic. New embedded APIs should prefer:

- `std::span` for caller-owned buffers;
- `std::array` for fixed-capacity storage;
- explicit bounded capacities;
- status/result returns for parse failures;
- compile-time feature slicing;
- no filesystem/iostream dependency;
- no uncontrolled heap growth in steady-state protocol loops.

Existing `std::vector`/`std::string` based codecs are retained during migration for behavioral parity with ARIEC61850. They are migration inputs, not the final MCU allocation model.

## ESP32 deployment notes

MMS IED operation needs TCP/IP, so the future HAL will bind the OSI/MMS runtime to the platform TCP stack (for example lwIP through ESP-IDF).

GOOSE and Sampled Values are raw Ethernet Layer-2 services. The platform integration therefore needs a raw Ethernet transmit/receive path; Wi-Fi/TCP sockets are not substitutes for SV/GOOSE framing.

No ESP32-specific network driver is part of `embedded_core`. Platform code owns clocks, Ethernet/TCP I/O, tasks, synchronization and persistent configuration.

## Host-sim build

From the repository root:

```bash
cmake -S embedded -B build-embedded
cmake --build build-embedded --parallel
ctest --test-dir build-embedded --output-on-failure
```

This is a portability gate, not a claim that the stack already runs on ESP32 hardware.
