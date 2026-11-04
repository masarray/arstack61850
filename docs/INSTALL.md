# Install ARStack Studio

**Latest stable:** [Process Bus Simulator — Dual Firmware Stable](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-dual-stable-20261105) (5 November 2026) · [Product website](https://masarray.github.io/arstack61850/) · [Getting started](GETTING_STARTED.md)

## Windows 10/11 (x64)

**Recommended:** [Download Windows Setup ZIP](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-win-x64-setup.zip), extract it and run the contained **ARStack-Studio-win-x64-setup.exe**.

Alternatives: [Windows Portable ZIP](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-win-x64-portable.zip) (no installer) or [direct Setup EXE](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-win-x64-setup.exe). The current stable installer uses its own AppId and can coexist with an earlier Preview installation.

The Setup ZIP helps reduce direct-EXE browser download friction, but **does not bypass SmartScreen, antivirus scanning or code-signing warnings** for unsigned executable installers.

## Linux (x86_64)

[Download Linux AppImage](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-linux-x86_64.AppImage).

In a terminal in the download directory, run:

    chmod +x ARStack-Studio-linux-x86_64.AppImage
    ./ARStack-Studio-linux-x86_64.AppImage

The stable release includes a Linux SHA-256 file and **SHA256SUMS.txt**. Some Linux distributions may require additional AppImage/FUSE runtime support.

## Firmware and first Ethernet lab

The dual stable release includes two separate revision-aware ESP32-P4 firmware images:

- **arstack-esp32p4-smv-0.1.1.bin** — pre-v3 hardware.
- **arstack-esp32p4-smv-0.1.1-v3x.bin** — v3.x hardware (**engineering-preview physical qualification**).

In **ARStack Studio → Configuration → Firmware**, verify the device and install the image matched to both the board silicon and the **same Studio release**. The release provides a firmware catalog and manifest. Updating the desktop does not update the board automatically.

1. Open Studio and select **4I+4V Quick Start**.
2. Connect the ESP32-P4 over USB data and isolated lab Ethernet.
3. Select simulated **PTP SOURCE** and **SMPSYNCH AUTO** for the lab profile.
4. Start SV publishing, then inspect SV (EtherType 0x88BA) and PTP (0x88F7) in the subscriber/analyzer.
5. Compare APPID, MAC, VLAN and svID with the subscriber configuration.

The powered ESP32-P4 publishes autonomously; reconnecting Studio adopts the board state.

**Verification boundary:** M4 software integrates SCL-driven three-virtual-MU configuration and group START/STOP. Historical M3.H.3 on-wire evidence measures **one** stream; sustained three-stream aggregate and v3.x PHY testing are not yet proven by that benchmark. See the [M4 engineering roadmap](PROCESS_BUS_MULTI_SV_SCL_NEXT_PHASE.md) and [M3.H.3 capture record](releases/PROCESSBUS_AUTONOMOUS_M3H3_OFFICIAL_20261025.md).

Use on an isolated laboratory network, never a production/energized substation process bus.
