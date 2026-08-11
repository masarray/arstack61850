# ARStack61850 SMV Injector — Native Slint UX Shell

Native desktop UX shell for the ARStack61850 Sampled Values injector.

The visual hierarchy is inspired by professional substation test workstations such as OMICRON DANEO Control: persistent engineering modes, a compact command ribbon, a device/system tree, a large working canvas and a contextual inspector. It does **not** copy DANEO branding, icons, assets or product-specific UI resources.

## Why this exists

The existing `apps/smv_injector_gui` browser control plane remains the proven functional path for SCL profile compilation, ESP32-P4 deployment and live SMV signal control. This directory starts the native migration without disturbing that working path.

The shell is intentionally dense and engineering-first:

- compact 12–14 px visual hierarchy instead of oversized cards;
- persistent top work modes: Measurement, Signal Source, Network, Supervision, Injection and Observation;
- shallow command ribbon for Import SCL, Connect, Validate, Deploy, Start and Stop;
- left Test System tree for the ESP32-P4 and selected SV stream;
- center live-signal workspace for 4I + 4V engineering values;
- right Active Stream / Live Apply inspector;
- bottom status strip for device, profile and publisher state.

## Current scope

This first increment is a **UX shell and C++ integration seam**, not yet a replacement for the proven browser GUI.

`AppWindow` exposes two backend callbacks:

```text
action-requested(action)
live-apply-requested(channel, field, value)
```

The current C++ entry point logs these callbacks. The next integration increment can bind them to the existing native profile compiler and ESP32-P4 control path while preserving the current realtime boundary.

Live signal fields already model the intended operator workflow: magnitude and phase can be edited per channel, Enter emits a live-apply request, and the Apply control emits the current magnitude and phase values. Numeric range/unit validation belongs in the native controller before a value is allowed onto the device control path.

## Build

Requirements:

- CMake 3.21+
- C++20 compiler
- Rust 1.92+ when Slint is built from source
- Slint 1.17.1 (installed package or FetchContent fallback)

### Windows quick start

From the repository root:

```powershell
.\apps\smv_injector_slint\run.cmd
```

The launcher detects Ninja when available, otherwise uses the installed CMake/Visual Studio generator, builds the Release target and opens the shell.

### Manual build

```powershell
cmake -S apps/smv_injector_slint -B build-smv-slint -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-smv-slint --target arstack_smv_slint_shell
```

If a Slint C++ SDK is already installed, point `CMAKE_PREFIX_PATH` at it. To forbid source fetching and require an installed SDK:

```powershell
cmake -S apps/smv_injector_slint -B build-smv-slint -DARSTACK_SLINT_FETCH=OFF
```

## Migration plan

1. **Shell parity** — compile and stabilize the workstation layout on Windows/Linux.
2. **Native profile controller** — invoke the existing C++ SCL parser/profile compiler directly instead of through the browser bridge.
3. **Native device controller** — move serial connection, deploy/start/stop and telemetry behind a C++ service class.
4. **Validated live apply** — port the proven mutable signal-state semantics and enforce engineering limits before write-through.
5. **Observation modules** — add waveform, phasor, event and FFT views without coupling rendering to the realtime publisher.
6. **Retire duplicate UI path only after parity** — keep the current browser GUI available until native functional and bench parity is demonstrated.

## Architecture boundary

```text
Slint views
    |
    v
C++ presentation/controller layer
    |----------------------|
    v                      v
SCL/profile compiler    device control/telemetry
    |                      |
    +----------+-----------+
               v
        ESP32-P4 control state
               |
       immutable wire profile
       + mutable signal state
               |
               v
        realtime SMV publisher
```

The UI must never become the realtime waveform scheduler. Profile identity/layout changes remain a STOP → validate → deploy operation; mutable signal values remain the live-control path.
