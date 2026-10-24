# ARStack Process Bus Simulator M3.H — Stable Lab Simulator Edition (24 October 2026)

**Public stable engineering lab simulator release:** ESP32-P4 Ethernet Sampled Values publisher + IEC/IEEE 61850-9-3-style internal PTP grandmaster, operated through ARStack Studio on Windows.

This release is intended for isolated process-bus experiments, engineering education, SV/PTP decoder testing, and preparation for interoperability testing with protection relays. It does **not** require an external GPS, Omicron, or relay to operate. It deliberately advertises the **simulated global grandmaster profile** for laboratory purposes. This is a simulation capability, not independently verified UTC accuracy.

## What works and what was measured

* **Sampled Values:** one ASDU, 4 current + 4 voltage, 64-byte 8×(INT32+Quality), APPID 0x4000, SV ID `ARSTACK_SV01`, multicast `01:0C:CD:04:00:01`, 4000 frames/s and `smpCnt=0…3999`.
* **PTP server simulation:** Layer-2 Announce, Sync, hardware-timestamp-derived Follow_Up, 1 Hz cadence, domain 0, EMAC IEEE 1588 timebase with laboratory simulated Global GM / SOURCE profile. Hardware-assisted timestamp and SV phase/MAC observation paths remain enabled.
* **Studio:** 4I+4V scenario controls, explicit START/STOP, PTP SOURCE, `SMPSYNCH AUTO`, waveform/phasor preview, packet capture, verified ESP32-P4 serial identity, and faster automatic USB reconnect without automatic SV START.

**Independent on-wire capture provided by the engineer, 2026-10-24, M3.H firmware `a922e9bdba4e57a4`:**

| Evidence | Result |
|---|---|
| Captured SV span | 40.762934 seconds |
| SV packets | 163,048 |
| All `smpSynch` | **2 (100%; no 0 or 1)** |
| Missing/duplicate/reordered `smpCnt` | 0 / 0 / 0 |
| SV malformed / nonzero quality | 0 / 0 |
| Mean SV rate | 3999.884 fps |
| Host timestamp spacing median | 250 microseconds |
| PTP Announce | 44; seq 13–56, no gaps |
| PTP Sync + Follow_Up | 43 + 43; seq 13–55, paired, no gaps |
| PTP interval | 1 Hz approximately |
| Board telemetry | 0 missed, 0 TX failures; same boot and source during capture |
| Earlier M3.G sync flutter | **Not observed in M3.H capture** |

PCAP SHA-256 `7764c1477c357eb0e08b552073bd5cedd0816d8617c1fd587fff6cdf62e1b190`. The raw engineer capture is not bundled with the public installer.

**Capture qualifier:** operator requested 120 s, but SV STOP interrupted capture after ~40.76 s, so Studio correctly marked the *120-second capture request* `INVALID_CAPTURE`. The 40.76 s recorded segment itself is contiguous. The stop cause is not established by that file. This does not demonstrate 120-second endurance or STOP/START endurance.

## What is intentionally not claimed

* Not tested with a connected relay/merging-unit subscriber: **real IED PTP lock, SV acceptance, absence of relay alarms, multi-vendor interoperability, and Pdelay exchange with a peer are unverified** until an IED becomes available.
* Simulated global PTP quality and `smpSynch=2` are **lab-emulation metadata**; without a real timing reference they do not certify UTC/GNSS traceability or sub-microsecond physical accuracy.
* Desktop-host PCAP timestamps are not hardware timestamp calibration.
* Only the supported 4I+4V, 4 kHz SV profile is advertised as implemented on ESP32-P4.

## Install / first run

1. Download Windows Setup EXE (recommended) or Portable ZIP. Both include Qt dependencies, a pinned ESP32-P4 flasher, **matched firmware** and checksums.
2. Connect the ESP32-P4 to USB OTG and Ethernet on an **isolated test network**. Install bundled firmware using **Advanced → Firmware**; verify the board build ID equals **`a922e9bdba4e57a4`**. Merely installing Studio does not update board firmware.
3. Select **PTP Source** / `INTERNAL_GM_ONLY` and **`SMPSYNCH AUTO`**. Leave PTP enabled while stopping/starting SV. Set the 4I+4V amplitudes and phase angles; click **Start**.
4. Use ARStack Studio Capture, ARSAS, DigSubAnalyzer, or Wireshark to inspect multicast SV EtherType `0x88BA` and PTP EtherType `0x88F7`.
5. A successful lab session should have consecutive `smpCnt`, continuous 4 kHz samples, `smpSynch=2` once the on-board phase/MAC evidence is acquired, and paired 1 Hz PTP Announce/Sync/Follow_Up. Stop is intentional and produces no SV packet.

**Electrical/network caution:** Never connect an unsolicited SV publisher or additional PTP grandmaster to a live substation process bus. Use an isolated VLAN/switch or dedicated laboratory LAN.

## Reproducible binary identity

Software source and firmware image are pinned to **`a922e9bdba4e57a4e88406eb83344455dd8ff142`**, the SHA with **10/10 GitHub CI workflows passing**. Packaging workflow downloads immutable artifacts from successful run **`40061206222`**, verifies their exact checksums, and publishes without recompiling or changing any firmware timing code.

* Setup EXE SHA-256: `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e`
* Portable ZIP SHA-256: `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f`
* ESP32-P4 BIN SHA-256: `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607`

This is a **public laboratory simulator release**, not a certified MU/PTP reference device. Those are different product categories; external certification is not a release gate for this emulator.
