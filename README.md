# ARIEC61850 C++ Port

Incremental C++20 migration of the ARIEC61850 C# protocol stack. The C# implementation remains
the behavioral oracle until cross-language comparison and isolated-laboratory interoperability
are complete.

## Implemented

### Portable protocol foundation

- CMake-based C++20 static library with strict warnings.
- BER, MMS Data/UTC time, Ethernet/VLAN, classic PCAP.
- GOOSE and Sampled Values codecs plus offline supervision.
- SCL read-only parser and COMTRADE CFG/DAT readers.
- RFC 1006 TPKT, COTP, ISO Session, Presentation, ACSE, and MMS Initiate.
- Confirmed MMS GetNameList, variable attributes, Read, and Write codecs.
- DataSet directory, InformationReport, RCB state, offline monitoring, and report subscription.

### Phase 4C — built-in TCP and read-only discovery

- Non-blocking Windows Winsock and POSIX TCP transport.
- IPv4/IPv6 resolution, deadlines, cancellation, partial sends, receive chunking, and peer close.
- Bounded live domain, variable, DataSet, type, and RCB discovery.
- `ariec61850_live_discover` CLI.

### Phase 4C.1 — C# live-model parity

- `live-ied-model-v1` JSON compatible with the C# exporter.
- Logical Device / Logical Node / Data Object / Data Attribute hierarchy.
- Functional constraints, MMS/SCL types, DataSets, report controls, identity, and CDC confidence.
- Deterministic canonical manifest and model fingerprint.
- C#↔C++ parity script.
- Windows/Linux repeated reconnect and physical read-only evidence runner.

The live discovery surface sends only GetNameList, GetVariableAccessAttributes,
GetNamedVariableListAttributes, and Read. It does not construct Write, control, GI, RCB
reservation/enable, dynamic DataSet mutation, or file-service requests.

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

## Live read-only discovery

Human summary:

```powershell
.\build\Release\ariec61850_live_discover.exe 192.168.1.10 102
```

C#-compatible model:

```powershell
.\build\Release\ariec61850_live_discover.exe 192.168.1.10 102 `
  --model-json --ied-name IED_A > live-ied-model.json
```

Cross-language parity:

```bash
python scripts/compare-live-model-parity.py \
  csharp-live-ied-model.json cpp-live-ied-model.json \
  --output csharp-cpp-parity.json
```

Physical read-only acceptance:

```powershell
.\scripts\run-live-readonly-interop.ps1 `
  -HostName 192.168.1.10 -Cycles 10 `
  -OutputDirectory .\evidence\IED_A `
  -ExpectedCSharpModel .\csharp-live-ied-model.json
```

See `PHASE_4C1_LIVE_MODEL_INTEROP.md` for acceptance rules and evidence requirements.

## Other read-only tools

```powershell
.\build\Release\ariec61850_comtrade_inspect.exe .\record.cfg --json
.\scripts\run-lab-check.ps1 -Pcap .\captures\device-session.pcap
```

## Acceptance boundary

Source implementation and deterministic/simulated validation do not replace physical IED
acceptance. Before merging Phase 4C/4C.1, run the repository GCC/Clang/MSVC and security
workflows, execute the physical evidence runner against a reachable IED or vendor simulator,
and review C#↔C++ parity against the same configuration.
