# ESP32-P4 Sampled Values first-trial firmware

This is the first flashable arstack61850 firmware target. It is intentionally narrower than the full desktop stack and proves the active Sampled Values transmit vertical slice on ESP32-P4.

## Hardware profile

- board target: Waveshare ESP32-P4-ETH
- ESP-IDF target: `esp32p4`
- Ethernet: internal ESP32-P4 EMAC + RMII
- PHY: IP101-compatible IEEE 802.3 path
- PHY address: 1
- PHY reset GPIO: 51
- Sampled Values: 50 Hz / 80 samples per cycle / 4,000 sample instants per second
- APPID: `0x4001`
- destination MAC: `01:0C:CD:04:00:01`
- untagged Ethernet first proof
- one ASDU per Ethernet frame
- 8 synthetic `INT32 + quality` pairs (64-byte `seqOfData`)

The synthetic sample values are deterministic and are only for transport/wire validation. They are not IEC 61869-9 instrument-transformer scaling.

## Build

From an ESP-IDF v6.0.2 shell:

```bash
idf.py set-target esp32p4
idf.py build
```

GitHub Actions builds the same project with ESP-IDF v6.0.2. A successful workflow uploads the complete flashable build set as the `esp32p4-sv-trial-firmware` artifact, including the application image, bootloader, partition table, ELF, map, `flash_args`, `flasher_args.json`, and the generated sdkconfig.

## Flash

With the repository and ESP-IDF environment available:

```bash
idf.py -p <PORT> flash monitor
```

A downloaded CI artifact can also be flashed with Espressif's flashing tools using the included `flash_args`/`flasher_args.json`. Keep the extracted directory structure intact so the relative binary paths remain valid.

## Expected serial output

After boot the firmware:

1. initializes the ESP32-P4 EMAC and PHY;
2. waits for an Ethernet link;
3. starts a 250 us periodic ESP timer;
4. wakes a dedicated high-priority publisher task;
5. publishes at most one SV frame for each processed timer notification;
6. reports counters approximately once per second.

The QA line reports frames sent, encode/TX failures, late polls, maximum lateness, timer notifications, coalesced timer notifications, current free heap, and minimum-ever free heap.

## What CI proves without hardware

A green repository can prove:

- portable protocol core builds with GCC and Clang;
- host-sim publisher behavior passes CTest;
- ESP32-P4 RISC-V cross-compilation and final link succeed under ESP-IDF v6.0.2;
- a complete flashable firmware image set is produced.

CI **cannot** prove PHY electrical bring-up, real cable transmission, DMA behavior under sustained 4 kHz traffic, scheduler jitter on silicon, or Wireshark-observed packet continuity. Those remain explicit hardware gates.
