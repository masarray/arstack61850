# ARStack Studio

**Desktop controls for real-time IEC 61850 Sampled Values and PTP lab experiments.**

[**Download the latest stable version**](https://github.com/masarray/arstack61850/releases/latest) · [**First-run tutorial**](../../docs/GETTING_STARTED.md) · [**Product website**](https://masarray.github.io/arstack61850/)

ARStack Studio configures the supported 4I+4V publisher profile and controls waveform setpoints. The ESP32-P4 performs real-time Ethernet SV publication and internal PTP source emulation.

## Your first experiment

1. Install Studio (Windows Setup or Portable) and open **4I+4V Quick Start**.
2. Connect a compatible ESP32-P4 Ethernet development board by USB on an isolated lab Ethernet segment.
3. Use **Configuration → Firmware** to verify the board and install the matching release firmware.
4. Deploy your 4I+4V profile while stopped.
5. Enable **PTP SOURCE**, select **SMPSYNCH AUTO**, set signal magnitude/phase and press **Start**.
6. Observe the generated SV/PTP traffic using Studio Capture or an Ethernet analyzer.

The current M3.H reference publisher supports four current and four voltage channels, one ASDU per frame, a 64-byte sample payload, 4,000 SV frames/s and `smpCnt` 0–3999. Compatible SCL/CID/SCD/IID input can be used for the supported profile.

## M3.H measured milestone

The 24 October 2026 M3.H public release combines SV publishing with an internal simulated PTP grandmaster and hardware-assisted phase evidence.

An independent lab capture recorded **163,048 consecutive SV frames** in **40.763 seconds**, measured at **3,999.884 frames/s**. No missing, duplicate or malformed SV frames were observed; all captured SV advertised `smpSynch=2`. PTP Announce/Sync/Follow_Up packets were captured alongside SV.

[Read the complete technical record and firmware identity](../../docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md).

## Firmware installation and recovery

The Windows package includes the firmware image, manifest/checksum and flashing tool. Use **Configuration → Firmware** to:

1. Select the detected USB serial device.
2. **Verify ESP32-P4**.
3. Choose **Install / Recover** and allow the device to restart.
4. Reconnect and verify the ARStack device identity.

Studio and ESP32-P4 firmware must come from the **same public release**. Installing Studio alone does not change the firmware on the board.

Network SV/PTP transmission is for an isolated test bench; never inject an unsolicited PTP grandmaster or SV source into an operating substation network.

## For developers

Studio is a Qt 6 / C++20 application. The desktop controller handles device verification, SCL profile validation, and operator commands; real-time frame scheduling occurs in ESP32-P4 firmware.

Build from a Qt-enabled shell with CMake 3.24+ and Qt 6.5+:

```powershell
cmake -S apps/arstack_studio -B build-arstack-studio -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-arstack-studio --target arstack_studio --parallel
```

[Developer documentation](../../docs/DEVELOPER_GUIDE.md) · [Embedded architecture](../../embedded/esp32p4_smv_injector/ARCHITECTURE.md) · [Contributing](../../CONTRIBUTING.md)
