# Deterministic SV injector control model

ARStack61850 treats the Sampled Values injector as one scenario engine with multiple transports and execution targets, not as separate Windows and ESP32 implementations.

## Why this differs from the ARSVIN publisher

ARSVIN remains a useful feature and UX reference: SCL-driven identity, manual/ramp/sequence sources, waveform shaping, COMTRADE replay, quality modes, timing-health evidence, PCAP and live Npcap workflows are all valuable product capabilities.

The deterministic injector changes the execution model. Waveform state is advanced by an integer **logical sample index**, not by UI time, wall-clock elapsed time, or the number of frames that a host scheduler happened to send successfully.

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
                      +-------------+-------------+
                      |                           |
                 software loopback           ESP32-P4 EMAC
                 / PCAP reference            physical injector
```

The scheduler answers **when** to attempt a frame. The deterministic engine answers **which sample** and **what values** belong to that logical instant. Keeping those responsibilities separate is required for reproducible PC-vs-MCU comparison and controlled loss/jitter testing.

## First control schema

Schema identifier:

```text
arstack-sv-injector-control-v1
```

The first Windows CLI exposes the capability contract with:

```powershell
ariec61850_sv_injector --capabilities
```

Initial command semantics reserved for the PC-to-device control plane are:

- `capabilities` — protocol, engine, profile and device feature discovery;
- `configure` — replace validated inactive configuration;
- `arm` — freeze the active scenario/configuration for a run;
- `start` — begin from a defined logical sample index;
- `stop` — stop generation without mutating the stored configuration;
- `status` — lifecycle and active-profile state;
- `stats` — frame, scheduler, transport and resource telemetry.

The transport is intentionally not part of the command semantics. Windows may use an in-process dispatcher, while the first ESP32-P4 control transport can use line-delimited JSON over USB serial. TCP/WebSocket or a GUI can be added later without redefining injector behavior.

## First deterministic profile

The first common PC/P4 profile remains deliberately narrow:

- 4,000 sample instants/s;
- 50 Hz nominal waveform;
- 80 samples/cycle;
- one ASDU per Ethernet frame;
- `smpCnt` wrap at 4,000 for the first profile;
- channel order `Ia, Ib, Ic, In, Va, Vb, Vc, Vn`;
- one INT32 sample plus 32-bit quality word per channel;
- 64-byte `seqData` payload;
- APPID `0x4001`;
- destination `01:0C:CD:04:00:01`;
- untagged first proof;
- `smpSynch=0` until synchronization evidence exists.

The waveform engine uses a fixed-point phase accumulator, fractional phase remainder, a fixed Q15 sine table and integer scenario interpolation. Its steady-state generation path does not require floating-point math or heap allocation.

## Windows reference CLI

First executable:

```text
ariec61850_sv_injector
```

Examples:

```powershell
# Two deterministic seconds at 4 kHz.
.\ariec61850_sv_injector.exe --scenario normal --frames 8000

# Protection-style prefault / fault / recovery sequence.
.\ariec61850_sv_injector.exe --scenario protection-fault

# Simulate a downstream loss after every 1000 successful TX frames.
.\ariec61850_sv_injector.exe --frames 8000 --drop-every 1000

# Retain exactly what the software receiver observed.
.\ariec61850_sv_injector.exe --frames 8000 --pcap sv-loopback.pcap

# Machine-readable app/CI evidence.
.\ariec61850_sv_injector.exe --frames 8000 --json
```

The initial `software-loopback` transport does not need Npcap, administrator rights, a physical NIC or an ESP32. The encoded raw Ethernet frame is synchronously passed through `RawEthernetPort`, decoded again, and checked for stream identity, payload equality and sample-counter continuity.

`--drop-every` injects loss **after** the producer transport reports success. This deliberately leaves the logical sample timeline advancing so the receiver must observe a `smpCnt` gap rather than a repeated old sample.

## Built-in scenarios in the first slice

### `normal`

Balanced three-phase current and voltage, neutral channels disabled, 50 Hz, held indefinitely. The CLI defaults to 8,000 frames (two virtual seconds).

### `protection-fault`

A deterministic 4,800-sample sequence:

1. prefault: 2,000 samples / 0.5 s;
2. fault: 800 samples / 0.2 s, phase currents 4x and phase voltages 0.25x;
3. recovery: 2,000 samples / 0.5 s.

The first preset is a regression scenario, not a calibrated secondary-injection claim.

## Next source modes

Once the fixed-point engine and software-loopback gate are green on Windows, GCC, Clang and the embedded profile, add sources in controlled slices:

1. explicit per-channel manual phasors;
2. deterministic ramp/state sequencer;
3. harmonic/DC/clipping controls;
4. COMTRADE-to-SV playback using sample-index mapping;
5. quality-state scripting;
6. controlled duplicate/corrupt/jitter scheduling faults;
7. SCL-derived stream identity and payload mapping;
8. multi-ASDU packing;
9. GOOSE injector scenarios.

COMTRADE playback must retain the original record/sample mapping independently from transmission timing; the network scheduler must never decide which COMTRADE sample is logically next.

## ESP32-P4 integration rule

The current ESP32-P4 firmware still uses its first synthetic payload generator. It should adopt `DeterministicSvInjector` only after the host and embedded regression gates accept the new engine.

The P4 application will then own only:

- ESP-IDF timer/task scheduling;
- EMAC/RMII Ethernet transport;
- USB-serial management transport;
- configuration persistence where required;
- telemetry and health reporting.

The scenario/waveform engine, payload semantics and SV wire codec remain shared with the Windows reference path.

## Evidence levels

Do not conflate these results:

- `LOOPBACK/PASSED` — deterministic logical stream and software receive path accepted;
- `SIMULATED/PASSED` — host/embedded regression and independent wire evidence accepted;
- `ESP32P4/CROSS-COMPILED` — real P4 toolchain accepted;
- `FLASHABLE/READY` — complete P4 firmware artifact accepted;
- `HARDWARE/PASSED` — physical Ethernet/timing evidence recorded.

A deterministic logical waveform does not by itself make Windows, FreeRTOS scheduling, Ethernet transmission or clock synchronization deterministic. Those remain separate measurable properties.
