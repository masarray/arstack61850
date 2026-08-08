# Deterministic SV injector control model

ARStack61850 treats the Sampled Values injector as one scenario engine with multiple transports and execution targets, not as separate Windows and ESP32 implementations.

## Execution model

Waveform state advances by integer **logical sample index**, not by UI time, wall-clock elapsed time, or the number of frames that a host scheduler happened to send successfully.

```text
scenario configuration
        |
        v
logical sample index -----> fixed-point waveform engine
        |                           |
        |                           v
        |                    8 x INT32 + quality
        |                           |
        v                           v
sample counter -------------> SV frame encoder
                                    |
                +-------------------+-------------------+
                |                   |                   |
          software loopback   Windows Npcap live   ESP32-P4 EMAC
          / PCAP reference    NIC oracle           physical injector
```

The scheduler answers **when** to attempt a frame. The deterministic engine answers **which sample** and **what values** belong to that logical instant.

## Control schema

Schema identifier:

```text
arstack-sv-injector-control-v1
```

Commands:

- `capabilities`
- `configure`
- `arm`
- `start`
- `stop`
- `status`
- `stats`

Lifecycle:

```text
IDLE -> CONFIGURED -> ARMED -> RUNNING -> STOPPED
                         |         |
                         +-> STOPPED
                                   |
                                  FAULT
```

`configure` replaces validated inactive configuration. It is rejected while `ARMED` or `RUNNING`. `arm` freezes the current configuration revision. `start` is accepted only from `ARMED`. `stop` ends an armed/running session without deleting the stored configuration. A new valid `configure` can recover from `FAULT`.

## First deterministic profile

- 4,000 sample instants/s
- 50 Hz nominal waveform
- 80 samples/cycle
- one ASDU per Ethernet frame
- `smpCnt` wrap at 4,000
- channel order `Ia, Ib, Ic, In, Va, Vb, Vc, Vn`
- one INT32 sample plus 32-bit quality word per channel
- 64-byte `seqData`
- APPID `0x4001`
- destination `01:0C:CD:04:00:01`
- `svID = ARSTACK61850_INJECTOR`
- dataset `ARSTACK61850/LLN0$PhsMeas1`
- untagged first proof
- `smpSynch=0` until synchronization evidence exists

The waveform engine uses a fixed-point phase accumulator, fractional phase remainder, fixed Q15 sine table and integer scenario interpolation. Its steady-state generation path does not require floating-point math or heap allocation.

## Windows reference injector

Executable:

```text
ariec61850_sv_injector
```

### Software loopback

```powershell
.\ariec61850_sv_injector.exe --scenario normal --frames 8000
.\ariec61850_sv_injector.exe --scenario protection-fault
.\ariec61850_sv_injector.exe --frames 8000 --drop-every 1000
.\ariec61850_sv_injector.exe --frames 8000 --pcap sv-loopback.pcap
```

### Windows Npcap live

```powershell
.\ariec61850_sv_injector.exe --list-interfaces

.\ariec61850_sv_injector.exe `
  --mode live `
  --interface "Ethernet" `
  --scenario normal `
  --continuous
```

The Windows live path uses real monotonic host timing with no catch-up burst. Logical waveform sequence remains deterministic, while physical Windows scheduling is explicitly best-effort.

A manual interoperability run has now shown Process Bus Insight decoding the live Windows stream on the Microsoft KM-TEST Npcap adapter with APPID `0x4001`, 50 Hz, the canonical 4I+4V mapping, and balanced 120-degree three-phase phasors. Treat this as **Windows live subscriber acceptance**, not ESP32-P4 hardware acceptance.

## ESP32-P4 device control

The ESP32-P4 firmware now uses the same `DeterministicSvInjector` and canonical 4I+4V profile as the Windows oracle. The earlier count-coded synthetic payload generator has been removed from the active P4 trial path.

The built-in ESP32-P4 USB Serial/JTAG controller is the primary control transport. ESP-IDF standard input/output is mapped to this interface, which appears as a `COMx` device on Windows.

