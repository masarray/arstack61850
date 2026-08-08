# Deterministic SV injector control model

ARStack61850 treats the Sampled Values injector as one sample-index-driven signal engine with multiple source modes, transports and execution targets. The user interface, management transport and physical scheduler are deliberately separated from waveform state.

## Execution model

Waveform/source state advances by integer **logical sample index**, not by UI refresh time, wall-clock elapsed time, or the number of frames a host scheduler happened to send successfully.

```text
source configuration / program
            |
            v
logical sample index -----> deterministic source engine
            |                        |
            |                        v
            |                 8 x INT32 + quality
            |                        |
            v                        v
sample counter ----------------> SV frame encoder
                                      |
                 +--------------------+--------------------+
                 |                    |                    |
           software loopback     Windows live NIC     ESP32-P4 EMAC
           / PCAP reference      reference path       physical injector
```

The source engine answers **which logical sample and values** belong to a sample index. The scheduler answers **when a physical frame may be attempted**. A late scheduler must not decide which waveform sample logically comes next.

## Control schema

Schema identifier:

```text
arstack-sv-injector-control-v1
```

Lifecycle:

```text
IDLE -> CONFIGURED -> ARMED -> RUNNING -> STOPPED
                                   |
                                   v
                                  FAULT
```

Lifecycle commands:

- `capabilities`
- `configure`
- `arm`
- `start`
- `stop`
- `status`
- `stats`

`configure` replaces validated inactive configuration. It is rejected while `ARMED` or `RUNNING`. `arm` freezes the current configuration revision. `start` is accepted only from `ARMED`. `stop` ends an armed/running session without deleting the stored configuration. A new valid `configure` can recover from `FAULT`.

## First deterministic profile

- 4,000 sample instants/s
- 50 Hz nominal generated waveform
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

The generated-waveform engine uses a fixed-point phase accumulator, fractional phase remainder, fixed Q15 sine table and integer scenario interpolation. Its steady-state generation path does not require floating-point math or heap allocation.

## Windows reference injector

Executable:

```text
ariec61850_sv_injector
```

Software loopback examples:

```powershell
.\ariec61850_sv_injector.exe --scenario normal --frames 8000
.\ariec61850_sv_injector.exe --scenario protection-fault
.\ariec61850_sv_injector.exe --frames 8000 --drop-every 1000
.\ariec61850_sv_injector.exe --frames 8000 --pcap sv-loopback.pcap
```

Windows live examples:

```powershell
.\ariec61850_sv_injector.exe --list-interfaces

.\ariec61850_sv_injector.exe `
  --mode live `
  --interface "Ethernet" `
  --scenario normal `
  --continuous
```

The Windows live path uses a real monotonic host clock with no catch-up burst. Logical waveform sequence remains deterministic, while physical Windows scheduling is explicitly best-effort.

A manual lab run has already shown an independent external SV analyzer decoding the Windows live stream on a local virtual Ethernet adapter with APPID `0x4001`, 50 Hz, the canonical 4I+4V mapping and balanced 120-degree three-phase phasors. Treat this as **Windows live subscriber acceptance**, not ESP32-P4 hardware acceptance.

## ESP32-P4 device control

The ESP32-P4 firmware uses the same `DeterministicSvInjector` and canonical 4I+4V profile as the Windows reference path.

The initial management transport is line-delimited JSON over USB Serial/JTAG standard input/output. It appears as a serial device on the host operating system.

Requests are one JSON object per line. Responses are one-line records prefixed with:

```text
ARCTRL 
```

so a PC controller can distinguish control responses from firmware log output.

On boot the P4 validates the normal source and waits in `CONFIGURED`. A normal run is:

```json
{"command":"status"}
{"command":"arm"}
{"command":"start"}
```

The publisher retains a pending logical sample if a scheduler poll occurs before the next frame is due. This prevents an early wake-up from consuming a waveform sample without transmission. Encode failure, physical TX failure, missing injector state, or logical sample/`smpCnt` divergence transitions the device to `FAULT` rather than silently continuing with ambiguous test data.

## Realtime manual source

