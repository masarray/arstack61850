# ARStack Studio — native Qt SMV Injector

`apps/arstack_studio` is the native Qt 6 / C++ / QML operator surface for the ESP32-P4 Sampled Values injector.

This branch is intentionally stacked on `feature/smv-scl-profile-bridge` so the desktop application follows the latest SCL/profile and embedded-runtime behavior instead of freezing an older web-GUI snapshot.

## Architecture

```text
SCL / CID / SCD / IID
  -> ARIEC61850 SclParser
  -> SvPublisherProfileCompiler
  -> shared ESP32-P4 deployment support classifier
  -> Qt SclProfileModel
  -> operator selects / validates stream
  -> Qt DeviceController (QSerialPort)
  -> current ESP32-P4 PROFILE + live-control protocol
  -> deterministic publisher on ESP32-P4
```

The Qt application is a presentation and control plane. The PC is not the 4000/4800 fps realtime clock and does not replace the ESP32-P4 publisher.

## Latest workflow carried into Qt

- direct native SCL/CID/SCD/IID import using the repository C++ parser;
- resolved SampledValueControl stream selection;
- compatibility Class A / B / C;
- fail-closed ESP32-P4 device-support classification;
- explicit sample-counter-modulus confirmation for Class-B candidate profiles;
- explicit current counts/A and voltage counts/V test scaling;
- immutable profile deployment while STOPPED;
- profile identity: svID, data set, destination MAC, APPID, VLAN/PCP, confRev, publisher rate and counter modulus;
- optional DataSet / SampleRate ASDU-field flags already supported by the current device bridge;
- native serial 115200 8N1 control using Qt SerialPort;
- smart USB discovery with firmware-handshake verification and sequential
  read-only fallback probing when Windows exposes only generic COM metadata;
- explicit `IDENTIFY` device identity backed by the ESP32-P4 factory MAC, so
  the normal workflow never asks the operator to choose a COM port;
- live magnitude, phase, editable AC frequency, DC, enable and quality controls;
- mandatory live apply after verified connection; no mode switch can leave the
  operator editing a stale local copy while output is running;
- an ARSVIN-compatible CT-saturation test shape with bounded DC offset,
  harmonic, order, and clip controls in Expert mode;
- START / STOP / ZERO / SHOW and runtime telemetry;
- keyboard-first 4I + 4V matrix navigation;
- 3-phase linkage;
- persistent generated phasor and waveform preview;
- PTP Lab TX status, bounded runtime configuration, start/stop, and counters;
- serial diagnostics surface.

The current deployment gate intentionally accepts only the embedded layout already supported by the ESP32-P4 runtime. Broader SCL structures may parse correctly while remaining `unsupported-layout`; the GUI does not guess them into the firmware profile.

## Truthfulness boundary

The phasor and waveform are **generated setpoint previews**. They visualize the configured engineering state. They are not independent Ethernet on-wire evidence.

CT Saturation is a deterministic test-waveform approximation, not a calibrated
electromagnetic CT model. Its preview and parameters are labeled accordingly.

A later Process Bus monitor can add an observed trace and generated-vs-observed comparison without changing this distinction.

`smpSynch` remains an embedded timing/synchronization truth. The GUI must not claim synchronized output until the runtime has measured synchronization evidence.

## Build

Qt 6.5+ is required with:

- Qt Core / Gui / Qml / Quick / QuickControls2
- Qt SerialPort
- a C++20 compiler
- CMake 3.24+

### Windows example

From the repository root with a Qt-enabled developer shell:

```powershell
cmake -S apps/arstack_studio -B build-arstack-studio -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-arstack-studio --target arstack_studio --parallel
.\build-arstack-studio\arstack_studio.exe
```

On the Windows development workstation used for this phase, Qt is installed at
`D:\Qt\6.8.3\msvc2022_64`. Build and launch with one command:

```powershell
.\apps\arstack_studio\run-windows.ps1
```

Use `-NoLaunch` for a build-only verification. The launcher discovers the
installed Visual Studio C++ toolchain with `vswhere`, reuses its bundled Ninja,
and does not modify the user's global `PATH`.

## P2 fast-workflow ribbon