Requests are one JSON object per line:

```json
{"command":"capabilities"}
{"command":"status"}
{"command":"configure","scenario":"normal"}
{"command":"configure","scenario":"protection-fault"}
{"command":"arm"}
{"command":"start"}
{"command":"stop"}
{"command":"stats"}
```

Responses are one-line records prefixed with:

```text
ARCTRL 
```

so a PC controller can distinguish control responses from ESP-IDF log output.

On boot the P4 validates the `normal` profile and waits in `CONFIGURED`. A normal run is:

```json
{"command":"status"}
{"command":"arm"}
{"command":"start"}
```

The publisher retains a pending logical sample if a scheduler poll occurs before the next frame is due. This prevents a timer wake-up from consuming a waveform sample without transmission. Encode failure, physical TX failure, missing injector state, or logical sample/`smpCnt` divergence transitions the device to `FAULT` rather than silently continuing with inconsistent state.

## Windows P4 control client

A standalone Windows client is provided:

```text
ariec61850_sv_device_ctl
```

It is built from `tools/device_ctl` and uses the same JSON contract over the P4 USB Serial/JTAG COM port.

Examples:

```powershell
.\ariec61850_sv_device_ctl.exe --device COM7 status
.\ariec61850_sv_device_ctl.exe --device COM7 arm
.\ariec61850_sv_device_ctl.exe --device COM7 start
.\ariec61850_sv_device_ctl.exe --device COM7 stats
.\ariec61850_sv_device_ctl.exe --device COM7 stop

.\ariec61850_sv_device_ctl.exe --device COM7 configure protection-fault
```

The client removes the `ARCTRL ` prefix and writes the returned JSON object to stdout, making it suitable for PowerShell, automated tests, and a future GUI.

## Built-in scenarios

### `normal`

Balanced three-phase current and voltage, neutral channels disabled, 50 Hz, held indefinitely.

### `protection-fault`

A deterministic 4,800-sample sequence:

1. prefault: 2,000 samples / 0.5 s
2. fault: 800 samples / 0.2 s, phase currents 4x and phase voltages 0.25x
3. recovery: 2,000 samples / 0.5 s

The sequence loops while the device remains running.

## Physical P4 acceptance

1. flash the CI-produced P4 firmware
2. connect USB Serial/JTAG and Ethernet
3. query `status`; expect `CONFIGURED`
4. issue `arm`, then `start`
5. capture `eth.type == 0x88ba` in Process Bus Insight/Wireshark
6. verify APPID, `svID`, dataset, 4I+4V waveform and `smpCnt` continuity
7. compare phase/value behavior against the Windows reference stream
8. request `stats` and retain timer/TX/lateness evidence
9. issue `stop` and verify the stream ceases
10. repeat with short, 10-minute, and one-hour runs

## Evidence levels

Do not conflate these results:

- `LOOPBACK/PASSED` — deterministic logical stream and software receive path accepted
- Windows live subscriber accepted — external host analyzer decoded the real Npcap stream
- `SIMULATED/PASSED` — embedded-host behavior and independent wire oracle accepted
- `ESP32P4/CROSS-COMPILED` — ESP-IDF toolchain compiled and linked the P4 firmware
- `FLASHABLE/READY` — complete P4 firmware artifact accepted
- P4 USB control accepted — physical COM request/response observed on hardware
- `HARDWARE/PASSED` — physical P4 Ethernet/timing/subscriber evidence recorded

A deterministic logical waveform does not by itself make Windows, FreeRTOS scheduling, Ethernet transmission, USB transport, or clock synchronization deterministic. Those remain separate measurable properties.

## Next source modes

After P4 control and physical SV acceptance:

1. explicit per-channel manual phasors
2. deterministic ramp/state sequencer
3. harmonic/DC/clipping controls
4. COMTRADE-to-SV sample-index playback
5. quality-state scripting
6. controlled duplicate/corrupt/jitter scheduling faults
7. SCL-derived stream identity and payload mapping
8. multi-ASDU packing
9. GOOSE injector scenarios
