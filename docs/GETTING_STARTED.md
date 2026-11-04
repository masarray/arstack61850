# Your first IEC 61850 Process Bus experiment

Use ARStack Studio to generate real Ethernet Sampled Values (SV) and simulated GPS-style PTP Grandmaster traffic with a compatible ESP32-P4 board on an **isolated engineering network**.

**Current release:** [Process Bus Simulator — Dual Firmware Stable](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-dual-stable-20261105). Studio supports Windows and Linux x86_64; paired revision-aware ESP32-P4 firmware is published alongside it.

## What you need

- Windows 10/11 x64 or Linux x86_64, plus [ARStack Studio](https://masarray.github.io/arstack61850/).
- For **real Ethernet output**: a compatible ESP32-P4 Ethernet board, USB data cable, Ethernet cable and an isolated test network.
- Optional: Wireshark or an SV/PTP-capable analyzer, relay or BCU subscribed to your stream.

You can explore Studio without a board. Transmitting real SV and PTP Ethernet frames requires a powered ESP32-P4.

## Step 1 — Install Studio

1. **Windows:** [Download Setup ZIP](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-win-x64-setup.zip), extract it and run the installer inside. The release also offers Windows Portable ZIP and a direct Setup EXE.
2. **Linux x86_64:** [Download AppImage](https://github.com/masarray/arstack61850/releases/download/processbus-sim-dual-stable-20261105/ARStack-Studio-linux-x86_64.AppImage), mark it executable (chmod +x) and run it.
3. Open Studio. Use desktop software and board firmware from the **same release**. A Studio upgrade does not automatically reflash the board.

See the [installation guide](INSTALL.md).

## Step 2 — Connect the ESP32-P4

1. Connect the board over USB **data** (not a charging-only cable).
2. Connect the board Ethernet port to your **isolated lab network**.
3. Connect Studio to the detected serial device and check its identity and firmware status.

**Do not connect the publisher or PTP source to a live/energized substation process bus.**

## Step 3 — Install revision-matched firmware

1. Open **Configuration → Firmware** in Studio.
2. Use **Verify ESP32-P4** to identify the board.
3. Install/recover firmware from the **same stable release**, using the matching **pre-v3** or **v3.x** image.
4. Restart/reconnect as instructed and verify the recognized board identity.

The v3.x image is part of the dual-firmware package but retains **engineering-preview physical qualification**. Do not present historical pre-v3 traffic acceptance as v3.x evidence.

## Step 4 — Generate Sampled Values and PTP

1. Select built-in **4I+4V Quick Start**, or import a supported SCL/SCD/CID/IID engineering profile.
2. Deploy the supported stream configuration while output is stopped.
3. Select simulated **PTP SOURCE / INTERNAL_GM_ONLY** and **SMPSYNCH AUTO** for the intended lab profile.
4. Set current/voltage magnitudes and phase angles, then press **Start**.
5. Press **Stop** when finished. PTP SOURCE can remain active across SV stop/start.

The historic M3.H.3 single-stream test used four current and four voltage values at nominal 4,000 SV frames per second. The powered board publishes autonomously; if Studio closes, it continues while board power and Ethernet remain available, until operator stop or a fault. Reopened Studio adopts the live board state.

## Step 5 — Inspect real Ethernet traffic

Connect your analyzer to the correct lab segment or mirrored switch port:

- SV display filter: `eth.type == 0x88ba`
- PTP display filter: `eth.type == 0x88f7`
- Inspect APPID, svID, multicast MAC, VLAN/PCP, smpCnt, confRev, quality and PTP Announce/Sync/Follow_Up.
- Ensure the subscriber expects the identity and layout actually published by Studio.

A normal switch might not forward all multicast packets to the PC port; use the appropriate capture port.

## SCL and multiple virtual Merging Units

**M4 software now includes SCL-driven three-virtual-MU configuration and group START/STOP** with a shared PTP/sample timebase. This is **integrated software**, not merely a future plan. Sustained **3 × 4,000 fps aggregate physical wire-load acceptance** and broader v3.x board/PHY qualification remain separate verification tasks.

See the [Multi-SV engineering roadmap](PROCESS_BUS_MULTI_SV_SCL_NEXT_PHASE.md) and [SCL troubleshooting](SCL_TROUBLESHOOTING.md).

## Support and verification

[Installation](INSTALL.md) · [Support](../SUPPORT.md) · [Community testing](COMMUNITY_TESTING.md) · [Historical single-stream M3.H.3 evidence](releases/PROCESSBUS_AUTONOMOUS_M3H3_OFFICIAL_20261025.md)
