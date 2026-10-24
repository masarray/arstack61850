# ARStack61850

An open-source **IEC 61850 engineering lab** for exploring Sampled Values, PTP timing and process-bus behavior. Configure waveforms in a native desktop app, then generate real Ethernet traffic with an ESP32-P4 board.

[![Latest release](https://img.shields.io/github/v/release/masarray/arstack61850?label=Latest%20release)](https://github.com/masarray/arstack61850/releases/latest)
[![C++ CI](https://github.com/masarray/arstack61850/actions/workflows/cpp-ci.yml/badge.svg)](https://github.com/masarray/arstack61850/actions/workflows/cpp-ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue)](LICENSE)

[**Download latest release**](https://github.com/masarray/arstack61850/releases/latest) · [Product website](https://masarray.github.io/arstack61850/) · [Installation guide](docs/INSTALL.md) · [Report an issue](https://github.com/masarray/arstack61850/issues)

## What can you do with ARStack?

| Capability | Your workflow |
| --- | --- |
| 4I + 4V Sampled Values | Set current, voltage, frequency and phase scenarios in ARStack Studio, then publish SV from an ESP32-P4 Ethernet board |
| PTP laboratory timing | Explore a simulated grandmaster and observe Announce, Sync and Follow_Up frames |
| Capture and inspect | Review traffic, counters, waveform/phasor behavior and timing evidence |
| IEC 61850 engineering files | Explore SCL-based configurations and reference profiles in the desktop tooling |
| Open protocol foundations | Build on C++20 MMS, GOOSE, Sampled Values, SCL and COMTRADE components |

ARStack is made for hands-on experimentation, demonstrations and engineering validation. You can launch the desktop app and explore its controls before connecting hardware; Ethernet SV/PTP output uses the supported ESP32-P4 device.

## Get started

1. **Install Studio.** Open the [latest stable release](https://github.com/masarray/arstack61850/releases/latest) and download the Windows x64 Setup EXE or Portable ZIP.

2. **Choose a scenario.** Open ARStack Studio and select the built-in **4I+4V** profile, or open a compatible SCL configuration.

3. **Connect hardware for live output.** Attach your ESP32-P4 Ethernet board through USB and install the firmware shipped with the *same release* using Studio's Firmware panel.

4. **Start your lab traffic.** Connect Ethernet to a dedicated test switch, set your signals and PTP mode, then use **Start / Stop** and inspect the resulting SV/PTP packets.

Prefer installing from a Windows terminal? The verified installer script follows GitHub's current public stable release:

```powershell
$script = Join-Path $env:TEMP 'arstack-install.ps1'
Invoke-WebRequest 'https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-windows.ps1' -OutFile $script
& $script
```

For Linux x86_64, check [Downloads](https://github.com/masarray/arstack61850/releases/latest) for an AppImage when it is published; the [installation guide](docs/INSTALL.md) also covers Linux command-line setup and WinGet package status.

## See ARStack Studio

![ARStack Studio native Windows engineering workspace — captured from the real Qt application](assets/screenshots/arstack-studio-windows.png)

*Real ARStack Studio Windows screenshot, captured by the application's built-in native-window acceptance test.*

## See it in action

The released **M3.H** laboratory milestone captured **163,048 SV frames** in 40.763 seconds at **3,999.884 frames/s**, with continuous `smpCnt` and no missing, duplicate or malformed SV frames in that captured interval. The same test recorded PTP Announce / Sync / Follow_Up traffic.

[View the M3.H engineering results](docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md) · [Explore the product page](https://masarray.github.io/arstack61850/)

## Developers & contributors

ARStack includes a portable C++20 IEC 61850 engine, Qt desktop applications, embedded targets and automated protocol tests.

- [Contributing](CONTRIBUTING.md) — propose improvements with reproducible evidence and current-main PRs
- [Security policy](SECURITY.md) — report vulnerabilities privately and handle lab evidence responsibly
- [Technical & developer reference](docs/DEVELOPER_GUIDE.md) — architecture, build commands, implemented modules and lab validation
- [Project direction](NORTH_STAR.md) — product goals and architecture
- [Studio operator guide](apps/arstack_studio/README.md) — usage and firmware handling
- [IEC 61850 simulator](apps/ied_simulator/README.md) — MMS server and model-based engineering workbench
- [Issues and discussions](https://github.com/masarray/arstack61850/issues) — report a problem or share a reproducible test

The application is free and open source under the [GPL-3.0 license](LICENSE). Keep active SV/PTP publishing on a dedicated lab network rather than a live substation LAN.