The normal source can be edited while `RUNNING` without resetting the global logical sample index or `smpCnt`.

Sparse control examples:

```json
{"command":"set-channel","channel":"Ia","rms":2500}
{"command":"set-channel","channel":"Va","phaseMilliDeg":15000}
{"command":"set-channel","channel":"Ib","frequencyMilliHz":49500,"quality":0}
```

Supported fields include:

- enabled state
- RMS
- DC offset
- phase
- frequency
- harmonic amplitude/order
- clipping
- quality word

The control task parses and queues typed commands. The publisher task is the single writer of the runtime source program. A sparse edit copies the active hold profile, changes only the requested fields and stages the new profile on a logical sample boundary.

An edit is rejected as `source-busy` while a finite transition is actively in progress. This is intentional: an interruptible transition needs an explicit snapshot of the current effective interpolated state before a new target can be defined.

## Sample-indexed ramp

Ramp duration is expressed in logical samples, not UI milliseconds.

```json
{"command":"ramp-channel","channel":"Ia","rms":5000,"durationSamples":4000}
```

At 4,000 sample instants/s, 4,000 samples is one logical second. The runtime program creates the transition and a final indefinite hold. RMS, DC, frequency, harmonic amplitude and clipping use deterministic integer interpolation. Phase target changes currently use state-boundary phase-offset behavior rather than a continuous phase-slew model.

## Transactional state sequencer

Finite state programs are built as a transaction so a partially uploaded program can never become active.

Protocol flow:

```text
sequence-begin
sequence-state-begin <durationSamples> [step|linear]
sequence-set-channel <channel> <field> <value>
sequence-state-commit
... repeat state construction ...
sequence-commit
```

`sequence-abort` discards the draft.

Each newly opened state inherits the previous committed draft state, so only changed channels need to be transmitted. The fixed-capacity builder validates each state before accepting it. `sequence-commit` is the only point that stages the prepared sequence into the active runtime source ring.

A finite sequence ends in an indefinite hold of its final state. Repeat counts, external triggers and conditional branches remain higher-level orchestration features and are not silently implied by the bounded primitive.

## Windows device-control client

Executable:

```text
ariec61850_sv_device_ctl
```

Lifecycle examples:

```powershell
.\ariec61850_sv_device_ctl.exe --device COM7 status
.\ariec61850_sv_device_ctl.exe --device COM7 arm
.\ariec61850_sv_device_ctl.exe --device COM7 start
.\ariec61850_sv_device_ctl.exe --device COM7 stats
.\ariec61850_sv_device_ctl.exe --device COM7 stop
```

Realtime examples:

```powershell
.\ariec61850_sv_device_ctl.exe --device COM7 set-channel Ia rms 2500
.\ariec61850_sv_device_ctl.exe --device COM7 set-channel Va phase 15000
.\ariec61850_sv_device_ctl.exe --device COM7 ramp-channel Ia rms 5000 4000
```

Sequence examples:

```powershell
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-begin
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-state-begin 2000 step
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-set-channel Ia rms 1000
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-state-commit
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-state-begin 800 linear
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-set-channel Ia rms 4000
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-state-commit
.\ariec61850_sv_device_ctl.exe --device COM7 sequence-commit
```

`--emit-request` generates the exact JSON without opening a serial port. CI uses this path to test the public command contract on Windows and Linux.

## Atomic grouped-edit foundation

Interactive GUIs often need several phase/channel parameters to become active on the same sample boundary. The core therefore includes a revisioned full-profile transaction builder that can snapshot a profile, apply multiple channel edits and represent either an immediate or ramped commit target.

This builder is covered by embedded regression tests. It is **not yet exposed as a device control command**, so grouped multi-channel edits should not be described as an active public device feature until the transactional wire commands are added.

## Recorded-waveform replay

Recorded waveform replay is kept separate from generated phasor sources.

Current host pipeline:

```text
recorded waveform
      |
      v
host parser + semantic channel map
      |
      v
ARSVRPL1 normalized replay bundle
8 x (INT32 + quality), 64 bytes/sample
      |
      +------> Windows live replay reference
      |
      v
bounded device replay-ring primitive
```

