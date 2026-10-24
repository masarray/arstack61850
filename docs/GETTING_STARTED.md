# Your first IEC 61850 Process Bus experiment

This guide takes you from a fresh Windows installation to inspecting Sampled Values and PTP on a small Ethernet lab.

**You will learn how to:** open ARStack Studio, generate a 4-current + 4-voltage waveform, start real SV traffic from an ESP32-P4, and inspect PTP alongside SV.

## What you need

- A Windows 10/11 x64 PC with [ARStack Studio](https://github.com/masarray/arstack61850/releases/latest).
- For **real Ethernet output**: a compatible ESP32-P4 Ethernet development board, USB data cable, Ethernet cables and an isolated Ethernet switch or direct lab connection.
- Optional: Wireshark or another SV/PTP analyzer.

You can install and explore the desktop software before purchasing or connecting a board.

## Step 1 — Install Studio

1. Open the [latest stable release](https://github.com/masarray/arstack61850/releases/latest).
2. Download the **Windows Setup EXE**. Choose the Portable ZIP if you prefer not to install.
3. Open ARStack Studio.

The setup includes the desktop dependencies and **matching board firmware**.

## Step 2 — Connect the ESP32-P4

1. Connect the board over **USB data** (not a charge-only cable).
2. Connect the board's Ethernet port to your **isolated test network**.
3. In Studio, connect to the detected serial device. Follow the displayed device identity and firmware status.

Do not connect the SV publisher or PTP source to an operating substation LAN.

## Step 3 — Install the matched firmware

1. In Studio, open **Configuration → Firmware**.
2. Select the ESP32-P4 device and use **Verify ESP32-P4**.
3. Choose **Install / Recover** to install the firmware provided with this Studio release.
4. Allow the board to restart, then reconnect from Studio and confirm device recognition.

Installing a newer Windows application does **not** update the board firmware automatically. Use the firmware packaged with that release.

## Step 4 — Generate Sampled Values

1. Select **4I+4V Quick Start**.
2. Deploy the supported publisher profile while the output is stopped.
3. Choose **PTP SOURCE / internal laboratory grandmaster** and **SMPSYNCH AUTO**.
4. Set current and voltage magnitudes and phase angles in Studio.
5. Press **Start**. The ESP32-P4 sends the real-time SV stream directly from its Ethernet hardware.
6. Press **Stop** when finished. PTP SOURCE can remain active across SV STOP/START.

The published M3.H reference profile sends 4 current and 4 voltage values at 4,000 SV frames per second.

## Step 5 — View your Ethernet traffic

With the PC capture interface connected to the relevant lab segment or switch mirror:

- Filter **Sampled Values:** `eth.type == 0x88ba`
- Filter **PTP:** `eth.type == 0x88f7`
- Watch the SV `smpCnt` increase and wrap from `3999` to `0`.
- Inspect `svID`, `APPID`, quality fields, and the PTP Announce/Sync/Follow_Up messages.

A switched network does not automatically forward every multicast frame to the PC capture port. If you cannot see SV, verify the lab capture topology before changing the publisher.

## Try something different

Adjust a voltage or current setpoint and observe the generated waveform. Open a compatible SCL/CID/SCD/IID file to inspect and select an SV stream supported by the currently published 4I+4V device profile.

## Need help?

Check the [installation guide](INSTALL.md) or [support guide](../SUPPORT.md). For an SCL import where SV ID or MAC address does not appear, follow the [SCL troubleshooting guide](SCL_TROUBLESHOOTING.md). If a device will not connect, share the Studio version and the displayed error.

Want to publish observations from another IEC 61850 tool or device? See the [community testing guide](COMMUNITY_TESTING.md).

For measured M3.H packet evidence and the exact firmware pairing, see the [release technical record](releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md).
