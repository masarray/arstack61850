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
   SCL leaf values with type-aware controls, quality, origin, and undo history.

The native SCL parser is the structural-model compiler for the simulator. The
desktop server projects the selected IED's logical-device, logical-node, and
resolved DO/DA/BDA leaf hierarchy into the bounded host MMS object model. The
host validation ceiling is 8192 MMS objects; this is a validation bound, not a
static allocation in the embedded core. Static DataSets retain their ordered SCL
member references.

While an association is open, a value applied in the runtime editor is
atomically published to the server backing model and is visible to a subsequent
MMS Read without forcing the client to reconnect. Quality and Timestamp values
use their IEC 61850 MMS wire types rather than a display-string substitute.
Multiple associations can be served concurrently. TCP/COTP/ACSE/MMS activity
and value synchronization are reported back to the GUI.

ReportControl blocks are carried from SCL into the same simulator endpoint.
The current regression-covered reporting slice includes URCB `RptEna` + GI
with unsolicited `InformationReport`, and BRCB `RptEna` + DataSet-member
change capture after `BufTm` with buffered `InformationReport` and `EntryID`.
The BRCB adapter uses the existing bounded native BRCB runtime and only commits
a staged retained entry after the complete frame is accepted by the socket.
Simulator BRCB retained storage is currently association-local; cross-association
retained replay/reconnect persistence is not claimed by this application layer
yet even though the portable core has the lower-level ownership/replay
primitives.

This phase does not yet claim dynamic DataSet creation/deletion in the simulator,
full RCB attribute/configuration parity, client-originated IEC 61850 control
handling, GOOSE transmission, or host file-service transfer. The GOOSE and file
selectors in the current shell must therefore not be interpreted as evidence
that those server-side services are active. Those paths remain separate parity
slices and require independent wire regressions before being marked complete.

## Windows build

```powershell
cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ied-simulator-qt --target arstack_ied_simulator --parallel
```

The app target also builds `ariec61850_ied_simulator_server` and the external
URCB/BRCB regression probes. Keep the simulator and server executables beside
each other when packaging.

The discovery status becomes green only after the server emits listener-ready
evidence. Use **Copy diagnostics** to capture the endpoint, IEDScout association
profile, process state, model counts, and recent protocol activity.

## Runtime interoperability test

The integration test starts the real Qt controller, imports an SCL containing
both URCB and BRCB controls, starts its child MMS server, and proves the public
wire contract through independent MMS clients. It covers complete structural
model reads, a live edited value, concurrent associations, URCB GI delivery,
BRCB event delivery with `EntryID`, and same-association Quality/Timestamp
refreshes:

```powershell
python apps/ied_simulator/test_gui_live_value.py `
  --app build-ied-simulator-qt/arstack_ied_simulator.exe `
  --read-probe build-ied-simulator-qt `
  --scl tests/fixtures/scl/minimal-station-brcb.scd
```

## Deterministic UI smoke test

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
build-ied-simulator-qt/arstack_ied_simulator.exe --smoke-test
```

For visual QA, `--scl <path> --screenshot <png>` captures the commissioning
state. Add `--runtime` to capture the running workspace.
