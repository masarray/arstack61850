# arstack61850 — Public Alpha + Waveshare ESP32-S3 Roadmap

## Product direction

arstack61850 is not intended to become a line-by-line C++ clone of ARIEC61850.
ARIEC61850 remains the behavioral oracle. arstack61850 becomes the portable
industrial implementation with two supported deployment classes:

1. **Host / engineering** — Windows and Linux tools, live MMS discovery,
   evidence, diagnostics, SCL/PCAP/COMTRADE integration.
2. **Embedded / runtime** — bounded protocol engine suitable for ESP32-class
   MCUs first and STM32/NXP-class targets later.

The first reference MCU is the **Waveshare ESP32-S3-POE-ETH-8DI-8DO**.

## Reference hardware facts

Board: Waveshare ESP32-S3-POE-ETH-8DI-8DO, SKU 32108.

- ESP32-S3-WROOM-1U-N16R8: 16 MB Flash, 8 MB PSRAM.
- W5500 10/100 Ethernet controller over SPI.
- PoE module, IEEE 802.3af.
- Eight isolated digital inputs and eight isolated digital outputs.
- W5500 pins: INT=GPIO12, MOSI=13, MISO=14, SCLK=15, CS=16, RST=39.
- DI1..DI8: GPIO4..GPIO11.
- Digital outputs are exposed through TCA9554PWR, I2C address 0x20.
- Isolated RS485 and CAN are available for future gateway use.
- W5500 supports MACRAW at chip level. The preferred ESP-IDF W5500 driver
  operates the device in MAC RAW mode and feeds the ESP software TCP/IP stack.
  Raw SV/GOOSE and lwIP TCP/MMS therefore share one Ethernet interface instead
  of arstack independently allocating W5500 hardware sockets.

Raw Layer-2 SV/GOOSE is an Ethernet feature. Wi-Fi is not a substitute for the
reference process-bus path.

## Non-negotiable architecture rules

### Embedded core

`ARIEC61850::embedded_core` may contain wire codecs and bounded runtime state,
but must not depend on:

- WinSock or BSD/POSIX sockets;
- filesystem, iostream or CLI code;
- PCAP/evidence tooling;
- COMTRADE;
- full SCL parsing;
- live-discovery host workflows.

Platform ownership is injected through small callback HAL contracts. ESP-IDF,
lwIP, FreeRTOS and W5500 details stay outside the protocol engine.

### Memory discipline

Steady-state publisher/subscriber/runtime paths should move toward:

- caller-owned buffers;
- bounded capacities;
- no per-frame allocation;
- no RTTI;
- eventually no C++ exceptions in the MCU core.

Existing `std::vector`/`std::string` data models may remain as configuration-time
containers during migration. They must not force allocation on the 4 kHz SV
steady-state encode/transmit path.

### Compatibility

Existing host APIs remain source-compatible where practical. Allocation-free
APIs are added underneath host convenience wrappers, so Windows/Linux users do
not lose ergonomics while the same codec becomes MCU-capable.

---

# Delivery plan

## Track A — Public Alpha

### A0. Live read-only MMS foundation — CURRENT

Acceptance:

- TPKT/COTP/Session/Presentation/ACSE association works against live vendor IED.
- GetNameList domains, variables and DataSets works.
- GetVariableAccessAttributes works.
- Read works.
- DataSet and RCB inventories are preserved.
- Dynamic/unbound RCB state is represented without false failure.
- RCB planner/availability evidence is read-only.
- GCC, Clang and MSVC CI pass.
- ASan/UBSan and fuzz corpus pass.

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

Acceptance:

- clean configure/build/test from a fresh clone;
- at least three repeat live cycles on the current test IED/simulator;
- no diagnostic regression;
- reproducible JSON parity report checked into evidence.

### A2. Public alpha release

Goal: tag a release that is genuinely useful even before full ARIEC61850 parity.

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

### E0. Embedded architecture baseline — IN PROGRESS

Deliverables:

- standalone `embedded/CMakeLists.txt`;
- `ARIEC61850::embedded_core`;
- no-RTTI host-simulation CI;
- source-boundary audit;
- callback HAL for raw Ethernet, connected TCP and clocks;
- configurable ESP32 small-capacity profile;
- Waveshare board facts/profile.

Exit gate:

- GCC and Clang embedded host-simulation builds link every embedded source;
- host-only dependency audit is clean;
- legacy exception debt is measured and cannot silently expand.

### E1. Allocation-bounded SV encoder — IN PROGRESS

Goal: the 4 kHz publisher loop does not allocate a new frame buffer per sample.

API direction:

```cpp
std::optional<std::size_t> SampledValuesFrameCodec::encoded_size(...) noexcept;
wire::EncodeResult SampledValuesFrameCodec::encode_into(
    const SampledValuesFrame&,
    std::span<std::uint8_t> destination) noexcept;
```

Host `encode()` remains as a convenience wrapper.

Acceptance:

- bounded encoder is byte-identical to existing C#/C++ golden vectors;
- buffer-too-small returns required size and does not partially report success;
- same caller-owned Ethernet buffer can be reused continuously;
- timestamp serialization has a no-allocation path;
- sanitizer/fuzzer/host regression remains green.

### E2. ESP-IDF W5500 Ethernet adapter

Goal: map `embedded::RawEthernetPort` to the official ESP-IDF W5500 Ethernet
MACRAW path without leaking ESP-IDF/W5500 APIs into protocol code.

