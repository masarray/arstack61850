# ARStack61850 SMV Injector GUI

Operator-facing control plane for the ESP32-P4 Sampled Values injector.

The GUI is not the realtime waveform generator. The PC compiles engineering intent and sends bounded profile/control updates; the ESP32-P4 remains the realtime publisher.

For the current completion boundary and retained bench evidence, see [`../../docs/SMV_INJECTOR_STATUS.md`](../../docs/SMV_INJECTOR_STATUS.md).

## Current workflow

```text
Import SCL / CID / SCD / IID
    -> C++ SCL parser
    -> normalized SvPublisherProfile
    -> select SV stream
    -> resolve Class A/B/C diagnostics
    -> confirm sample-counter policy when required
    -> configure explicit test scaling when SCL does not establish it
    -> Deploy to device while STOPPED
    -> immutable profile armed on ESP32-P4
    -> Start
    -> live I/U/phase/frequency/quality changes
    -> Stop
```

The browser does not implement a second IEC 61850 semantic parser. `run.cmd` builds the focused native C++ profile compiler and starts a localhost-only Python bridge. Engineering data is written to a temporary local file only for compilation and removed immediately after the request.

## Functional status

The SCL-driven injector workflow is physically proven for the current supported 4I+4V device profile.

A real engineering-file-derived 4800 fps profile has been compiled, validated, deployed and run on the ESP32-P4 with retained device telemetry showing approximately 4800 samples/s, zero missed slots and zero canonical TX failures in the observed run.

This is a functional-completion statement, not a claim of universal SCL support or formal IEC conformance.

## Supported device profile in this phase

The host parser can inspect broader SCL SV structures, but the first ESP32-P4 deployment bridge intentionally accepts only the physically proven 4I+4V wire layout:

- one ASDU;
- `SmpPerSec` with an absolute publisher rate;
- explicit/validated sample-counter modulus;
- 16 ordered leaves as 8 `INT32 value + Quality` pairs;
- 64-byte sample payload;
- channel order `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`;
- configurable destination MAC, APPID, VLAN/PCP, `svID`, `confRev` and supported ASDU field presence.

A structurally valid but unsupported layout remains visible in the GUI and is blocked from deployment. The firmware does not guess a payload layout.

## Counter-policy rule

SCL sampling rate does not universally prove the `smpCnt` wrap policy. When the compiler can only infer a rate-sized candidate, the GUI shows Class B and requires explicit confirmation from an applicable profile rule, independent wire evidence, or engineering knowledge before deployment becomes Class A.

## Engineering scaling

Generic SCL does not always establish physical scaling. The GUI therefore exposes current and voltage counts-per-unit conversion for the active test profile instead of claiming a universal scale. This scaling affects only engineering-value conversion in the control plane; it does not change realtime scheduling.

## Canonical stream and diagnostic visibility

The active SCL profile is the canonical standards-facing stream identity. During the current bench phase, firmware also emits a separate untagged broadcast diagnostic mirror:

```text
canonical
  -> SCL-derived multicast destination / APPID / svID / VLAN

diagnostic mirror
  -> broadcast / APPID 0x4F01 / svID AR_DIAG_SV1 / untagged
```

The mirror exists because the currently tested host USB-Ethernet capture path does not reliably expose multicast/priority-tagged canonical traffic. If a raw analyzer shows `AR_DIAG_SV1`, it is seeing the diagnostic mirror, not evidence that the SCL profile failed to deploy.

The diagnostic mirror follows the active publisher rate and configuration revision but intentionally keeps a different identity.

## Run

1. Flash firmware from the same `feature/smv-scl-profile-bridge` branch.
2. Exit `idf.py monitor` with `Ctrl+]`; the GUI and monitor cannot own the serial port simultaneously.
3. From the repository root run:

```powershell
.\apps\smv_injector_gui\run.cmd
```

The launcher:

- detects a native Windows C++ compiler by capability rather than Visual Studio product year;
- incrementally builds the smart SCL profile tool into `build-smv-gui`;
- starts the local control-plane server on `127.0.0.1:8765`;
- opens the GUI.

4. Click **Connect device** and select the ESP32-P4 serial port.
5. Import an engineering file and select an SV stream.
6. Resolve any Class-B counter-policy requirement.
7. **Deploy to device** while STOPPED.
8. Press **Start**. Magnitude, phase, frequency, quality and enabled state remain live controls while running.

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

The rational GPTimer scheduler supports runtime publisher rates without permanently rounding fractional-microsecond periods. For example, 4800 events/s uses a repeating 208/208/209 microsecond schedule in the 1 MHz timer domain.

## Next validation surface

The preferred next hardware validation path is an independent raw Ethernet analyzer, potentially using a second ESP32-P4 board in promiscuous/all-multicast receive mode with application-layer VLAN parsing. That analyzer should compare configured SCL truth against observed canonical wire truth without relying on the current Windows USB-NIC capture path.