# ESP32-P4-ETH SMV Injector — P0/P1 + P2 50 Hz bring-up

This target brings up the current ESP32-P4 Ethernet development board using the internal EMAC + onboard RMII PHY and publishes IEC 61850 Sampled Values through the existing ARStack process-bus codec.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the standards-first SCL/profile/PTP direction after the current P2 bring-up.

## Current hardware result

P0/P1 and the first P2 realtime path have been proven on real hardware:

- ESP32-P4 rev 1.3 boots and drives the onboard RMII PHY.
- Raw Layer-2 transmit reaches a host capture NIC.
- Independent packet capture recognizes the generated `0x88BA` payload as IEC 61850 Sampled Values.
- The GPTimer-driven publisher sustains about 4000 samples/s on the current bench with device-side TX failures and missed timer notifications at zero in the observed run.
- A raw process-bus analyzer reconstructs balanced 50 Hz three-phase voltage/current waveforms from the diagnostic stream and reports continuous sequence integrity.
- The tested USB-Ethernet host capture path reports roughly the correct average rate but shows large individual arrival-time excursions. Treat those timestamps as capture-path evidence, not device-TX timing evidence.
- The tested host capture path does not reliably expose the normal IEC 61850 multicast/priority-tagged stream. The firmware therefore keeps a lab-only untagged broadcast diagnostic stream so waveform/timing validation can continue without pretending that stream is the standards-facing output.

## P2 50 Hz publisher

The default firmware enables the first realtime publisher foundation:

- 50 Hz nominal frequency.
- 80 samples/cycle.
- 4000 frames/s.
- 250 us GPTimer period.
- canonical `smpCnt` wraps at 4000.
- prebuilt fixed-size frame templates; no BER encode or heap allocation in the hot TX loop.
- fixed 2-byte `smpCnt` and 4-byte `confRev` patch fields in the runtime template.
- balanced three-phase sine generator.
- dataset order `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`.
- each channel is `(INT32 sample, UINT32 quality)`.
- current payload scaling: 1 mA/count.
- voltage payload scaling: 10 mV/count.
- default phase current: 1.000 A RMS.
- default phase-to-neutral voltage: approximately 57.74 V RMS (100 V line-to-line).
- `smpSynch=0` remains truthful until a measured PTP/1PPS lock exists.

### Standards-facing canonical stream

The current canonical stream is aligned with `01_SV_Stream_4I+4V_(9-2LE).scd` stream #1:

- destination `01:0C:CD:04:00:01`;
- APPID `0x4000`;
- `svID=MU01_SV1`;
- `confRev=1`;
- VLAN PCP 4 / VID 0 enabled by default;
- one ASDU;
- dataset-reference field omitted because the supplied SCL has `dataSet=false`;
- explicit `smpRate` / `smpMod` fields omitted because the supplied SCL has `sampleRate=false`.

This is the interoperability target. The build still does **not** claim final strict 9-2LE/IEC 61869-9 conformance; exact profile rules, synchronization, canonical multicast/VLAN capture on a trusted path and relay interoperability remain acceptance gates.

### Lab diagnostic stream

For the current host-capture limitation the firmware also publishes a separate diagnostic stream:

- destination `FF:FF:FF:FF:FF:FF`;
- APPID `0x4F01`;
- `svID=AR_DIAG_SV1`;
- untagged;
- explicit `smpRate=4000` and `smpMod=SmpPerSec` so a raw analyzer can reconstruct the waveform without SCL binding;
- independent normal 16-bit sample counter.

The diagnostic identity is intentionally different from the canonical stream so analyzers do not merge both sequences. This stream is a capture/debug aid, not a process-bus compliance target.

## Board wiring used

| Signal | GPIO |
|---|---:|
| PHY RESET | 51 |
| MDC | 31 |
| MDIO | 52 |
| RMII REF_CLK input | 50 |
| TX_EN | 49 |
| TXD0 | 34 |
| TXD1 | 35 |
| CRS_DV | 28 |
| RXD0 | 29 |
| RXD1 | 30 |
| PHY address | 1 |

## Toolchain

Use ESP-IDF 5.5.x. If the board contains an early ESP32-P4 engineering sample, select the matching chip revision configuration before flashing. For a detected ESP32-P4 v1.x/v2.x board, enable the ESP-IDF pre-v3 chip-revision option and rebuild. Do not bypass a revision mismatch with `--force`.

The currently tested board is ESP32-P4 rev 1.3, so its local `sdkconfig` must retain the pre-v3 revision setting.

## Build and flash

```bash
cd embedded/esp32p4_smv_injector
idf.py build
idf.py -p <PORT> flash monitor
```

For the tested bench the port is currently `COM3`.

After boot, expected serial output includes:

```text
P2 waveform: 50 Hz, 80 samples/cycle, 4000 fps, period=250 us
SCL canonical: svID=MU01_SV1 ... APPID=0x4000 ...
LAB mirror: svID=AR_DIAG_SV1 APPID=0x4F01 ... explicit smpRate=4000 smpMod=SmpPerSec
P2 timing: samples=... MC ok=... fail=... mirror ok=... fail=... missed=... wake_late_us[min/mean/max]=...
```

## Capture / analyzer checks

For the current host capture adapter, use the diagnostic stream first:

```text
eth.type == 0x88ba
```

Expected diagnostic behavior:

- roughly 4000 decoded SV frames/s;
- continuous 16-bit `smpCnt` progression;
- visible non-zero three-phase current and voltage samples;
- reconstructed 80 samples/cycle at 50 Hz;
- balanced A/B/C waveform and phasors separated by 120 degrees.

The standards-facing multicast stream is sent in parallel. A trusted capture path should validate:

```text
eth.dst == 01:0c:cd:04:00:01
vlan.etype == 0x88ba
```

Do not use host USB-Ethernet arrival jitter as the final device timing metric. The next P2 timing gate is ESP32-P4 EMAC IEEE1588v2 hardware TX timestamps and/or an independent reference capture path.

## Configuration

Run `idf.py menuconfig` and open **ARStack ESP32-P4 SMV Injector** to control:

- live TX;
- P2 50 Hz realtime mode;
- canonical VLAN PCP4/VID0;
- lab-only untagged diagnostic stream;
- optional `0x88B5` capture probes;
- current RMS in mA;
- voltage RMS in mV.

Use an isolated bench network or a direct capture/TAP connection. Do not connect this development image to an operational substation process bus.

## Remaining P2 acceptance work

Before calling the publisher production-grade:

- validate 60 seconds of 4000 fps capture with zero unexplained `smpCnt` gaps;
- enable ESP32-P4 EMAC IEEE1588v2 hardware time and collect hardware TX timestamp evidence;
- calculate scheduled-vs-actual TX min/max/mean/P95/P99 timing error;
- validate canonical VLAN multicast on a trusted capture path;
- add 60 Hz / 4800 fps scheduling;
- add IEC/IEEE 61850-9-3 PTP synchronization before changing `smpSynch` from 0;
- move from hard-coded bench constants to an SCL/profile-compiled runtime configuration;
- validate exact IEC 61850-9-2 / IEC 61869-9 / legacy 9-2LE behavior against sourced profile rules and independent relay/tool testing.

Tracked work:

- P2 timing evidence: issue #21
- standards-first SCL/profile/PTP engine: issue #24