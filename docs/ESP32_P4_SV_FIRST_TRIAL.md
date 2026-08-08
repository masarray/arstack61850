# ESP32-P4 Sampled Values first trial

This is the first active process-bus transmit proof for arstack61850. It is a
controlled laboratory step, not a protection or metering accuracy claim.

## Target

Start with an ESP32-P4 board that exposes the internal Ethernet EMAC through an
RMII PHY and for which the application already owns a started `esp_eth_handle_t`.
The arstack protocol code does not configure board-specific PHY pins. That stays
in the ESP-IDF application so the same stack can be reused with different P4
boards and later with other MCU families.

The first trial intentionally uses an **untagged** Ethernet frame. ESP-IDF v6.0.2
still documents VLAN-tagged Ethernet frames as unsupported by the basic Ethernet
path. VLAN transmission is therefore a separate hardware/driver acceptance gate,
not something the protocol core should hide or emulate.

Useful Espressif references:

- ESP32-P4 Ethernet: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/network/esp_eth.html
- ESP32-P4 getting started: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/index.html

## What this branch provides

- `SampledValuesPduCodec::encode_into()` and
  `SampledValuesFrameCodec::encode_into()` reuse caller-owned buffers.
- `SampledValuesPublisher` provides deterministic sample-count progression and a
  no-catch-up pacing policy.
- `SampledValuesPayloadWriter` updates common INT32 + quality pairs in a
  pre-sized `seqOfData` buffer without allocating.
- `embedded::RawEthernetPort` is the platform-neutral raw Ethernet contract.
- `ports/esp_idf` maps that contract to `esp_eth_transmit()` and exposes
  `esp_timer_get_time()` as a monotonic microsecond clock.

The steady-state publisher path is `noexcept` and does not resize the frame,
ASDU, or payload containers. Allocate and size configuration objects before the
publisher loop starts.

## Reference first profile

Use this conservative initial profile:

| Item | First-trial value |
| --- | --- |
| System frequency | 50 Hz |
| Samples/cycle | 80 |
| Sample instants | 4,000/s |
| ASDUs/frame | 1 |
| Ethernet | untagged first |
| EtherType | `0x88BA` |
| Destination MAC | `01:0C:CD:04:00:01` |
| APPID | `0x4001` |
| `confRev` | `1` for the whole run |
| `smpSynch` | `0` until a real synchronized time source is proven |
| `refrTm` | omitted for throughput proof |
| `smpCnt` wrap | `4000` for this first profile |

The source MAC must be the actual Ethernet MAC assigned to the ESP32-P4
interface. Do not copy the synthetic host-test source MAC into a physical unit.

## ESP-IDF component integration

Add the repository's `ports/esp_idf` directory as an extra component in the
application-level `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../arstack61850/ports/esp_idf")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arstack_sv_trial)
```

The component compiles only the embedded protocol boundary plus the ESP-IDF
adapter. Host TCP discovery, PCAP, COMTRADE, SCL parsing, filesystem and CLI
sources are intentionally excluded from firmware.

## Minimal publisher setup

The board application is responsible for installing and starting Ethernet. Once
it has a valid `esp_eth_handle_t eth_handle`, build the publisher once and reuse
it:

