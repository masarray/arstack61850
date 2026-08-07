# C++ Port Status

## Current milestone

**Phase 4C.1 — built-in non-blocking TCP, bounded read-only discovery, C#-compatible live-model parity, and physical interoperability evidence tooling are implemented for review.**

## Delivered modules

| Area | Status |
|---|---|
| Phase 0 and Phase 1 process-bus foundation | Merged to `main` |
| SCL and COMTRADE engineering formats | Merged to `main` |
| TPKT/COTP and Session/Presentation/ACSE | Merged to `main` |
| MMS Initiate and confirmed services | Merged to `main` |
| Offline DataSet/RCB/report monitoring | Merged to `main` |
| Transport-injected MMS association/runtime | Merged to `main` |
| Persistent RCB subscription runtime | Merged to `main` |
| Built-in Windows/Linux TCP transport | Implemented in PR #14 |
| Live read-only MMS discovery | Implemented in PR #14 |
| `live-ied-model-v1` hierarchy/parity mapper | Implemented in PR #14 |
| Physical read-only evidence runner | Implemented in PR #14 |
| Physical IED acceptance evidence | Pending lab execution |

## Phase 4C.1 behavior

- Non-blocking Winsock/POSIX TCP honors deadlines and cancellation.
- Discovery is bounded and sends only GetNameList, GetVariableAccessAttributes,
  GetNamedVariableListAttributes, and Read.
- MMS references are mapped to Logical Device, Logical Node, Data Object, and Data Attribute.
- Functional constraints, MMS/SCL type evidence, DataSets, and report controls are retained.
- Direct leaf and nested TypeSpecification evidence are resolved into the model.
- IED identity and CDC are inferred conservatively with explicit confidence.
- Canonical manifests and fingerprints make unchanged-model runs comparable.
- C# and C++ `live-ied-model-v1` JSON can be compared by the parity script.
- The lab runner performs repeated reconnect/discovery cycles and records acceptance evidence.

## Validation completed without a physical IED

- Phase 4B baseline GCC, Clang, MSVC, sanitizer, CTest, and fuzz evidence remains valid.
- Phase 4C TCP loopback, cancellation, strict GCC/Clang warning, and sanitizer checks passed locally.
- Live-model hierarchy, deterministic fingerprint, parity, and multi-translation-unit header
  validation passed locally with GCC and Clang; ASan/UBSan smoke passed.
- Python parity and interoperability scripts pass syntax checks and a deterministic simulated
  three-cycle acceptance run.
- The discovery regression decodes all emitted requests and rejects MMS Write.

## Remaining acceptance gates

- Run repository GCC/Clang/MSVC and Security/Evidence workflows through `workflow_dispatch`.
- Run the physical evidence runner against a reachable IED or vendor simulator.
- Record a primary-vendor ten-cycle run, timeout/reconnect evidence, and large-model pagination
  where available.
- Export the C# oracle model and accept C#↔C++ parity against the same IED configuration.
- Review warning policy and packet-capture references before merging PR #14.

## Safety boundary

The live discovery surface is read-only and does not construct Write, control, GI, RCB
reservation/enable, dynamic DataSet mutation, or file-service requests. The generic TCP transport
can be used by other explicit runtime surfaces, but those surfaces retain their own authorization
and physical-laboratory gates.
