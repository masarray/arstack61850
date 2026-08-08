# arstack61850 — Embedded / ESP32 Roadmap

## Product direction

arstack61850 is not intended to become a line-by-line C++ clone of ARIEC61850.
ARIEC61850 remains the behavioral oracle. arstack61850 becomes the portable
industrial implementation with two supported deployment classes:

1. **Host / engineering** — Windows and Linux tools, live MMS discovery,
   evidence, diagnostics, SCL/PCAP/COMTRADE integration.
2. **Embedded / runtime** — bounded protocol engine suitable for ESP32-class
   MCUs first and STM32/NXP-class targets later.

The **first process-bus transmit reference is now ESP32-P4** using the official
ESP-IDF Ethernet abstraction and a board-specific EMAC/PHY configuration owned
by the application. The previously selected **Waveshare ESP32-S3-POE-ETH-8DI-8DO**
remains a useful secondary I/O reference with W5500 Ethernet, but protocol code
must not depend on either board.

## Reference hardware roles

### ESP32-P4 — first SV timing / raw-Ethernet proof

Use ESP32-P4 for the first active Sampled Values trial because it provides a
stronger MCU target for deterministic process-bus experimentation while still
using the same `esp_eth` adapter boundary.

The protocol stack must receive an already-started `esp_eth_handle_t`; board PHY
selection, RMII pins, clocking and link initialization remain application code.
The first proof is deliberately **untagged SV**. VLAN/PCP acceptance is a later
hardware/driver gate rather than a hidden protocol-core assumption.

### Waveshare ESP32-S3-POE-ETH-8DI-8DO — secondary I/O IED reference

Board: Waveshare ESP32-S3-POE-ETH-8DI-8DO, SKU 32108.

- ESP32-S3-WROOM-1U-N16R8: 16 MB Flash, 8 MB PSRAM.
- W5500 10/100 Ethernet controller over SPI.
- PoE module, IEEE 802.3af.
- Eight isolated digital inputs and eight isolated digital outputs.
- W5500 pins: INT=GPIO12, MOSI=13, MISO=14, SCLK=15, CS=16, RST=39.
- DI1..DI8: GPIO4..GPIO11.
- Digital outputs are exposed through TCA9554PWR, I2C address 0x20.
- Isolated RS485 and CAN are available for future gateway use.

When this board is used, the official ESP-IDF W5500 driver should own the chip
MACRAW path. arstack should not independently program W5500 hardware sockets.
Raw SV/GOOSE and lwIP TCP/MMS must share one Ethernet interface through platform
adapters.

Raw Layer-2 SV/GOOSE is an Ethernet feature. Wi-Fi is not a substitute for the
reference process-bus path.

## Non-negotiable architecture rules

### Embedded core

`ARIEC61850::embedded_core` and platform slices may contain wire codecs and
bounded runtime state, but must not make the wire protocol depend on:

- WinSock or BSD/POSIX sockets;
- filesystem, iostream or CLI code;
- PCAP/evidence tooling;
- COMTRADE;
- full SCL parsing;
- live-discovery host workflows;
- ESP-IDF, FreeRTOS, W5500, RMII or a specific PHY type.

Platform ownership is injected through small callback HAL contracts. ESP-IDF,
lwIP, FreeRTOS and board details stay outside the protocol engine.

### Memory discipline

Steady-state publisher/subscriber/runtime paths move toward:

- caller-owned buffers;
- bounded capacities;
- no per-frame allocation;
- no RTTI;
- no exception control flow in real-time paths;
- eventually a fully `-fno-exceptions` MCU build.

Existing `std::vector`/`std::string` data models may remain as configuration-time
containers during migration. They must not force allocation on the 4 kHz SV
steady-state encode/transmit path.

### Compatibility

Existing host APIs remain source-compatible where practical. Allocation-bounded
APIs are added underneath host convenience wrappers, so Windows/Linux users do
not lose ergonomics while the same codec becomes MCU-capable.

---

# Delivery plan

## Track A — Public Alpha

### A0. Live read-only MMS foundation — ACCEPTED ON CURRENT PRIMARY IED

Acceptance achieved on the current OCR7SR12 laboratory target includes:

- TPKT/COTP/Session/Presentation/ACSE association;
- GetNameList domains, variables and DataSets;
- GetVariableAccessAttributes;
- Read;
- DataSet and RCB inventories;
- dynamic/unbound RCB state without false failure;
- read-only RCB planner/availability evidence;
- bounded multi-page `GetNameList` continuation, including a physical 48-page
  / 4,758-name sequence;
