# Same-IED Live Model Parity

This workflow compares the ARIEC61850 C# behavioral oracle with arstack61850
against the **same IED endpoint**.

The default gate is deliberately structural. Runtime DataSet inventory and
current RCB binding are not structural blockers because devices with dynamic
DataSet/report-control allocation can legitimately expose different runtime
snapshots between sessions.

## 1. Export the C# oracle model

From the ARIEC61850 repository:

```powershell
New-Item -ItemType Directory -Force .\.artifacts\parity\csharp | Out-Null

dotnet run --project .\apps\AR.Iec61850.Cli -- `
  mms-model-discover 192.168.1.10 `
  --port 102 `
  --timeout-ms 120000 `
  --read-datasets false `
  --read-types true `
  --type-read-source model `
  --max-type-reads 256 `
  --output .\.artifacts\parity\csharp
```

`--type-read-source model` is required in this read-only parity recipe. If type
source is left at the `datasets` default while `--read-datasets false` is used,
ARIEC61850 can legitimately report `0 candidate(s)` and `exactTypes=0/0`, which
is not useful type-parity evidence.

The canonical C# model file is:

```text
.\.artifacts\parity\csharp\ied-model.json
```

ARIEC61850's exporter also writes companion DataSet, RCB, control-block and
variable-access-attribute evidence files. The structural comparator only needs
`ied-model.json`.

## 2. Export the C++ model

From the arstack61850 repository:

```powershell
New-Item -ItemType Directory -Force .\.artifacts\parity | Out-Null

.\build\Release\ariec61850_live_discover.exe `
  192.168.1.10 102 `
  --timeout-ms 120000 `
  --no-datasets `
  --no-rcb `
  --max-types 256 `
  --model-json `
  | Set-Content -Encoding utf8 .\.artifacts\parity\cpp-model.json
```

`--no-datasets` and `--no-rcb` only skip deep runtime reads. DataSet/RCB name
inventory still comes from read-only MMS discovery where available.

## 3. Compare structural parity

```powershell
python .\tools\compare_live_model_json.py `
  <path-to-ARIEC61850>\.artifacts\parity\csharp\ied-model.json `
  .\.artifacts\parity\cpp-model.json
```

Exit codes:

- `0`: structural parity passed;
- `1`: one or more blocking structural differences;
- `2`: invalid input or tool error.

The structural gate compares:

- IED identity;
- logical-device identities;
- logical-node identity/class;
- DataObject references;
- DataAttribute references + functional constraint;
- RCB identities + BRCB/URCB mode;
- GOOSE/SV/setting-group/log control-block identities.

It intentionally does **not** block on:

- current dynamic DataSet inventory;
- DataSet membership that can change at runtime;
- current RCB `DatSet` binding;
- `RptEna`, reservation, owner or reservation-time state.

## 4. Add type evidence

When both runs include successful `GetVariableAccessAttributes` evidence:

```powershell
python .\tools\compare_live_model_json.py `
  <path-to-ARIEC61850>\.artifacts\parity\csharp\ied-model.json `
  .\.artifacts\parity\cpp-model.json `
  --types
```

Exact MMS type-signature differences on attributes present on both sides are
blocking. Type-template set drift is reported as informational evidence because
one side may have intentionally omitted low-confidence templates or type reads.

## 5. Inspect runtime drift separately

```powershell
python .\tools\compare_live_model_json.py `
  <path-to-ARIEC61850>\.artifacts\parity\csharp\ied-model.json `
  .\.artifacts\parity\cpp-model.json `
  --types `
  --runtime
```

Runtime differences are informational and do not change structural pass/fail.
This is important for IEDs where an empty RCB `DatSet` is a valid unbound dynamic
slot and where named-variable-list/DataSet count can change between sessions.

## Fingerprints

arstack's live model now exposes three fingerprints during the A1 migration:

- `fingerprint`: legacy canonical fingerprint retained for compatibility;
- `structuralFingerprint`: stable device/model structure, excluding mutable
  DataSets and RCB runtime bindings;
- `runtimeSnapshotFingerprint`: mutable DataSet/RCB snapshot evidence.

A changing runtime fingerprint with an unchanged structural fingerprint is an
expected result when only dynamic report configuration changed.

## Read-only boundary

This parity workflow is Phase 4C evidence only. It may use:

- `GetNameList`;
- `GetVariableAccessAttributes`;
- `GetNamedVariableListAttributes` when explicitly enabled;
- `Read` for bounded evidence.

It does **not** authorize or perform RCB reservation, `RptEna`, GI, Write,
control, dynamic DataSet mutation, or file-service mutation.
