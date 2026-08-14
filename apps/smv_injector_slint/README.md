# ARStack61850 SMV Injection Studio

Native Slint desktop workstation for fast IEC 61850-9-2 Sampled Values injection.

The interface combines the fast numeric workflow of professional test equipment with a modern, calm three-view workspace. It uses original ARStack61850 styling and does not copy OMICRON or ARSVIN product assets.

## Operator workflow

- Quick Manual table for 4 voltage and 4 current channels.
- Arrow keys move between magnitude, angle, and frequency cells.
- Enter validates and commits; focus selects the complete value for immediate replacement.
- Invalid values remain focused and never reach the device command path.
- Live Apply transmits validated edits to the next coherent generation.
- Phasor and waveform views update from the effective editor state.
- Start, running timer, Stop, TX sequence, and publisher state remain visible during injection.
- Status History and Overload Monitor live in a collapsible lower dock.
- Lucide icons provide a consistent command vocabulary.

## Native controller

The C++ controller owns validation and Windows serial transport. The current command protocol is:

```text
SET <channel> <magnitude-counts> <phase-millidegrees> 0
FREQ <millihertz>
ENABLE <channel> <0|1>
START
STOP
ZERO
SHOW
```

Default validation limits are 0-10,000 A RMS, 0-1,000,000 V RMS, -360 to 360 degrees, and 1-1,000 Hz. Profile identity and layout changes remain a STOP -> validate -> deploy operation; mutable values use the live-control path.

## Windows quick start

Requirements: CMake 3.21+, a C++20 MSVC toolchain, and Rust 1.92+ when Slint is fetched from source.

From the repository root:

```powershell
.\apps\smv_injector_slint\run.cmd
```

On systems with a `D:` drive, the launcher defaults to:

```text
D:\Build\arstack61850-smv-slint
D:\Tools\Slint\1.17.1
```

Override either location with `ARSTACK_BUILD_DIR` or `ARSTACK_SLINT_DEPS_DIR`. The CMake target copies `slint_cpp.dll` beside the executable after every successful Windows build, preventing a missing-runtime startup error.

## Manual build

```powershell
cmake -S apps/smv_injector_slint -B build-smv-slint -DCMAKE_BUILD_TYPE=Release
cmake --build build-smv-slint --target arstack_smv_slint_shell --config Release
```

Set `-DARSTACK_SLINT_FETCH=OFF` to require an installed Slint C++ package.

## Architecture boundary

```text
Slint workstation views
        |
        v
C++ validation and presentation controller
        |---------------------------|
        v                           v
profile compiler             serial device control
        |                           |
        +-------------+-------------+
                      v
             ESP32-P4 mutable state
                      |
                      v
              realtime SMV publisher
```

The UI is never the realtime waveform scheduler.

## Third-party assets

Toolbar and dock icons are from [Lucide](https://lucide.dev/) under the ISC License. The license text is stored in `assets/lucide/LICENSE`.