- GCC, Clang and MSVC regression coverage.

Multi-vendor evidence remains separate from this one-target acceptance.

### A1. Public discovery/model alpha

Goal: a third-party engineer can clone the repository and use it without
knowing the C# project.

Deliverables:

- stable `live_discover` CLI contract;
- documented JSON/model schema;
- deterministic structural fingerprint separated from runtime evidence;
- TypeTemplates + VariableTypeDiscoveries parity slice;
- same-IED C# versus C++ model comparison fixture;
- examples for association, read, DataSet and RCB inventory;
- README quick-start for Windows/Linux;
- explicit feature matrix: implemented / partial / host-only / embedded-ready.

### A2. Public alpha release

Required public surface:

- MMS client association + confirmed read services;
- read-only live model discovery;
- DataSet/RCB inspection;
- GOOSE encode/decode/runtime foundation;
- SV encode/decode foundation;
- documented portability boundary;
- examples and CI status.

Not release-blocking:

- full MMS file service;
- TLS;
- all dynamic DataSet mutation workflows;
- complete ARIEC61850 host-tool parity;
- full IED server implementation.

---

## Track E — ESP32 reference product

### E0. Embedded architecture baseline — IMPLEMENTED

Delivered:

- standalone `embedded/CMakeLists.txt`;
- `ARIEC61850::embedded_core`;
- no-RTTI host-simulation CI;
- source-boundary audit;
- callback HAL for raw Ethernet, connected TCP and clocks;
- configurable ESP32 small-capacity profile;
- ESP-IDF raw-Ethernet/clock adapter boundary.

Remaining hardening:

- remove the shared codec's compile-time C++ exception dependency from the
  MCU-specific build rather than merely avoiding exceptions in the hot path.

### E1. Allocation-bounded SV encoder — IMPLEMENTED

The first steady-state API direction is now implemented:

```cpp
std::optional<std::size_t> SampledValuesFrameCodec::encoded_size(...) noexcept;
wire::EncodeResult SampledValuesFrameCodec::encode_into(
    const SampledValuesFrame&,
    std::span<std::uint8_t> destination) noexcept;
```

The caller can reuse the same Ethernet buffer continuously. Host `encode()`
remains a convenience wrapper.

### E2. Portable SV publisher runtime — IMPLEMENTED / PHYSICAL EVIDENCE PENDING

`SampledValuesPublisher` now owns only protocol runtime state. It does not create
threads or sleep. The application provides the monotonic timestamp and decides
whether a FreeRTOS task or high-resolution timer drives the stream.

Implemented behavior:

- 4,000 samples/s first profile;
- caller-owned Ethernet frame buffer;
- sample counter advances after successful full-frame transmit only;
- configured sample-counter wrap;
- at most one transmit attempt per `poll()`;
- late scheduler wake-up does not generate catch-up bursts;
- TX/encode/lateness statistics;
- allocation-free payload writer for common INT32 + quality pairs.

Host smoke acceptance covers due/not-due behavior, 3999→0 wrap, late wake-up,
TX timeout and encode buffer failure.

### E3. ESP32-P4 SV Publisher hardware proof — CURRENT

Reference first profile:

- 50 Hz system;
- 80 samples/cycle = 4,000 sample instants/s;
- one ASDU/frame initially;
- EtherType `0x88BA`;
- APPID `0x4001`;
- multicast destination `01:0C:CD:04:00:01`;
- source MAC from the physical interface;
- `confRev=1` for the run;
- `smpSynch=0` until synchronized time is independently proven;
- no `refrTm` for the first throughput proof;
- untagged Ethernet first.

Cross-compile gate:

- real ESP32-P4 target;
- ESP-IDF v6.0.2;
- firmware component contains the first SV TX dependency slice only;
- current transition enables `CONFIG_COMPILER_CXX_EXCEPTIONS=y` because shared
  desktop validation/convenience APIs still contain `throw/catch`; publisher hot
  path must not use exception control flow.

Physical acceptance:

- raw SV frame visible with `eth.type == 0x88ba`;
- exact destination/source MAC and APPID;
- Wireshark decodes one ASDU correctly;
- sustained 4,000 frames/s target or an explicitly documented alternative;
- no publisher-generated sample-counter discontinuity;
- no catch-up burst after scheduler lateness;
- no steady-state frame-buffer allocation;
- short capture, 10-minute soak, then 1-hour soak;
- CPU usage, heap delta/minimum free heap, TX errors and lateness statistics
  recorded.