```cpp
#include "ariec61850/embedded/profile.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/ports/esp_idf/ethernet_port.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/publisher.hpp"

#include <array>
#include <cstdint>

using namespace ar::iec61850;

ports::esp_idf::RawEthernetContext ethernet_context{eth_handle};
auto raw_port = ports::esp_idf::make_raw_ethernet_port(ethernet_context);
auto clock = ports::esp_idf::make_monotonic_clock();

sampled_values::SampledValueAsdu asdu;
asdu.sv_id = "ESP32P4/LLN0$MSVCB01";
asdu.data_set_reference = "ESP32P4/LLN0$PhsMeas1";
asdu.configuration_revision = 1;
asdu.sample_synchronization = 0;
asdu.sample_rate = std::uint16_t{4000};
asdu.sample_mode = std::uint16_t{1};
asdu.sample_payload.resize(64); // eight INT32 + quality pairs; allocate once

std::array<std::uint8_t, 6> dst{0x01, 0x0C, 0xCD, 0x04, 0x00, 0x01};
std::array<std::uint8_t, 6> src{}; // fill from the actual Ethernet MAC

sampled_values::SampledValuesFrame frame{
    ethernet::MacAddress{dst},
    ethernet::MacAddress{src},
    std::nullopt,
    0x4001,
    0,
    0,
    sampled_values::SampledValuesPdu{{asdu}}};

std::array<std::uint8_t, embedded::Esp32SmallProfile::ethernet_frame_bytes>
    ethernet_buffer{};

sampled_values::SampledValuesPublisher publisher{
    frame,
    ethernet_buffer,
    raw_port,
    sampled_values::SampledValuesPublisherConfig{
        4000,
        std::uint16_t{4000},
        0,
        true}};
```

For every sample instant, update the already-sized payload in place. The exact
channel order, scaling and quality semantics must match the DataSet/profile that
will eventually describe this stream; the following is only a synthetic wire
proof:

```cpp
auto payload = std::span<std::uint8_t>{frame.pdu.asdus.front().sample_payload};

sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
    payload, 0, synthetic_ia, 0);
sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
    payload, 1, synthetic_ib, 0);
// ...pre-sized remaining pairs...

const auto result = publisher.poll(clock.monotonic_us());
```

`poll()` never sends more than one frame per call. If the FreeRTOS task wakes up
late, the publisher records lateness and anchors the next deadline after the
actual attempt instead of emitting a burst of back-to-back catch-up frames.
This mirrors the pacing lesson learned from the C# transport implementation.

If a high-resolution timer already owns the 4 kHz cadence, call
`publish_now(clock.monotonic_us())` once per tick instead. Do not call both
pacing modes for the same stream session.

## Recommended FreeRTOS ownership

For the first hardware proof:

1. Use one dedicated high-priority publisher task pinned according to the board
   application's CPU/task policy.
2. Prepare strings, vectors, frame configuration and the Ethernet buffer before
   entering the steady-state loop.
3. Update only payload bytes and runtime counter state in the hot path.
4. Do not log every frame. Aggregate counters and print them at a slow interval.
5. Avoid dynamic allocation, filesystem work, JSON, Wi-Fi work or UI activity in
   the publisher task.
6. If `esp_eth_transmit()` returns timeout/error, retain the same `smpCnt`; the
   next successful transmission will not silently skip a publisher-generated
   counter value.

ESP-IDF documents that short/frequent Ethernet traffic can require DMA descriptor
and buffer tuning. If TX cannot sustain the target, inspect Ethernet DMA buffer
size/count configuration before changing IEC 61850 wire semantics.

## Wireshark acceptance

Capture on an isolated lab switch/TAP and filter:

```text
eth.type == 0x88ba
```

First acceptance requires:

- destination multicast MAC is exact;
- source MAC matches the ESP32-P4 interface;
- EtherType is `0x88BA`;
- APPID is `0x4001`;
- one ASDU decodes;
- `svID`, DataSet reference and `confRev` remain stable;
- `smpCnt` is continuous and wraps at the configured point;
- no scheduler-late catch-up bursts are generated by the publisher logic;
- no steady-state growth in application heap attributable to the arstack
  publisher path;
- TX errors/timeouts are counted and visible.

Run a short functional capture first, then a 10-minute soak. If stable, extend to
one hour. Record at minimum:

- `frames_sent`;
- `transmit_failures`;
- `late_polls`;
- `maximum_lateness_us`;
- current free heap and minimum-ever free heap;
- observed packets/s in Wireshark;
- any counter gap/duplicate/out-of-order finding from an independent subscriber.

## What this proof does not claim

A successful first trial proves the C++ wire core, MCU portability boundary,
raw-Ethernet adapter and basic pacing path can cooperate on ESP32-P4. It does not
yet prove IEC 61869-9 measurement scaling, PTP synchronization, VLAN priority,
protection-grade timing, multi-vendor interoperability, or certification.
