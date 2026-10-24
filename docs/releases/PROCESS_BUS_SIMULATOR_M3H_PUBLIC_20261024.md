# ARStack Process Bus Simulator M3.H — Stable Lab Edition

**24 October 2026 · Public stable release**

Generate IEC 61850 Sampled Values and emulate PTP synchronization using **ARStack Studio and ESP32-P4 Ethernet hardware**. This edition combines a 4-current + 4-voltage SV publisher, internal laboratory PTP grandmaster simulation, and hardware-assisted phase synchronization.

## What's new

- **SV + PTP in one lab setup** — real Ethernet SV at 4,000 frames/s alongside Layer-2 PTP Announce, Sync and Follow_Up.
- **Hardware-assisted synchronization** — ESP32-P4 EMAC timestamp/phase evidence supports `SMPSYNCH AUTO` and qualified lab `smpSynch=2`.
- **Simple desktop control** — adjust 4I+4V magnitudes and phase, use explicit Start/Stop, preview waveforms and capture traffic.
- **Matched firmware included** — Windows Setup and Portable packages contain the ESP32-P4 firmware, firmware manifest and installation utility.

## Download and try it

**[Latest stable release and assets](https://github.com/masarray/arstack61850/releases/latest)** · [Windows installation](../INSTALL.md) · [First-run tutorial](../GETTING_STARTED.md)

1. Install ARStack Studio from Windows Setup EXE or use the Portable ZIP.
2. Connect a compatible ESP32-P4 Ethernet board on an isolated lab network.
3. Open **Configuration → Firmware** and install the firmware from **this same release**. Check the device build ID **`a922e9bdba4e57a4`**.
4. Choose **4I+4V Quick Start**, select **PTP SOURCE / INTERNAL_GM_ONLY** and **SMPSYNCH AUTO**, adjust the waveforms and press **Start**.
5. Inspect SV (`0x88BA`) and PTP (`0x88F7`) with Studio Capture, ARSAS, DigSubAnalyzer or Wireshark.

No external GPS or protection relay is needed for the isolated-lab emulator. **Never connect an unsolicited SV publisher or PTP grandmaster to a live substation process bus.**

## Measured Ethernet results

The independent M3.H engineer capture from 24 October 2026 recorded:

| Measurement | Result |
| --- | --- |
| Continuous SV capture | 40.762934 seconds |
| SV frames | **163,048** |
| Measured SV rate | **3,999.884 frames/s** |
| `smpSynch=2` | **100%** of the recorded SV |
| Missing / duplicate / reordered counters | **0 / 0 / 0** |
| Malformed SV / nonzero quality | **0 / 0** |
| Host-capture spacing median | 250 microseconds |
| PTP Announce | 44 |
| PTP Sync / Follow_Up | 43 / 43 paired |

**Reference Ethernet SV profile:** one ASDU, 4I+4V, eight INT32+Quality pairs (64 bytes), APPID `0x4000`, `svID=ARSTACK_SV01`, multicast `01:0C:CD:04:00:01`, `smpCnt=0…3999`.

PTP source emulation uses Layer-2 Announce/Sync and hardware-timestamp-derived Follow_Up at approximately 1 Hz (domain 0). This is an internally simulated laboratory grandmaster profile; `smpSynch=2` denotes qualified **local emulation evidence**, not external UTC/GNSS calibration.

### Measurement and reproduction notes

The raw capture is identified by SHA-256 `7764c1477c357eb0e08b552073bd5cedd0816d8617c1fd587fff6cdf62e1b190`. It was provided as independent engineer evidence and is not bundled in the installer.

The operator's requested 120-second capture was interrupted by a **SV STOP** after 40.763 seconds, so the 120-second capture request was marked `INVALID_CAPTURE`. The recorded 40.763-second segment itself is contiguous and valid. This file alone should not be represented as a 120-second endurance record. Extended internal observations and the archived PCAP are different evidence sources.

External relay/BCU subscriber acceptance, multi-device PTP behavior and physical UTC time accuracy are outside the measurements in this specific capture. Engineers with external equipment are welcome to [share lab interoperability observations](../COMMUNITY_TESTING.md).

## Build identity and downloadable artifacts

The published Studio and firmware pair is pinned to source SHA `a922e9bdba4e57a4e88406eb83344455dd8ff142` (embedded Build ID `a922e9bdba4e57a4`); its source build passed 10/10 CI workflows. Integration to `main` later passed 14/14 candidate workflows through [PR #299](https://github.com/masarray/arstack61850/pull/299), without changing this shipped firmware/Studio source.

| Artifact | SHA-256 |
| --- | --- |
| `ARStack-ProcessBus-M3H-win-x64-setup.exe` | `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e` |
| `ARStack-ProcessBus-M3H-win-x64-portable.zip` | `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f` |
| `ARStack-ESP32P4-ProcessBus-M3H-firmware.bin` | `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607` |

The release includes `firmware-manifest.json` and `SHA256SUMS.txt`. The supplied Windows packages include a pinned board flasher; users do not need an embedded build toolchain.

[Download the latest public release](https://github.com/masarray/arstack61850/releases/latest) · [Help and community testing](../../SUPPORT.md)
