# ESP32-first embedded roadmap

The embedded roadmap is product-driven. The first hardware proofs are deliberately small, measurable, and reusable as foundations for a portable IEC 61850 product family.

See [`docs/REFERENCE_PRODUCT_ARCHITECTURE.md`](../docs/REFERENCE_PRODUCT_ARCHITECTURE.md) for the reference-product topology and long-term architecture.

## Reference products

Short-term roles are fixed as:

- **Windows PC** — reference CLI/workbench, loopback QA, scenario/configuration controller, evidence collection;
- **ESP32-P4 / Waveshare ESP32-P4-ETH** — IEC 61850 process-bus injector, SV first and GOOSE next;
- **ESP32-S3 / Waveshare ESP32-S3-Relay-6CH** — static I/O IED, six physical relay outputs and isolated RS485, MMS/IP first and wired station-bus/GOOSE after a supported Ethernet path is added.

The protocol core must remain common across these products. Board-specific code belongs only in HAL/platform/application adapters.

## E0 — portability boundary

Status: accepted for the current embedded slice, with hard-profile work still remaining in E5.

Accepted:

- standalone `embedded/` CMake build;
- GCC and Clang host-sim compile;
- RTTI disabled in the embedded profile;
- host TCP/filesystem/capture services excluded from the first MCU component boundary;
- all selected embedded objects participate in a full-link smoke executable;
- desktop C++ CI remains independent;
- platform-neutral raw-Ethernet/clock abstraction exists;
- ESP-IDF component boundary exists.

Still deferred to the hard embedded profile:

- global `-fno-exceptions` acceptance;
- removal/splitting of remaining exception-dependent convenience/decode APIs from the MCU slice;
- explicit footprint budgets for every future embedded protocol subset.

## E1 — allocation-bounded SV wire path

Status: accepted for the first-trial publisher slice.

Accepted:

- caller-owned Ethernet frame buffer;
- allocation-bounded steady-state SV encode/transmit path;
- no exception propagation from the publisher hot path;
- existing host API retained;
- VLAN/non-VLAN framing remains explicit;
- sample counter wrap behavior tested;
- deterministic 8,000-frame / two-second simulation at 4 kHz;
- exact-byte independent Python PCAP oracle;
- GCC + Clang embedded-host simulation evidence retained by CI.

Semantic follow-up before mature process-bus acceptance:

- finalize and document `smpCnt` progression when a transmit fails or nominal sample instants are intentionally skipped after scheduler lateness.

## E2 — Windows SV loopback reference CLI

Status: next implementation slice.

Initial executable direction:

```text
ariec61850_sv_loopback.exe
```

Required first modes:

1. `software` — `SampledValuesPublisher` -> in-memory `RawEthernetPort` -> decoder/verifier;
2. `pcap` — generated stream -> PCAP -> reopen/independent verification;
3. machine-readable JSON summary plus human console output.

Fault/regression modes should then cover:

- drop;
- duplicate;
- corrupt;
- late poll/tick;
- counter wrap;
- rate/profile change.

Later add `live-npcap` raw Layer-2 transmit/capture through a selected Windows Ethernet adapter.

This CLI becomes both a pre-hardware QA tool and the desktop behavioral reference against which the ESP32-P4 injector is compared.

## E3 — ESP32-P4 SV injector MVP

Status: software/cross-compile/flashable gates accepted; physical Ethernet acceptance pending.

Platform layer:

- ESP-IDF/FreeRTOS owns task scheduling and Ethernet driver;
- `RawEthernetPort` binds to `esp_eth_transmit()`;
- fixed frame buffers are application-owned;
- sample source is pluggable;
- management plane is conceptually separated from the process-bus data plane.

Accepted before hardware:

- real ESP32-P4 ESP-IDF v6.0.2 cross-compile/link;
- flashable firmware image produced by CI;
- ESP-IDF flasher manifest targets `esp32p4`;
- bootloader, partition table, application image, ELF/map, checksums and build-profile evidence retained;
- first synthetic SV profile is reproducible by host simulation.

Physical minimum demonstration:

1. configure destination MAC, APPID and svID;
2. initialize native ESP32-P4 EMAC/RMII and PHY;
3. generate deterministic sampled values;
4. publish valid IEC 61850 SV Layer-2 frames;
5. decode them with Wireshark/desktop tools;
6. capture PCAP and compare fields against the host golden path;
7. measure sustained 4,000-frame/s behavior, TX failures, missed/coalesced ticks, lateness, and heap watermark;
8. short, 10-minute, then one-hour soak.

A common 50 Hz / 80 samples-per-cycle profile implies 4,000 sample instants per second. That is the first stress target, not a blanket standards claim for every SV application.