Preferred architecture:

```text
arstack raw SV/GOOSE ----> esp_eth raw Ethernet transmit ----+
                                                            |
MMS TCP <---- lwIP / BSD socket API <---- esp_netif --------+--> ESP-IDF W5500 MACRAW
```

The ESP-IDF W5500 driver owns the chip-level MACRAW operation. arstack should
not independently program W5500 hardware sockets when using that driver.

Responsibilities:

- initialize the official ESP-IDF W5500 SPI Ethernet driver with board pins;
- attach the driver to `esp_netif`/lwIP for TCP/MMS;
- expose raw-frame transmit callback for GOOSE/SV;
- preserve destination/source MAC, VLAN tag and EtherType exactly;
- expose PHY/link state;
- instrument TX errors, would-block and frame counts.

Acceptance on host/mock first:

- adapter-facing policy can be unit-tested with fake transmit backend;
- protocol core does not include ESP-IDF/W5500 headers.

Acceptance on board:

- Ethernet obtains link and normal IP traffic works through lwIP;
- raw custom EtherType frame visible in Wireshark;
- VLAN-tagged frame visible without mutation;
- SV EtherType 0x88BA frame visible while normal TCP/IP remains usable.

### E3. ESP32-S3 SV Publisher hardware proof

Start with synthetic waveform; do not block on ADC hardware.

Reference first profile:

- 50 Hz system;
- 80 samples/cycle = 4,000 sample instants/s;
- one ASDU initially;
- fixed APPID and multicast destination;
- sample counter wraps according to configured rate;
- configuration revision fixed during a run;
- timestamp optional for the first throughput proof, then enabled.

Acceptance:

- sustained 4,000 frames/s target or explicitly documented multi-ASDU packing
  alternative;
- no steady-state frame-buffer allocation;
- no sample-counter discontinuity generated by publisher logic;
- packet capture decodes as IEC 61850 SV;
- 10-minute soak first, then 1-hour soak;
- CPU usage, heap delta, minimum free heap and TX errors recorded.

### E4. Board I/O HAL

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

Acceptance:

- deterministic read of all eight DIs;
- deterministic set/reset of all eight DOs;
- power-cycle safe-state test;
- no IEC 61850 code knows GPIO/I2C details.

### E5. Minimal MMS server foundation

This is required for the board to become an actual IEC 61850 I/O IED.

Implement a static, bounded server model first. Do not port desktop dynamic
model discovery into the MCU server.

Minimum server services:

1. association acceptor;
2. MMS Initiate negotiation;
3. GetNameList;
4. GetVariableAccessAttributes;
5. Read.

Initial static model proposal:

```text
ESP32S3IO
└── LD0
    ├── LLN0
    ├── LPHD1
    ├── GGIO1   digital inputs
    └── GGIO2   digital outputs/status
```

Acceptance:

- IEDScout can associate;
- IEDScout can browse the static model;
- eight DI values can be read;
- server remains bounded under malformed/fuzzed MMS requests.

### E6. Simple IEC 61850 I/O IED

Add controlled output operation only after read-only server browse/read is
stable.

Sequence:

- simple Write where standards/model permit;
- IEC 61850 control model required for the exposed DOs;
- interlock/check hooks;
- operation timeout;
- command audit/result status.

Reporting sequence:

- static DataSet;
- URCB first;
- data-change report for DI state;
- BRCB later, not a first-I/O-IED requirement.

Acceptance:

- IEDScout browse/read/control proof;
- input change generates expected report;
- output command changes physical DO and reflected model state;
- negative tests for invalid control/FC/reference.

### E7. GOOSE I/O extension

After raw MAC transport is proven by SV, GOOSE is a natural extension.

Targets:

- DI state as GOOSE publisher;
- GOOSE subscriber mapped to selected DO only through application safety policy;
- retransmission schedule and TAL supervision;
- VLAN priority validation.

### E8. Industrialization / STM32 migration

ESP32 is the reference accessibility target, not the final certification claim.
The same embedded core should later move to STM32H7/NXP-class hardware.

Industrialization work:

- `-fno-exceptions` embedded core;
- replace remaining hot-path dynamic containers with bounded storage/views;
- explicit memory budget report;
- deterministic scheduler/timing hooks;
- watchdog integration;
- stronger time synchronization strategy;
- long-duration soak and fault injection;
- multi-vendor interoperability campaign.

---

# Priority rule

Until Public Alpha is tagged:

- approximately **70% effort** goes to public-useful host protocol parity,
  documentation and real-IED evidence;
- approximately **30% effort** goes to embedded architecture and the SV hardware
  path.

Exception: embedded changes that prevent architectural debt (allocation-free
wire encoding, HAL boundaries, compile gates) are performed immediately before
new protocol features are ported.

After Public Alpha:

- finish E2/E3 hardware SV proof;
- then shift primary development toward E5/E6 MMS server + I/O IED.

# Definition of success

The project succeeds when the same protocol implementation can demonstrate all
of these without forks of the wire core:

1. Windows/Linux engineering client can browse a real IED.
2. ESP32-S3 can publish standards-shaped SV frames over the ESP-IDF W5500
   MACRAW Ethernet interface.
3. ESP32-S3 can expose eight isolated inputs/outputs as a small IEC 61850 IED.
4. A later STM32/NXP port changes platform adapters and capacities, not protocol
   semantics.
