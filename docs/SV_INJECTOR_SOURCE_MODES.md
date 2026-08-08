# Sampled Values injector source modes

The injector is designed as a sample-index-driven signal source with multiple source modes behind one common control plane. User interfaces and transports may change, but they must not become the waveform clock.

## Core rule

The scheduler answers **when a frame may be transmitted**. The source engine answers **which logical sample and values belong to that sample index**.

A control update therefore never rewinds `smpCnt`, never derives waveform phase from UI refresh timing, and never sends a burst of overdue logical samples to catch up with a delayed host or task.

## Realtime manual source

Manual mode is the interactive source for live phasor/value adjustment while the stream is running.

Supported channel fields are designed around the existing fixed-point signal profile:

- enabled/disabled state;
- RMS counts;
- DC counts;
- phase in millidegrees;
- frequency in millihertz;
- harmonic amplitude in permyriad;
- harmonic order;
- clipping in permyriad;
- IEC 61850 quality word.

The control command may contain only the fields that changed. The publisher task copies the active hold profile, applies the sparse edit, stages the new profile in the fixed-capacity runtime ring, and changes to it on a logical sample boundary.

Example control messages:

```json
{"command":"set-channel","channel":"Ia","rms":2500}
{"command":"set-channel","channel":"Va","phaseMilliDeg":15000}
{"command":"set-channel","channel":"Ib","frequencyMilliHz":49500,"quality":0}
```

The first implementation accepts realtime edits only while the runtime source is in an indefinite hold slot. This prevents an edit in the middle of a ramp from silently jumping from the ramp target instead of the current effective value. Interruptible ramps require an explicit effective-profile snapshot and remain a later extension.

The Windows device-control CLI provides the same sparse operations:

```text
set-channel <channel> <field> <value>
ramp-channel <channel> <field> <value> <durationSamples>
```

`--emit-request` prints the exact request JSON without opening a device. CI uses this mode to regression-test the public command contract on Windows and Linux.

## Ramp source

Ramp mode uses the same integer sample timeline as the waveform engine. Duration is expressed in samples, not milliseconds from the UI thread.

```json
{"command":"ramp-channel","channel":"Ia","rms":5000,"durationSamples":4000}
```

At 4,000 sample instants/s, a duration of 4,000 samples is one logical second. The runtime program creates:

1. the current hold state;
2. a linear transition state with the requested sample duration;
3. an indefinite hold of the final target.

The current implementation can ramp RMS, DC, frequency, harmonic amplitude and clipping through the deterministic engine's integer interpolation. Phase changes are applied at a state boundary through the existing phase-offset transition mechanism; a dedicated continuous phase-slew model can be added separately.

## State sequencer

`InjectorRuntimeProgram` provides a fixed-capacity finite state sequence primitive. Each state contains the complete eight-channel profile, a duration in logical samples, and either a step or linear transition.

Sequence programming is transactional. Draft states do not affect the running source until the final commit succeeds:

```text
sequence-begin
sequence-state-begin <durationSamples> [step|linear]
sequence-set-channel <channel> <field> <value>
sequence-state-commit
... repeat state construction ...
sequence-commit
```

`sequence-abort` discards a draft. Each newly opened state inherits the previous committed draft state, so only changed channels need to be edited. The builder is fixed-capacity and validates the state before activation.

A finite program ends in an indefinite hold of the final state. This is deliberate: a malformed or incomplete upload must not accidentally create an endless test. Repeat counts, conditional transitions and external trigger policies belong to a higher orchestration layer and can be added without changing the bounded state primitive.

The ESP32-P4 control path implements this transaction inside the publisher-task ownership boundary. `sequence-commit` is the only operation that stages the prepared program into the active runtime ring.

## Recorded waveform replay

Recorded waveform replay is a different source type from generated phasors. File parsing does not belong inside the realtime publisher task.

The implemented first replay pipeline is:

```text
COMTRADE record
    |
    v
host parser + semantic channel map
    |
    v
ARSVRPL1 normalized replay bundle
8 x (INT32 + quality), 64 bytes/sample
    |
    +----> Windows live replay oracle
    |
    v
bounded device replay ring
    |
    v
future proven bulk upload/preload transport
```

`ariec61850_sv_replay_prepare` loads a COMTRADE record, maps recognized analog channels into the canonical `Ia, Ib, Ic, In, Va, Vb, Vc, Vn` order, converts supported engineering units to base A/V counts, writes quality zero for the first slice, and emits an `ARSVRPL1` bundle. Missing mapped channels are zero-filled.

Replay bundle v1 intentionally accepts only an exact 4,000 Hz source. It does not silently resample a recording. A future resampler must be a separately tested host-side transformation with explicit policy and evidence.

The bundle header is fixed and portable. Each following frame contains the exact 64-byte `seqOfData` payload that can be copied into the SV ASDU without host-endian reinterpretation.

`ariec61850_sv_replay_live` validates the bundle on Windows or Linux and, on Windows with Npcap, can replay it onto a selected Ethernet adapter at the same best-effort 4 kHz live pacing used by the reference publisher. `--continuous` repeats the recorded sample sequence until `Ctrl+C`.

A separate Python oracle in CI validates the bundle magic, header, byte count, channel order, first payload values and quality words without using the C++ bundle decoder.

The MCU side has a fixed-capacity `InjectorReplayRing` with explicit overflow and underrun counters. The ring is the intended boundary between bulk delivery/preload and the 4 kHz publisher task.

At 4,000 samples/s and 64 payload bytes/sample, the replay payload alone is 256,000 bytes/s before transport framing. The existing line-delimited control protocol is therefore kept for commands and configuration; it is not treated as a proven bulk replay data plane. A high-throughput binary upload/preload transport must be measured before device replay is labeled realtime-capable.

## Future source families

The same source boundary is intended to support additional modes without changing the SV encoder:

- multi-channel manual phasors;
- multi-channel ramps and sweeps;
- finite and repeating state sequences;
- recorded waveform replay;
- harmonic/DC/clipping profiles;
- scripted quality changes;
- controlled network fault scenarios;
- SCL-derived stream identity and dataset mapping.

Every mode must preserve the same separation between logical sample generation, physical scheduling and transport evidence.

## Runtime safety model

Runtime source changes are single-writer operations inside the publisher task. The control task only parses input and queues typed commands. This avoids concurrent mutation of waveform state.

The fixed-capacity runtime ring and sequence builder are allocated outside the high-priority publisher task stack. No manual, ramp or sequence edit requires steady-state heap growth.

Replay buffering is separately bounded and reports overflow/underrun rather than hiding missing samples.

If source state, logical sample index or publisher `smpCnt` diverge, the device should fail closed into a reported fault instead of continuing with ambiguous test data.
