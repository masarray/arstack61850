# arstack61850

Portable C++20 IEC 61850 protocol stack under active migration from the
ARIEC61850 C# implementation. ARIEC61850 remains the behavioral oracle while
arstack61850 develops an independent host + embedded implementation.

The project is **not yet a complete IEC 61850 replacement stack**. Current
Public Alpha work focuses on a useful read-only MMS engineering client, process
bus codecs/runtime foundations, reproducible cross-language evidence, and an
MCU-safe protocol boundary.

## Current strengths

### Portable protocol foundation

- BER and MMS data/UTC-time codecs.
- Ethernet/VLAN process-bus framing and classic PCAP support.
- GOOSE and Sampled Values codecs plus offline/runtime supervision foundations.
- SCL read-only parsing and COMTRADE CFG/DAT support for host engineering tools.
- RFC 1006 TPKT, COTP, ISO Session, Presentation, ACSE, and MMS Initiate.
- Confirmed MMS GetNameList, GetVariableAccessAttributes, Read, and Write codecs.
- DataSet directory, InformationReport, RCB state, and report-subscription foundations.

A codec existing in the library does **not** mean the corresponding mutating
operation is enabled in live Phase 4C discovery.

### Live read-only MMS discovery

`ariec61850_live_discover` currently provides:

- built-in Windows/POSIX TCP transport;
- MMS association through the implemented OSI stack;
- domain and variable GetNameList discovery;
- named-variable-list/DataSet inventory;
- bounded variable-type probes;
- bounded RCB read evidence;
- LD/LN/DO/DA live-model projection;
- IED identity inference;
- CDC/type heuristics compatible with the C# oracle;
- GOOSE/SV/setting-group/log control-block name inventory;
- dynamic RCB `Bound` / `Unbound` / `NotRead` / `ReadFailed` evidence;
- read-only RCB candidate planning and conservative operational availability.

The live discovery surface sends only read-only services such as GetNameList,
GetVariableAccessAttributes, GetNamedVariableListAttributes and Read. It does
**not** authorize or perform Write, control, GI, RCB reservation/enable,
dynamic DataSet mutation, or file-service mutation.

## Public Alpha A1 model work

The live model now projects C#-style:

- `proposedLnTypeId` and `proposedDoTypeId`;
- `typeTemplates`;
- `variableTypeDiscoveries`;
- deterministic IED identity evidence;
- DataSet/RCB/control-block inventory.

Dynamic report configuration is separated from static model identity with three
fingerprints during migration:

- `fingerprint` — legacy canonical fingerprint retained for compatibility;
- `structuralFingerprint` — LD/LN/DO/DA + control identities, excluding mutable
  DataSet/RCB runtime state;
- `runtimeSnapshotFingerprint` — current DataSet inventory and RCB binding/state.

For IEDs with dynamic DataSets, a changed runtime fingerprint with an unchanged
structural fingerprint is expected and does not by itself mean the IED model or
firmware changed.

## Build

### Windows

```powershell
cmake -S . -B build -A x64 -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Live discovery quick start

Human summary:

```powershell
.\build\Release\ariec61850_live_discover.exe 192.168.1.10 102
```

Bounded read-only RCB evidence:

```powershell
.\build\Release\ariec61850_live_discover.exe `
  192.168.1.10 102 `
  --timeout-ms 30000 `
  --no-types `
  --no-datasets `
  --max-rcb 50 `
  --rcb-plan dynamic `
  --rcb-availability
```

Model JSON with bounded type discovery:

```powershell
New-Item -ItemType Directory -Force .artifacts\parity | Out-Null

.\build\Release\ariec61850_live_discover.exe `
  192.168.1.10 102 `
  --timeout-ms 120000 `
  --no-datasets `
  --no-rcb `
  --max-types 256 `
  --model-json `
  | Set-Content -Encoding utf8 .artifacts\parity\cpp-model.json
```

Inspect fingerprint/type evidence:

```powershell
$model = Get-Content .artifacts\parity\cpp-model.json -Raw | ConvertFrom-Json
$model.fingerprint
$model.structuralFingerprint
$model.runtimeSnapshotFingerprint
$model.typeTemplates.Count
$model.variableTypeDiscoveries.Count
```

## Same-IED C# ↔ C++ parity

The canonical comparator is:

```powershell
python .\tools\compare_live_model_json.py `
  .artifacts\parity\csharp\ied-model.json `
  .artifacts\parity\cpp-model.json
```

Optional evidence:

```powershell
python .\tools\compare_live_model_json.py `
  .artifacts\parity\csharp\ied-model.json `
  .artifacts\parity\cpp-model.json `
  --types `
  --runtime
```

Default pass/fail is structural. Runtime DataSet/RCB drift is informational.
The historical `scripts/compare-live-model-parity.py` path remains only as a
compatibility wrapper around this comparator.

See `docs/LIVE_MODEL_PARITY.md` for the complete C# export + comparison flow.

## Embedded direction

The wire core is being separated from host engineering services. Current target
roles are documented in `docs/EMBEDDED_TARGETS.md`:

- ESP32-P4-ETH: protocol/performance reference after Public Alpha;
- Waveshare ESP32-S3-POE-ETH-8DI-8DO: real I/O application reference;
- STM32H7/NXP class: later portability/industrialization target.

The embedded profile already excludes host TCP/live-discovery/PCAP/COMTRADE/SCL
parser tooling and provides a no-RTTI host-simulation gate. Sampled Values also
has a caller-owned `encode_into(span)` path so the eventual publisher does not
need to allocate a fresh Ethernet frame buffer per sample.

## Validation boundary

Implemented code and green CI do not equal full field acceptance. Current
validation includes GCC/Clang/MSVC builds, deterministic tests, sanitizer/fuzz
workflows, and selected live read-only interoperability evidence. Full
replacement claims still require same-IED C#↔C++ evidence, repeated live cycles,
packet capture review, and multi-vendor physical/simulator interoperability.

See `docs/FEATURE_MATRIX.md` for a conservative status table and
`PHASE_4C1_LIVE_MODEL_INTEROP.md` for the live-evidence boundary.
