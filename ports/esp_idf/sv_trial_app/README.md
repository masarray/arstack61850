# ESP32-P4 Sampled Values injector firmware

This is the first flashable arstack61850 Sampled Values injector target for ESP32-P4. The firmware uses the same fixed-point `DeterministicSvInjector` waveform engine and canonical 4I+4V profile as the Windows reference injector.

## Hardware profile

- board target: Waveshare ESP32-P4-ETH
- ESP-IDF target: `esp32p4`
- Ethernet: internal ESP32-P4 EMAC + RMII
- PHY: IP101-compatible IEEE 802.3 path
- PHY address: 1
- PHY reset GPIO: 51
- management/control: UART0 through the onboard Type-C USB-to-UART bridge
- UART0 TX: GPIO37
- UART0 RX: GPIO38
- control baud rate: 115200, 8 data bits, no parity, one stop bit
- Sampled Values: 50 Hz / 80 samples per cycle / 4,000 sample instants per second
- APPID: `0x4001`
- destination MAC: `01:0C:CD:04:00:01`
- `svID`: `ARSTACK61850_INJECTOR`
- dataset: `ARSTACK61850/LLN0$PhsMeas1`
- untagged Ethernet first proof
- one ASDU per Ethernet frame
- channel order: `Ia, Ib, Ic, In, Va, Vb, Vc, Vn`
- 8 x `INT32 + quality` pairs (64-byte `seqOfData`)

The first profile uses raw engineering counts shared with the Windows reference path. It is not yet a claim of IEC 61869-9 calibrated instrument-transformer scaling.

## Deterministic execution model

The logical waveform advances by integer sample index through the shared fixed-point engine. The 250 us ESP timer and publisher task decide when a frame may be attempted; they do not decide which waveform sample logically comes next.

If a timer notification arrives before the publisher is due, the firmware retains the same pending sample rather than advancing the waveform. If encode or physical TX fails, the first control slice enters `FAULT` instead of silently allowing waveform sample index and `smpCnt` to diverge.

## UART0 control plane

The board's Type-C programming/debug connector is implemented through an onboard USB-to-UART bridge connected to ESP32-P4 UART0. The injector therefore keeps UART0 as primary standard input/output so the same `COMx` port used for flashing and monitoring can carry injector commands after boot.

The protocol is line-delimited JSON, schema:

```text
arstack-sv-injector-control-v1
```

Each request is one JSON object terminated by a newline. Responses are one-line JSON prefixed by `ARCTRL ` so a PC application can separate control responses from firmware log output.

Supported lifecycle commands:

```json
{"command":"capabilities"}
{"command":"status"}
{"command":"configure","scenario":"normal"}
{"command":"configure","scenario":"protection-fault"}
{"command":"arm"}
{"command":"start"}
{"command":"stop"}
{"command":"stats"}
```

Realtime source commands additionally provide sparse channel edits, sample-indexed ramps, and transactional finite state-sequence construction.

Lifecycle:

```text
IDLE -> CONFIGURED -> ARMED -> RUNNING -> STOPPED
                         |         |
                         +-> STOPPED
                                   |
                                  FAULT
```

On boot the firmware validates and stores the `normal` profile, then waits in `CONFIGURED`. A normal run is therefore:

```json
{"command":"status"}
{"command":"arm"}
{"command":"start"}
```

Stop with:

```json
{"command":"stop"}
```

To change scenario after stopping:

```json
{"command":"configure","scenario":"protection-fault"}
{"command":"arm"}
{"command":"start"}
```

A `configure` command is rejected while `ARMED` or `RUNNING`. `start` is rejected unless the active configuration has been armed. A fault can be recovered by sending a new valid `configure`, then `arm`, then `start`.

## Build

From an ESP-IDF v6.0.2 shell in this directory:

```bash
idf.py set-target esp32p4
idf.py build
```

GitHub Actions builds the same project with ESP-IDF v6.0.2. A successful workflow uploads the complete flashable build set as the `esp32p4-sv-trial-firmware` artifact, including the application image, bootloader, partition table, ELF, map, `flash_args`, `flasher_args.json`, generated sdkconfig, checksums, and build evidence.

## Flash

Use the board Type-C programming/debug COM port:

```bash
idf.py -p <PORT> flash
```

A downloaded CI artifact can also be flashed with compatible flashing tools using the included `flash_args`/`flasher_args.json`. Keep the extracted directory structure intact so relative binary paths remain valid.

If a serial monitor is used after flashing, close it before starting a separate device-control client because the control client opens the COM port exclusively.

## Expected runtime behavior

After boot the firmware:

1. initializes the ESP32-P4 EMAC and PHY;
2. initializes the deterministic 4I+4V control configuration;
3. starts the 250 us timer and the high-priority publisher task;
4. exposes the JSON control plane on primary UART0 stdio through the Type-C USB-to-UART bridge;
5. remains `CONFIGURED` until `arm` and `start` are received;
6. while `RUNNING`, emits at most one SV frame for each processed timer opportunity;
7. pauses transmission while Ethernet link is down without consuming logical waveform samples;
8. reports publisher/timer/heap telemetry and responds to `stats` on demand.

The `stats` response includes frames sent, encode/TX failures, late polls, maximum lateness, timer notifications, and coalesced notifications. Periodic firmware QA logs additionally include heap watermarks.

## First physical acceptance

1. flash the firmware through the Type-C UART programming/debug port;
2. close any serial monitor that still owns the COM port;
3. connect Ethernet;
4. verify `status` reports `CONFIGURED` and Ethernet link state;
5. send `arm`, then `start`;
6. capture EtherType `0x88BA` with an independent receiver;
7. confirm APPID `0x4001`, `svID`, dataset, 4I+4V waveform, 50 Hz, and `smpCnt` continuity;
8. compare values/phase sequence against the Windows reference injector;
9. exercise realtime channel edits, ramps, and finite state sequences;
10. request `stats` and retain the telemetry as evidence;
11. send `stop` and verify the stream ceases.

## What CI proves without hardware

A green repository can prove:

- portable protocol/control core builds with strict GCC and Clang warnings;
- deterministic waveform and injector lifecycle/parser smoke tests pass;
- host-sim publisher behavior passes CTest;
- ESP32-P4 RISC-V cross-compilation and final link succeed under ESP-IDF v6.0.2;
- a complete flashable firmware image set is produced.

CI cannot prove USB-to-UART driver/cable behavior, physical UART command exchange, PHY electrical bring-up, real Ethernet transmission, DMA behavior under sustained 4 kHz traffic, scheduler jitter on silicon, or external subscriber interoperability. Those remain explicit hardware gates.
