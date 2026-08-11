# ARStack61850 SMV Injector GUI

Operator-facing control plane for the ESP32-P4 Sampled Values injector.

The GUI is not a waveform generator. The PC compiles engineering intent and sends bounded control/profile updates; the ESP32-P4 remains the realtime publisher.

## Current workflow

```text
Import SCL / CID / SCD / IID
    -> C++ SCL parser
    -> normalized SvPublisherProfile
    -> select SV stream
    -> resolve warnings / counter policy
    -> configure test scaling when SCL does not establish it
    -> Deploy to device while STOPPED
    -> Start
    -> live I/U/phase/frequency/quality changes
    -> Stop
```

The browser does not implement a second IEC 61850 semantic parser. `run.cmd` builds the focused C++ profile compiler and starts a localhost-only Python bridge. Uploaded engineering data is written to a temporary local file only for compilation and removed immediately after the request.

## Supported device profile in this phase

The host parser can inspect broader SCL SV structures, but the first ESP32-P4 deployment bridge intentionally accepts only the physically proven 4I+4V wire layout:

- one ASDU;
- `SmpPerSec` with an absolute publisher rate;
- explicit/validated sample-counter modulus;
- 16 ordered leaves as 8 `INT32 value + Quality` pairs;
- 64-byte sample payload;
- configurable destination MAC, APPID, VLAN/PCP, svID, confRev and supported ASDU field presence.

A structurally valid but unsupported layout remains visible in the GUI and is blocked from deployment. The firmware does not guess a payload layout.

## Counter-policy rule

SCL sampling rate does not universally prove the `smpCnt` wrap policy. When the compiler can only infer a rate-sized candidate, the GUI shows Class B and requires explicit confirmation from an applicable profile rule, independent wire evidence, or engineering knowledge before deployment becomes Class A.

## Engineering scaling

Generic SCL does not always establish physical scaling. The GUI therefore exposes current and voltage counts-per-unit conversion for the active test profile instead of claiming a universal scale. This scaling affects only engineering-value conversion in the control plane; it does not change realtime scheduling.

## Run

1. Flash firmware from the same `feature/smv-scl-profile-bridge` branch.
2. Exit `idf.py monitor` with `Ctrl+]`; the GUI and monitor cannot own the serial port simultaneously.
3. From the repository root run:

```powershell
.\apps\smv_injector_gui\run.cmd
```

The launcher incrementally builds the smart SCL profile tool into `build-smv-gui`, starts the local control-plane server on `127.0.0.1:8765`, and opens the GUI.

4. Click **Connect device** and select the ESP32-P4 serial port.
5. Import an engineering file, select an SV stream, resolve any Class-B counter-policy requirement, then **Deploy to device** while STOPPED.
6. Press **Start**. Magnitude, phase, frequency, quality and enabled state remain live controls while running.

## Realtime boundary

```text
engineering file
      |
      v
C++ profile compiler ---- GUI engineering values
      |                           |
      +-----------+---------------+
                  |
                  v
           ESP32-P4 control state
                  |
          immutable wire profile
          + mutable signal state
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

Profile identity/layout changes require `STOP -> validate -> deploy -> arm`. Live signal values do not rebuild BER and do not restart `smpCnt`.

The current rational GPTimer scheduler supports runtime publisher rates without rounding a fractional-microsecond period into a permanently wrong fixed interval; for example, 4800 events/s uses a repeating 208/208/209 microsecond schedule at the 1 MHz timer domain.
