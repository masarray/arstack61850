# ARStack61850 SMV Injector GUI

This is the first operator-facing control surface for the ESP32-P4 Sampled Values injector runtime.

It intentionally replaces direct bench command entry with user-facing engineering controls while keeping the current firmware protocol unchanged underneath.

## What works now

- connect directly to the ESP32-P4 serial control port;
- START and STOP the deterministic publisher;
- edit Ia, Ib, Ic, In, Ua, Ub, Uc and Un while the stream is running;
- use engineering units instead of raw firmware units:
  - current: A RMS -> current wire counts for the current development profile;
  - voltage: V RMS -> voltage wire counts for the current development profile;
  - phase: degrees -> milli-degrees;
  - frequency: Hz -> millihertz;
- enable/disable channels;
- zero all outputs;
- restore a balanced three-phase bench state;
- live-apply values as the operator edits them, or pause live apply and use Apply/Apply all;
- display a local phasor preview;
- parse device telemetry for publisher rate, missed slots, failures and signal generation;
- load an SCL/CID/SCD/IID file locally and confirm that it is well-formed SCL plus count discovered `SampledValueControl` elements.

The SCL file picker is deliberately not a second IEC 61850 semantic engine. Full engineering-file normalization and `SvPublisherProfile` compilation remain in the C++ host engine and will be connected to this GUI through the P3-A1 device-profile bridge.

## Run

1. Flash the current `feature/sv-live-control-runtime` compatible firmware (the GUI branch is based on that runtime).
2. Exit `idf.py monitor` with `Ctrl+]`. The serial port cannot be owned by the monitor and GUI at the same time.
3. From the repository root, run:

```powershell
.\apps\smv_injector_gui\run.cmd
```

4. The launcher opens the local GUI and starts an HTTP server bound only to `127.0.0.1`.
5. Click **Connect device** and select the ESP32-P4 serial port.
6. Use **Start**, edit channel magnitude/phase/frequency, and observe the live stream.

The browser must expose the Web Serial API. The GUI is served from localhost because device serial APIs require a secure local context.

## Important unit boundary

The firmware bench protocol still carries raw units. The GUI hides those details from the operator:

```text
GUI current A RMS      -> SET ... current_counts ...
GUI voltage V RMS      -> SET ... voltage_counts ...
GUI phase deg          -> SET ... phase_mdeg ...
GUI frequency Hz       -> FREQ frequency_millihz
```

This means an operator can enter `-120` degrees and the GUI sends `-120000` milli-degrees. The earlier confusing direct-console behavior is no longer part of the normal operator workflow.

The current A/V conversion is explicitly tied to the development profile that is still hard-coded in the firmware. When the compiled SCL profile bridge lands, the GUI must obtain engineering-unit scaling from the resolved profile rather than assume a universal scale.

## Realtime rule

The GUI is control plane only. It does not synthesize the 4 kHz waveform and does not become a timing dependency.

```text
GUI / host
   |
   | control updates
   v
ESP32-P4 live state bank
   |
   v
fixed-point signal engine
   |
   v
prebuilt SV packet template
   |
   v
realtime Ethernet publisher
```

Disconnecting or closing the GUI must not make the PC responsible for sample timing. The embedded publisher remains the realtime authority.

## Next integration

The next product step is to connect the C++ SCL profile compiler to this surface so the workflow becomes:

```text
Import SCL/CID
    -> select SV stream
    -> validate normalized profile
    -> deploy immutable stream identity/layout
    -> edit live engineering values
    -> Start
    -> change values while running
    -> Stop
```

Stream identity/layout changes remain a STOP -> validate -> arm operation. Magnitude, phase, frequency, quality and enabled state are live controls.