### P4 injector evolution

After physical SV acceptance:

- PC-controlled start/stop/arm/trigger;
- versioned configuration/scenario protocol, USB serial first;
- configurable SV identities/profile/VLAN;
- waveform and transient scenarios;
- COMTRADE-to-SV playback;
- controlled drop/duplicate/corrupt/jitter injection;
- GOOSE publisher/subscriber laboratory modes;
- PCAP compare/replay workflows;
- synchronized triggering and PTP only after separate hardware/timing evidence exists.

## E4 — ESP32-S3 I/O IED MVP

Status: planned after the Windows loopback slice and P4 first physical proof are stable enough to serve as test peers.

First hardware:

- Waveshare ESP32-S3-Relay-6CH, SKU 26756;
- six physical relay outputs;
- isolated RS485;
- Wi-Fi/Bluetooth/USB management capability;
- industrial DC input and isolation/protection hardware.

The Relay-6CH board has no onboard wired Ethernet, so development is split deliberately.

### E4.1 — MMS/static IED over IP/Wi-Fi

Initial server surface:

- TCP/TPKT/COTP/session/presentation/ACSE server-side accept path;
- MMS initiate;
- `GetNameList`;
- `GetVariableAccessAttributes`;
- `Read`;
- compile-time/static Logical Device / Logical Node / Data Object table;
- six relay outputs exposed through a small static GGIO-style I/O model;
- bounded, explicitly authorized relay control/write path;
- safe startup/recovery output policy;
- PC CLI interoperability and host-sim regression.

Wi-Fi proves MMS/application behavior but is not evidence for deterministic station-bus or protection-grade GOOSE.

### E4.2 — wired Ethernet IED

Add a supported wired network adapter behind the same portable HAL, for example a suitable SPI Ethernet controller, or use a Waveshare S3 industrial Ethernet I/O variant with onboard W5500 as the wired reference platform.

Then add, in controlled slices:

- one small static DataSet;
- URCB reporting;
- bounded GOOSE publisher/subscriber;
- refined control model;
- input/status/event timestamp expansion;
- RS485/Modbus gateway points where useful;
- SCL-driven static model generation;
- BRCB only after reservation/buffering semantics are proven.

Dynamic DataSet and large dynamic discovery structures are not initial MCU requirements.

## E5 — shared PC-to-device control plane

Status: planned.

The P4 injector and S3 IED must not invent unrelated ad-hoc command protocols.

Define one versioned, transport-independent management schema for:

- identity and capabilities;
- stack/firmware version;
- configuration get/set/export/import;
- start/stop/arm/trigger;
- scenario/profile selection;
- runtime statistics and health;
- explicit structured errors;
- fault injection where applicable;
- protocol-version negotiation.

Early development transport may be line-delimited JSON over USB serial. Later transports may include TCP/WebSocket without changing command semantics.

## E6 — hard embedded profile

Acceptance:

- `-fno-exceptions` enabled for the intended embedded protocol slices;
- `-fno-rtti` remains enabled;
- no uncontrolled heap use in protocol steady state;
- explicit compile-time capacities;
- platform clock/TCP/raw-Ethernet/I/O HAL only;
- ESP-IDF target builds in CI;
- per-profile RAM/flash/stack footprint reports;
- long-soak resource stability;
- fault/recovery behavior documented and regression-tested.

## E7 — industrial portability targets

After ESP32 proofs are stable, port only HAL/platform integration to additional hardware.

Candidate families include STM32, NXP and other industrial MCU/MPU platforms selected according to Ethernet MAC/DMA capability, timer determinism, memory, isolation, watchdog/recovery, synchronization/PTP requirements, lifecycle, and toolchain quality.

For higher-end protection/process-bus ambitions, hardware is selected to satisfy the timing/conformance goal. The IEC 61850 core must not be rewritten around a vendor SDK.

## Long-term outcome

ARStack61850 should converge into one coherent, smart, portable IEC 61850 platform spanning:

- SV and GOOSE process bus;
- MMS client/server, discovery, reporting, control and file service;
- SCL engineering and static embedded-model generation;
- COMTRADE and PCAP workflows;
- deterministic simulator/virtual IED;
- Windows/Linux engineering workbench;
- constrained MCU profiles and larger industrial targets;
- reproducible QA from host simulation through physical interoperability and timing evidence.

"Top global" is treated as an engineering quality target: clean architecture, standards-aware behavior, deterministic tests, bounded resources, fuzz/sanitizer coverage, reproducible artifacts, physical interoperability, timing evidence, long-soak results, clear documentation, and independent/certification evidence for any conformance level that is eventually claimed.