`ariec61850_sv_replay_prepare` parses COMTRADE input on the host, maps recognized analog channels into `Ia, Ib, Ic, In, Va, Vb, Vc, Vn`, converts supported engineering units to base A/V counts and emits an `ARSVRPL1` bundle. Missing mapped channels are zero-filled. Replay bundle v1 accepts an exact 4,000 Hz source; it does not silently resample.

`ariec61850_sv_replay_live` validates the same bundle cross-platform and can transmit it through a selected Windows Npcap adapter. A finite run plays the bundle once; `--continuous` repeats it until interrupted.

The bundle stores the exact 64-byte SV payload per logical sample. An independent Python oracle in CI verifies magic, header fields, expected byte count, channel order, first sample values and quality words without calling the C++ bundle decoder.

The MCU side provides a fixed-capacity replay ring with explicit overflow and underrun counters. It is the intended boundary between bulk delivery/preload and the 4 kHz publisher task.

At 4,000 samples/s and 64 payload bytes/sample, replay payload alone is 256,000 bytes/s before framing. The line-delimited JSON management channel is therefore **not** treated as a proven bulk replay data plane. Device replay requires a separately measured binary upload/preload transport before it can be labeled realtime-capable.

## Built-in generated scenarios

### `normal`

Balanced three-phase current and voltage, neutral channels disabled, 50 Hz, held indefinitely. This is the editable manual/ramp/sequence source.

### `protection-fault`

A deterministic 4,800-sample generated sequence:

1. prefault: 2,000 samples / 0.5 s
2. fault: 800 samples / 0.2 s, phase currents 4x and phase voltages 0.25x
3. recovery: 2,000 samples / 0.5 s

The built-in scenario loops while the device remains running. Runtime manual/sequence editing is intentionally kept on the normal source path in the first implementation.

## Physical P4 acceptance

1. flash the CI-produced P4 firmware
2. connect USB management and Ethernet
3. query `status`; expect `CONFIGURED`
4. issue `arm`, then `start`
5. capture `eth.type == 0x88ba` with an independent receiver
6. verify APPID, `svID`, dataset, 4I+4V waveform and `smpCnt` continuity
7. change manual values while running and verify sample-boundary behavior
8. run a sample-indexed ramp and verify transition duration
9. build and commit a finite state sequence and verify its state timing
10. request `stats` and retain timer/TX/lateness evidence
11. issue `stop` and verify the stream ceases
12. repeat short, 10-minute and one-hour soak runs

## Evidence levels

Do not conflate these results:

- `LOOPBACK/PASSED` — deterministic logical stream and software receive path accepted
- Windows live subscriber accepted — an independent external analyzer decoded a real live-NIC stream
- `SIMULATED/PASSED` — embedded-host behavior and independent wire oracle accepted
- `REPLAY-BUNDLE/PASSED` — normalized recorded-waveform bundle accepted by an independent oracle
- `REPLAY-LIVE/PASSED` / `REPLAY-LIVE/STOPPED` — host replay transmitted through the live Windows reference path
- `ESP32P4/CROSS-COMPILED` — ESP-IDF toolchain compiled and linked the P4 firmware
- `FLASHABLE/READY` — complete P4 firmware artifact accepted
- P4 USB control accepted — physical request/response observed on hardware
- `HARDWARE/PASSED` — physical P4 Ethernet/timing/subscriber evidence recorded

A deterministic logical source does not by itself make Windows, FreeRTOS scheduling, Ethernet transmission, USB transport or clock synchronization deterministic. Those remain separate measurable properties.

## Remaining source/control slices

- expose atomic grouped multi-channel edits through the control protocol
- explicit continuous phase-slew model
- interruptible ramp based on current effective state
- repeat counts, triggers and conditional sequencer orchestration
- proven high-throughput binary replay upload/preload
- quality-state scripting beyond direct field edits
- controlled duplicate/corrupt/jitter network fault modes
- SCL-derived dynamic stream identity and payload mapping
- multi-ASDU packing
- additional IEC 61850 test modes after SV hardware acceptance
