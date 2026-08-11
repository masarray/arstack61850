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
- live magnitude, phase, frequency, enable and quality controls;
- START / STOP / ZERO / SHOW and runtime telemetry;
- keyboard-first 4I + 4V matrix navigation;
- 3-phase linkage;
- persistent generated phasor and waveform preview;
- serial diagnostics surface.

The current deployment gate intentionally accepts only the embedded layout already supported by the ESP32-P4 runtime. Broader SCL structures may parse correctly while remaining `unsupported-layout`; the GUI does not guess them into the firmware profile.

## Truthfulness boundary

The phasor and waveform are **generated setpoint previews**. They visualize the configured engineering state. They are not independent Ethernet on-wire evidence.

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

1. native serial connect and SHOW/PROFILE SHOW;
2. development 4000 fps START/STOP and live SET/FREQ/ENABLE/QUALITY updates;
3. SCL Class-A profile deployment followed by PROFILE committed/armed confirmation;
4. 4800 fps profile deployment and observed ~4800 fps with the expected smpCnt cycle;
5. live magnitude/phase changes without unintended counter restart;
6. generated-vs-on-wire identity comparison using a trusted capture path.

## UI direction

The desktop visual target is a calm premium engineering instrument:

- Current and Voltage are the primary working matrices;
- SCL/profile information stays available without dominating the work surface;
- phasor and waveform remain persistent;
- Start/Stop remain fixed and obvious;
- values are optimized for keyboard operation;
- no browser/local-web-server dependency is required by the desktop application;
- visualization remains intentionally lightweight before any future custom `QQuickItem`/scene-graph optimization.
