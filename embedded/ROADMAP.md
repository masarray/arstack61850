# ESP32-first embedded roadmap

The embedded roadmap is product-driven. The first hardware proof should be small enough to finish and measure.

## E0 — portability boundary

Status: in progress.

Acceptance:

- standalone `embedded/` CMake build;
- GCC and Clang host-sim compile;
- RTTI disabled;
- host TCP/PCAP/COMTRADE/SCL/CLI dependencies rejected by CI;
- all embedded objects participate in a full-link smoke executable;
- existing desktop C++ CI remains unchanged and green.

Not yet accepted:

- `-fno-exceptions`;
- deterministic/no-heap wire encoding;
- ESP-IDF compilation;
- real ESP32 Ethernet traffic.

## E1 — allocation-bounded SV wire path

This is the next protocol refactor because it directly enables the smallest useful ESP32 product.

Target API direction:

```cpp
EncodeResult encode_into(
    const SampledValuesFrame& frame,
    std::span<std::uint8_t> destination) noexcept;
```

Acceptance:

- caller-owned Ethernet frame buffer;
- no heap allocation on the steady-state SV encode/transmit path;
- no exceptions on that path;
- byte-for-byte parity with existing C++/ARIEC61850 golden SV vectors;
- existing `std::vector` host API retained as a compatibility wrapper;
- VLAN/non-VLAN framing remains explicit;
- sample counter wrap behavior remains tested.

## E2 — ESP32 SV Publisher MVP

Platform layer:

- ESP-IDF/FreeRTOS owns task scheduling and network driver;
- `RawEthernetPort` binds to the platform raw Ethernet TX function;
- fixed frame buffers are owned by the application;
- sample source is pluggable (synthetic first, ADC later).

Minimum demonstration:

1. configure destination MAC, APPID and svID;
2. generate deterministic sampled values;
3. publish valid IEC 61850 SV Layer-2 frames;
4. decode them with the desktop arstack61850 decoder and an independent IEC 61850 tool;
5. capture PCAP and compare payload fields against the host golden path;
6. run a sustained-rate test appropriate to the selected SV profile.

A common 50 Hz / 80 samples-per-cycle profile implies 4,000 sample instants per second. That is a useful stress target, not a blanket standards claim for every SV application.

Timing quality must be reported honestly. A lab ESP32 publisher is not called protection-grade or time-synchronized until jitter and synchronization/PTP evidence support that claim.

## E3 — simple ESP32 IEC 61850 I/O IED

Start with a deliberately static device model.

Initial server surface:

- TCP/TPKT/COTP/session/presentation/ACSE server-side accept path;
- MMS initiate;
- `GetNameList`;
- `GetVariableAccessAttributes`;
- `Read`;
- compile-time/static Logical Device / Logical Node / Data Object table;
- GPIO-backed SPS/DPS/measurement points.

Then add, in controlled slices:

- bounded `Write` for configuration points where appropriate;
- control model for outputs;
- one small static DataSet;
- URCB reporting first;
- BRCB only after reservation/buffering semantics are proven.

Dynamic DataSet and large live-discovery structures are not initial MCU requirements.

## E4 — hard embedded profile

Acceptance:

- `-fno-exceptions` enabled in CI;
- `-fno-rtti` remains enabled;
- no uncontrolled heap use in protocol steady state;
- explicit compile-time capacities;
- platform clock/TCP/raw-Ethernet HAL only;
- ESP-IDF build in CI or reproducible local toolchain build;
- RAM/flash footprint report.

## E5 — STM32/NXP industrial target

After the ESP32 proof is stable, port only the HAL and platform integration. The protocol core must remain unchanged.

For higher-end protection/process-bus ambitions, select hardware based on Ethernet MAC/DMA, timer determinism, memory, and synchronization/PTP requirements rather than rewriting the stack around a vendor SDK.
