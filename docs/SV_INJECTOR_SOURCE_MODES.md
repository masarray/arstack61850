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

The first implementation accepts realtime edits only while the runtime source is in an indefinite hold slot. This prevents an edit in the middle of a ramp from silently jumping from the ramp target instead of the current effective value. Interruptible ramps will require an explicit effective-profile snapshot.

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

A finite program ends in an indefinite hold of the final state. This is deliberate: a malformed or incomplete upload must not accidentally create an endless test. Repeat counts, conditional transitions and trigger sources should be orchestration features layered above this bounded primitive.

The initial public control protocol exposes manual and ramp edits first. A sequence upload transaction should be added as a separate bounded protocol slice with validation before activation.

## Recorded waveform replay

Recorded waveform replay is a different source type from generated phasors. The protocol core already has host-side COMTRADE parsing; device replay should not parse arbitrary files inside the realtime publisher task.

The intended architecture is:

```text
record file
    |
    v
host parser / channel mapping
    |
    v
normalized sample blocks
    |
    v
bounded upload or streaming transport
    |
    v
P4 replay ring
    |
    v
logical sample index -> SV payload
```

The host owns file parsing, engineering-unit/channel mapping, resampling policy and validation. The device owns bounded buffering, deterministic playback order, underrun detection and Ethernet transmission.

The line-delimited JSON control channel is suitable for commands and configuration, but bulk replay samples should use a compact framed binary transport so control traffic cannot dominate the realtime path.

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

The fixed-capacity runtime ring is allocated outside the high-priority publisher task stack. No source edit requires steady-state heap growth.

If source state, logical sample index or publisher `smpCnt` diverge, the device should fail closed into a reported fault instead of continuing with ambiguous test data.
