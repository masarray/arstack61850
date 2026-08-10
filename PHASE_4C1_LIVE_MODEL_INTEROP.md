# Phase 4C.1 — Live-Model Parity and Physical Read-Only Interoperability

## Scope

Phase 4C.1 converts the bounded Phase 4C discovery evidence into the same engineering model
shape used by the ARIEC61850 C# behavioral oracle and provides a repeatable, read-only physical
lab acceptance workflow.

The implementation remains read-only. It may issue only MMS GetNameList,
GetVariableAccessAttributes, GetNamedVariableListAttributes, and Read. It does not construct
Write, RCB enable/reservation, GI, control, dynamic DataSet mutation, or file-service requests.

## C# live-model parity

The C++ model uses schema `live-ied-model-v1` and preserves these C# concepts:

- IED identity and logical-device aliases;
- logical-device, logical-node, data-object, and data-attribute hierarchy;
- MMS and user object references;
- functional constraint;
- MMS type, SCL basic type, and deterministic type signature;
- DataSet members;
- buffered and unbuffered report-control inventory/state;
- model coverage and warnings.

MMS variables in the form `LN$FC$DO$DA...` are normalized into
`LD/LN.DO.DA...`. The known functional-constraint set follows the C# oracle. Logical-node names
are split into prefix, four-character LN class, and instance. Type evidence from
GetVariableAccessAttributes is resolved into the matching hierarchy. Both direct leaf evidence
and nested logical-node TypeSpecification trees are supported by the model builder; the Phase
4C discovery client remains bounded by `--max-types`.

DataSet-directory members and report-control evidence supplement incomplete GetNameList results.
IED identity is inferred conservatively from MMS domains unless `--ied-name` is supplied.
Common Data Classes are inferred with confidence and never represented as exact SCL evidence
when only heuristic evidence exists.

## Deterministic parity evidence

`ariec61850_live_discover` supports:

```text
--model-json       Full live-ied-model-v1 JSON
--manifest         Stable line-oriented parity manifest
--ied-name NAME    Explicit IED identity
--max-types N      Bound GetVariableAccessAttributes probes
```

The model contains a deterministic fingerprint. Reordering equivalent MMS discovery responses
does not change the canonical manifest or fingerprint.

Cross-language comparison:

```bash
python scripts/compare-live-model-parity.py \
  csharp/live-ied-model.json \
  cpp/live-ied-model.json \
  --output evidence/csharp-cpp-parity.json
```

The comparison reports identity, missing/unexpected attributes, functional-constraint and type
mismatches, DataSet differences, and RCB differences. Exit code 0 means no blocking finding,
1 means a parity mismatch, and 2 means invalid input.

The C# oracle already exports this schema through `LiveIedModelDiscoveryExporter`; use its
`live-ied-model.json` artifact as the expected document.

## Physical read-only interoperability runner

Windows:

```powershell
.\scripts\run-live-readonly-interop.ps1 \
  -HostName 192.168.1.10 \
  -Port 102 \
  -Cycles 10 \
  -TimeoutMs 10000 \
  -IedName IED_A \
  -OutputDirectory .\evidence\IED_A \
  -ExpectedCSharpModel .\csharp-evidence\live-ied-model.json
```

Linux:

```bash
./scripts/run-live-readonly-interop.sh \
  192.168.1.10 102 ./evidence/IED_A 10 \
  ./csharp-evidence/live-ied-model.json
```

The runner reconnects and performs a fresh discovery for each cycle. It writes:

- `cycle-NNN-live-model.json`;
- optional `cycle-NNN-csharp-cpp-parity.json`;
- `interop-summary.json`.

## Automated acceptance rules

The runner returns success only when:

1. every requested cycle associates and discovers at least one Logical Device and Data Attribute;
2. every cycle emits `live-ied-model-v1`;
3. every cycle has the same deterministic fingerprint;
4. no model warning is present unless `--allow-warnings` was explicitly selected; and
5. every C# parity comparison passes when an expected C# model is supplied.

## Required physical evidence before merge acceptance

Use an isolated engineering network and record at least:

- one IED or vendor simulator with three successful reconnect/discovery cycles;
- one ten-cycle run for the primary target vendor;
- one model with multi-page GetNameList pagination when available;
- one slow/timeout scenario and a subsequent clean reconnect;
- C# and C++ capture against the same IED configuration;
- stable fingerprints across unchanged runs;
- an explained change in fingerprint after a deliberate read-only model/configuration change.

For every target record vendor, model, firmware, IP, timestamp, C# commit, C++ commit, cycle
count, timeout, coverage, warning count, fingerprint, parity result, and packet-capture reference.

## Acceptance boundary

The source implementation, deterministic tests, parity tooling, and lab runner can be completed
without an IED attached to this development environment. Physical interoperability itself is an
evidence gate: it is accepted only after the runner is executed against a reachable IED or
vendor simulator and the generated evidence is reviewed.
