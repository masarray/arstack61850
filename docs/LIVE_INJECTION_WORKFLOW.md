# Live Sampled Values Injection Workflow

ARStack61850 targets an instrument workflow in which engineering intent is imported once, validated into a deterministic runtime profile, and signal values can then be changed while the publisher is running without rebuilding firmware or changing the canonical stream identity.

## User workflow

```text
Import SCL / CID / SCD / IID
        |
        v
Select and validate an SV stream
        |
        v
Resolve Class-A publisher profile
        |
        v
Configure initial signal values
        |
        v
Deploy profile to dedicated hardware
        |
        v
START
        |
        +---- live value / phase / frequency changes
        |
        +---- quality/state changes
        |
        +---- scenario transitions
        |
        v
STOP
```

The desktop/host side is the engineering and control plane. The ESP32-P4 is the deterministic realtime execution plane.

## Separation of immutable and mutable state

A running stream has two categories of state.

### Immutable while armed/running

These values define the stream/profile and are not edited in the per-sample hot path:

- destination MAC;
- APPID;
- VLAN / PCP;
- svID and DataSet field-presence policy;
- confRev;
- nofASDU;
- sample-rate / sample-mode policy;
- validated sample-counter policy;
- ordered wire layout and payload widths;
- optional ASDU field policy.

Changing an immutable property requires STOP -> validate/rebuild template -> ARM/START. This prevents half-updated frames.

### Mutable while running

These values are intentionally live:

- per-channel magnitude or raw wire-count target;
- per-channel phase;
- signal frequency where supported by the active signal model;
- quality values;
- scenario/fault state;
- waveform source selector and transition state.

A mutable update must become visible atomically at a sample boundary. The realtime task never parses text/XML, allocates memory, takes a host-facing mutex, or rebuilds BER structures.

## Runtime state machine

```text
EMPTY
  -> PROFILE_VALIDATED
  -> ARMED
  -> RUNNING
  -> STOPPED

RUNNING --live signal update--> RUNNING
RUNNING --profile change------> reject; STOP required
```

`START` begins deterministic publication from a validated/armed profile. `STOP` stops canonical publication without destroying the profile. Live signal updates are accepted during RUNNING and committed as one coherent generation.

## Coherent live update model

The host/control task writes a complete next signal state into an inactive bank and then publishes a generation/index atomically. The realtime publisher copies one coherent generation at the start of a sample and uses only that snapshot for the whole payload.

```text
control task                         realtime publisher
------------                         ------------------
copy active state
edit inactive bank
validate values
publish generation  ----atomic---->  copy coherent generation
                                     synthesize one sample
                                     patch prebuilt frame
                                     transmit
```

This prevents mixed-channel updates such as Ia from a new command while Ib/Ic still belong to an older command.

## Signal synthesis

The first live engine uses a fixed-point phase accumulator plus a precomputed sine lookup table. Floating-point initialization is outside the realtime sample path; the sample hot path uses bounded integer work.

A continuous phase accumulator allows magnitude, phase and frequency changes without restarting `smpCnt`. Frequency changes alter the next phase increment while preserving phase continuity. If realtime notifications are missed, both sample-count state and signal phase advance by the missed-slot count rather than replaying stale samples.

The first bench control surface uses the serial console for direct hardware testing. It is a development transport only. The product control plane will use a versioned bounded command protocol and the same live-state semantics.

## Safety and interoperability

- canonical profile identity is never changed by a live signal-value command;
- profile changes require explicit stop/re-arm;
- `smpSynch` remains evidence-driven and is never promoted by a user value edit;
- SCL/profile scaling and semantic mapping stay host/profile responsibilities; raw wire-count control remains available for exact interoperability testing;
- unsupported channel semantics are rejected rather than guessed;
- diagnostic streams remain separate from canonical output.

## Acceptance path

1. Load a vendor-neutral SCL regression profile and resolve it to Class A.
2. Configure initial 4I+4V values.
3. Start the ESP32-P4 publisher and verify canonical identity/layout.
4. Change Ia/Uc/phase/frequency while RUNNING.
5. Verify the next coherent generation is visible on wire without sequence reset, malformed payload, or mixed-generation channels.
6. Verify STOP suppresses canonical TX and START resumes according to the runtime policy.
7. Repeat for 4000 and 4800 events/s profile families once compiled-device profiles are integrated.
