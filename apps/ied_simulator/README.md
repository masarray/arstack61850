# ARStack IED Simulator

`arstack_ied_simulator` is the Qt/QML desktop shell for the portable IEC 61850
server stack. It keeps the commissioning flow and the live value workspace in
one executable while reusing the repository SCL parser and MMS server.

## Current product flow

1. Import an SCL, CID, SCD, IID, or ICD engineering file.
2. Review parsed IEDs and model/service counts.
3. Select the local interface, MMS port, GOOSE option, and file-service folder.
4. Start the bundled `ariec61850_static_ied_server`.
5. Use the runtime workspace to switch between imported IEDs and edit resolved
   DataSet/report/GOOSE members with type-aware controls, quality, origin, and
   undo history.

The current MMS process still exposes the bounded static prototype model from
`ariec61850_static_ied_server`; the Qt model browser is driven by the imported
SCL. Runtime SCL-to-MMS object materialization, GOOSE transmission, and host
file-service mapping remain explicit follow-up work and are not claimed here.

## Windows build

```powershell
cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ied-simulator-qt --target arstack_ied_simulator --parallel
```

The app target also builds `ariec61850_static_ied_server`. Keep both
executables beside each other when packaging.

## Deterministic UI smoke test

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
build-ied-simulator-qt/arstack_ied_simulator.exe --smoke-test
```

For visual QA, `--scl <path> --screenshot <png>` captures the commissioning
state. Add `--runtime` to capture the running workspace.
