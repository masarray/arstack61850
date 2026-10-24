# ARStack61850

**Open-source IEC 61850 Process Bus engineering lab**

Generate real Sampled Values (SV) traffic, explore PTP synchronization, and experiment with digital-substation signals using **ARStack Studio** and an **ESP32-P4 Ethernet board**.

[**Download the latest stable release**](https://github.com/masarray/arstack61850/releases/latest) · [**Getting started**](docs/GETTING_STARTED.md) · [**Product website**](https://masarray.github.io/arstack61850/) · [**Get help**](SUPPORT.md)

## What you can do

| Explore | With ARStack |
| --- | --- |
| **Generate Sampled Values** | Publish 4 current + 4 voltage channels over real Ethernet at 4,000 SV frames/s. |
| **Experiment with PTP** | Run an internal laboratory grandmaster emulator with hardware-assisted phase synchronization. |
| **Control waveforms** | Set current, voltage and phase values in the ARStack Studio desktop interface. |
| **Inspect live traffic** | Capture SV and PTP using Studio, Wireshark, ARSAS or DigSubAnalyzer. |
| **Learn with engineering files** | Open SCL files and deploy supported 4I+4V publisher profiles. |

**M3.H stable lab release (24 October 2026):** an independent capture measured **163,048 consecutive SV frames at 3,999.884 frames/s**, with no missing, duplicate or malformed SV packets and `smpSynch=2` throughout the recorded interval. [View the full measurements](docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md).

## Inside ARStack Studio

![ARStack Studio Windows desktop running 4I+4V engineering workspace and live waveform preview](assets/screenshots/arstack-studio-windows.png)

*Screenshot captured directly from the Windows Qt application by the native GUI test.*

## Get started in minutes

1. **[Download ARStack Studio](https://github.com/masarray/arstack61850/releases/latest)** for Windows x64 (Setup EXE or Portable ZIP).
2. Open Studio and try the built-in **4I+4V Quick Start** profile. You can explore the interface before connecting hardware.
3. To transmit real Ethernet frames, connect a compatible **ESP32-P4 Ethernet** board over USB and Ethernet. Use **Configuration → Firmware** to install the firmware bundled with **the same release**.
4. Choose **PTP SOURCE**, set **SMPSYNCH AUTO**, configure voltage/current waveforms, and press **Start**.
5. View the output using Studio capture or Wireshark (`0x88BA` for SV, `0x88F7` for PTP).

**[Full first-run guide →](docs/GETTING_STARTED.md)** · [Installation on Windows and Linux](docs/INSTALL.md)

For active SV/PTP transmission, use an **isolated laboratory Ethernet network**, not an operating substation process bus.

## Downloads and platforms

| Package | Availability |
| --- | --- |
| Windows Setup EXE | **Available** in the [latest release](https://github.com/masarray/arstack61850/releases/latest) |
| Windows Portable ZIP | **Available** in the [latest release](https://github.com/masarray/arstack61850/releases/latest) |
| Matched ESP32-P4 firmware | **Available** with the same release |
| Linux AppImage | Shown in the [latest release](https://github.com/masarray/arstack61850/releases/latest) when a tested package is published |
| Windows package manager | [WinGet preparation and status](docs/INSTALL.md#windows-package-manager--winget) |

Every published Windows release includes the application dependencies and a matched firmware package; no C++ build environment is required for ordinary installation.

## Community

ARStack is built to make IEC 61850 experiments more accessible to engineers, students and automation enthusiasts around the world.

- **[Ask a question or report a problem](SUPPORT.md)** — share what you tried and what happened.
- **[Share an interoperability experiment](docs/COMMUNITY_TESTING.md)** — feedback from independent laboratory setups helps improve the project.
- **[Contribute](CONTRIBUTING.md)** — improvements to documentation, software, tests and examples are welcome.

ARStack uses the [GPL-3.0 license](LICENSE). It is a laboratory simulation and engineering tool, not a certified primary measurement or UTC reference device.

## For developers

Want to explore the protocol engine or build from source? Start with the [developer documentation index](docs/DEVELOPER_GUIDE.md).

The source tree includes a portable C++20 IEC 61850 protocol engine, SCL/COMTRADE tooling, MMS and GOOSE foundations, ESP32-P4 firmware and reproducible tests. Capabilities vary by application and release; the [feature matrix](docs/FEATURE_MATRIX.md) separates released tools from libraries and development features.