The compact Home / View / Engineering ribbon carries ARSVIN's short operator
loop into the native Qt surface without copying its visual styling. Home keeps
Balanced, Zero, Check, Configure, Deploy, Start, and Stop one action away. View
owns dock visibility and detachment; Engineering owns SCL, smart discovery,
PTP refresh, and diagnostics. Device and output state stay in the header/footer
instead of being repeated as large workflow cards.

Keyboard operators can use `Ctrl+O`, `Ctrl+B`, `Ctrl+0`, `Ctrl+K`, `Ctrl+D`,
`F5`, and `F6`. Check reports readiness through the existing non-modal status
surface; it does not bypass the deployment gate.

## P2 dock shell

The main window now behaves as a compact engineering shell rather than a fixed
form. Engineering source, profile, scaling, quick setup, and runtime details
live in a dedicated Smart / Expert Configuration window instead of consuming
permanent space beside the injector. Phasor View and Waveform View are separate
right-side docks; each can be hidden or detached into its own real Qt window.
Status History and Output Monitor share a bottom dock that starts collapsed.
The View ribbon restores closed panels, detaches either plot, and expands the
bottom monitor.

The shell uses nested draggable splitters: editor vs preview docks, Phasor vs
Waveform, and workspace vs bottom monitor. Every pane keeps a protective
minimum size, while its split can be resized directly like an engineering MDI
shell. The complete monitor header is clickable for smooth expand/collapse.

Smart mode is the default and guides device recognition, PTP inspection, and SV
profile selection. Expert mode exposes the detailed SV profile compiler plus
bounded PTP domain, transportSpecific, VLAN, interval, and peer-delay controls.
Deploy and Start remain disabled until the serial response proves the connected
port is the ARStack ESP32-P4 injector.

PTP remains explicitly labeled as a laboratory timing companion. The firmware
serial surface now supports `PTP SHOW`, `PTP START`, `PTP STOP`, and bounded
`PTP CONFIG`; none of these claims external grandmaster lock or changes the
truthful `smpSynch` boundary.

The safety-critical editor remains the stable center pane. Split sizes are
operator-adjustable, while arbitrary drag-reordering and layout persistence
remain follow-up work; detaching a view must never move or duplicate the output
controls.

For redistribution, package the required dynamically linked Qt runtime libraries with the normal Qt deployment tooling and review the applicable Qt/LGPL distribution obligations for the chosen module set.

## CI

`.github/workflows/arstack-studio-qt.yml` performs:

1. standalone Qt configure;
2. native C++ / QML build;
3. headless QML launch smoke test;
4. retained configure/build/launch logs on every run.

A green desktop CI proves that the application builds and QML instantiates in the hosted environment. It does **not** replace physical ESP32-P4 / Ethernet validation.

## Physical acceptance still required

Before this desktop GUI is treated as a proven replacement for the current bench surface, retain hardware evidence for at least:

1. native serial `IDENTIFY`, SHOW, and PROFILE SHOW using updated firmware;
2. development 4000 fps START/STOP and live SET/FREQ/ENABLE/QUALITY updates;
3. SCL Class-A profile deployment followed by PROFILE committed/armed confirmation;
4. 4800 fps profile deployment and observed ~4800 fps with the expected smpCnt cycle;
5. live magnitude/phase changes without unintended counter restart;
6. DC and CT-saturation SHAPE commands with observed on-wire sample values;
7. PTP Lab TX configuration/counters and explicit non-lock truth labeling;
8. generated-vs-on-wire identity comparison using a trusted capture path.

## UI direction

The desktop visual target is a calm premium engineering instrument:

- Inter Variable is bundled under the SIL Open Font License and used for all
  interface, numeric, status, and plot-label text; no monospace family is used;
- compact Lucide SVG icons are bundled with upstream license notices for the
  ribbon and dock controls;
- Current and Voltage are the primary working matrices;
- SCL/profile information stays available without dominating the work surface;
- phasor and waveform remain persistent;
- Start/Stop remain fixed and obvious;
- values are optimized for keyboard operation;
- no browser/local-web-server dependency is required by the desktop application;
- visualization remains intentionally lightweight before any future custom `QQuickItem`/scene-graph optimization.
