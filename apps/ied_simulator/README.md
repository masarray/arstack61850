# ARStack IED Simulator

`arstack_ied_simulator` is the Qt/QML desktop shell for the portable IEC 61850
server stack. It keeps the commissioning flow and the live value workspace in
one executable while reusing the repository SCL parser and MMS server.

## Current product flow

1. Import an SCL, CID, SCD, IID, or ICD engineering file.
2. Review parsed IEDs and model/service counts.
3. Select the local interface, MMS port, GOOSE option, and file-service folder.
4. Start the bundled IEDScout-compatible `ariec61850_ied_simulator_server`.
5. Use the runtime workspace to switch between imported IEDs and edit resolved
   DataSet/report/GOOSE members with type-aware controls, quality, origin, and
   undo history.

The desktop server projects the imported SCL logical-device, logical-node, and
resolved leaf-value hierarchy into a bounded host MMS model (up to 128 objects).
Static DataSets retain their ordered SCL member references. While an association
is open, a value applied in the runtime editor is atomically published to the
server backing model and is visible to a subsequent MMS Read without forcing the
client to reconnect. TCP/COTP/ACSE/MMS activity and value synchronization are
reported back to the GUI.

This phase does not claim dynamic DataSet creation/deletion, URCB/BRCB report
delivery, client-originated control/write handling, GOOSE transmission, or host
file-service transfer. Those services stay disabled or diagnostic-only until
their protocol paths have independent interoperability coverage.

## Windows build

```powershell
cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ied-simulator-qt --target arstack_ied_simulator --parallel
```

The app target also builds `ariec61850_ied_simulator_server`. Keep both
executables beside each other when packaging.

The discovery status becomes green only after the server emits listener-ready
evidence. Use **Copy diagnostics** to capture the endpoint, IEDScout association
profile, process state, model counts, and recent protocol activity.

## Runtime interoperability test

The integration test starts the real Qt controller, imports the fixture SCL,
starts its child MMS server, applies a value in the runtime editor, and reads
that value through an independent MMS association:

```powershell
python apps/ied_simulator/test_gui_live_value.py `
  --app build-ied-simulator-qt/arstack_ied_simulator.exe `
  --read-probe build-ied-simulator-qt/ariec61850_mms_read_probe.exe `
  --scl tests/fixtures/scl/minimal-station.scd
```

## Deterministic UI smoke test

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
build-ied-simulator-qt/arstack_ied_simulator.exe --smoke-test
```

For visual QA, `--scl <path> --screenshot <png>` captures the commissioning
state. Add `--runtime` to capture the running workspace.