See `docs/ESP32_P4_SV_FIRST_TRIAL.md` for the execution checklist.

### E4. Exception-free MCU SV TX slice — NEXT HARDENING GATE

Goal: build the first SV publisher firmware with C++ exceptions disabled again,
without changing desktop convenience APIs.

Preferred direction:

- separate status-returning/no-throw transmit codec implementation units from
  throw-based desktop wrappers where necessary;
- add non-throwing MAC/config construction paths suitable for static firmware
  configuration;
- keep decode/diagnostic convenience APIs host-side when they are not required
  by the first publisher image;
- CI with ESP32-P4 and `CONFIG_COMPILER_CXX_EXCEPTIONS=n`.

Acceptance:

- ESP-IDF P4 cross-compile green with exceptions disabled;
- no `throw`/`catch` reachable or required by the MCU SV TX component;
- publisher golden bytes remain unchanged;
- host GCC/Clang/MSVC API/regression suite remains green.

### E5. Board I/O HAL — Waveshare ESP32-S3 secondary reference

Goal: make the Waveshare hardware useful as a real substation I/O prototype.

Inputs:

- DI1..DI8 from GPIO4..GPIO11;
- debounce/filter policy separated from raw input state;
- timestamped state-change queue.

Outputs:

- TCA9554PWR adapter at I2C address 0x20;
- EXIO1..EXIO8 mapping verified against schematic/vendor demo before hard-coding;
- safe startup state: outputs off unless application explicitly restores state;
- command watchdog/failsafe hooks.

### E6. Minimal MMS server foundation

This is required for an embedded board to become an actual IEC 61850 I/O IED.
Implement a static, bounded server model first. Do not port desktop dynamic model
discovery into the MCU server.

Minimum services:

1. association acceptor;
2. MMS Initiate negotiation;
3. GetNameList;
4. GetVariableAccessAttributes;
5. Read.

Initial static model proposal:

```text
ARSTACKIO
└── LD0
    ├── LLN0
    ├── LPHD1
    ├── GGIO1   digital inputs
    └── GGIO2   digital outputs/status
```

Acceptance:

- an independent IEC 61850 engineering client can associate;
- the client can browse the static model;
- configured I/O values can be read;
- server remains bounded under malformed/fuzzed MMS requests.

### E7. Simple IEC 61850 I/O IED

Add controlled output operation only after read-only server browse/read is stable.
Then add a static DataSet, URCB first, data-change reports and BRCB later.

### E8. GOOSE I/O extension

After raw MAC transport is proven by SV:

- DI state as GOOSE publisher;
- GOOSE subscriber mapped to selected DO only through application safety policy;
- retransmission schedule and TAL supervision;
- VLAN priority validation on a hardware/driver path that supports it correctly.

### E9. Industrialization / STM32-NXP migration

ESP32 is the accessibility/reference target, not a certification claim. The same
embedded core should later move to STM32H7/NXP-class hardware by replacing
platform adapters and capacities, not protocol semantics.

Industrialization work:

- fully exception-free MCU core;
- replace remaining hot-path dynamic containers with bounded storage/views;
- explicit memory budget report;
- deterministic scheduler/timing hooks;
- watchdog integration;
- stronger time synchronization strategy;
- long-duration soak and fault injection;
- multi-vendor interoperability campaign.

---

# Priority rule

Until Public Alpha is tagged, host parity and embedded architecture progress in
parallel. Embedded changes that prevent architectural debt — allocation-bounded
wire encoding, HAL boundaries, real cross-compile gates and real-time pacing
rules — are performed before adding new protocol surface.

The immediate embedded order is:

1. ESP32-P4 SV cross-compile;
2. physical untagged SV capture and timing evidence;
3. exception-free P4 SV TX hardening;
4. VLAN/priority path validation;
5. GOOSE raw Ethernet;
6. bounded MMS server / I/O IED.

# Definition of success

The project succeeds when the same protocol implementation can demonstrate all
of these without forks of IEC 61850 wire semantics:

1. Windows/Linux engineering client can browse real IEDs.
2. ESP32-P4 can publish standards-shaped SV frames through the ESP-IDF Ethernet
   abstraction with measured timing/heap evidence.
3. The MCU SV TX path can build with exceptions disabled.
4. A small ESP32 I/O reference can expose isolated inputs/outputs through a
   bounded IEC 61850 server model.
5. A later STM32/NXP port changes platform adapters and capacities, not protocol
   semantics.
