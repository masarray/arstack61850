# Built-in TCP, Live Read-Only Discovery, and Live-Model Parity Profile

## Scope

Phase 4C provides a cross-platform TCP implementation of `MmsByteTransport` and bounded live
MMS discovery. Phase 4C.1 maps that evidence into the same `live-ied-model-v1` engineering
shape exported by the C# behavioral oracle and adds repeatable physical-lab evidence tooling.

## TCP transport

`TcpMmsByteTransport` resolves IPv4/IPv6 endpoints, uses non-blocking Winsock/POSIX sockets,
handles partial sends and arbitrary receive chunks, and observes absolute deadlines plus
`std::stop_token` cancellation. It performs no operation until explicitly used by an
application runtime.

## Read-only discovery

`MmsLiveDiscoveryClient` performs only GetNameList, GetVariableAccessAttributes,
GetNamedVariableListAttributes, and Read. It does not construct Write, RCB enable/reservation,
GI, control, dynamic DataSet mutation, or file-service requests. Regression tests decode every
discovery request and reject service tag 5.

## Two distinct online model paths

For a short implementation decision map, read
[`docs/ONLINE_MODEL_CONNECT_DECISION.md`](docs/ONLINE_MODEL_CONNECT_DECISION.md) first.

ARStack must keep these workflows conceptually separate:

1. **Live discovery without a trusted SCL model** — obtain the structural model from MMS
   evidence, then acquire the initial live snapshot.
2. **SCL-assisted connect** — parse CID/SCL locally, use that model and its communication
   context, validate the reachable MMS domains, then acquire the initial live snapshot without
   redundantly rebuilding the model from MMS.

The wire evidence for the first path is documented in
[`docs/MMS_DISCOVERY_WIRE_PROFILE.md`](docs/MMS_DISCOVERY_WIRE_PROFILE.md).
The second path is documented in
[`docs/SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](docs/SCL_ASSISTED_MMS_CONNECT_PROFILE.md).

The key architectural consequence is that the final LN/FC-root initial-read planner should be
shared by both paths. Full live discovery reaches that planner after network-derived model
construction; SCL-assisted connect reaches the same planner after local SCL parsing and online
domain validation.

## Observed discovery wire behavior

A controlled loopback capture of an independent reference engineering client and MMS simulator
is documented in [`docs/MMS_DISCOVERY_WIRE_PROFILE.md`](docs/MMS_DISCOVERY_WIRE_PROFILE.md).
The capture records association parameters, monotonic invoke-ID behavior, GetNameList
pagination, LN-root type probing, selective Reads, DataSet directory discovery, and the
observed progressive interleaving of name, type, and semantic-read requests.

That document is empirical interoperability evidence, not a normative IEC 61850 requirement.
The current inventory-first discovery flow remains valid; a future optional progressive
scheduler may use the evidence to improve time-to-first-model and behavioral compatibility
without changing the canonical read-only service boundary.

## Observed SCL-assisted connect behavior

A second controlled capture established a materially different initial-connect pattern when a
CID/SCL model was already loaded locally. The observed sequence was:

```text
parse CID/SCL locally
 -> establish MMS association using SCL engineering context where available
 -> GetNameList(Domain, VMD)
 -> validate the online domain inventory
 -> skip redundant NamedVariable/GVAA/DataSet reconstruction
 -> read existing LN/FC roots in bounded batches
 -> map nested MMS Data through the local canonical type tree
 -> keep the association online
```

In that captured configuration, the initial path used 1 domain GetNameList plus 120 Read
requests, with zero GetVariableAccessAttributes and zero GetNamedVariableListAttributes
requests. The 120 final FC-root Reads matched the final snapshot phase observed after full live
discovery of the same simulated IED configuration.

This supports a reusable `InitialFcReadPlanner` architecture rather than two unrelated online
snapshot implementations. Exact counts and numeric association values remain capture-specific
and must not be generalized into universal IEC 61850 requirements.

## Live-model parity

The C++ mapper normalizes `LN$FC$DO$DA...` into LD/LN/DO/DA hierarchy, preserves FC and MMS
references, resolves direct or nested TypeSpecification evidence, infers CDC with explicit
confidence, includes DataSet and RCB evidence, and emits `live-ied-model-v1` JSON through
`ariec61850_live_discover --model-json`.

The model exposes separate fingerprints for separate questions:

- `structuralFingerprint` covers structural model identity and excludes mutable dynamic
  DataSet/RCB session state.
- `runtimeSnapshotFingerprint` covers mutable DataSet inventory plus RCB runtime evidence.
- the legacy `fingerprint` remains available during compatibility migration.

`tools/compare_live_model_json.py` is the canonical same-IED C#↔C++ comparator. Its default
mode is structural; `--types` compares common exact DA MMS type signatures, and `--runtime`
reports mutable DataSet/RCB drift as informational evidence. Runtime RCB comparison is
schema-aware: C++ `DataSetBindingStatus` is an enrichment with no C# `live-ied-model-v1`
peer, so it is validated against common C# DataSet/read evidence rather than placed in a raw
equality key. `scripts/compare-live-model-parity.py` is retained only as a compatibility
wrapper.

## Current OCR7SR12 evidence

The currently supplied same-IED captures have established a read-only milestone on one
OCR7SR12 endpoint/configuration:

- C# and C++ structural comparison: PASS, blocking=0, totalFindings=0.
- C# and C++ `--types` comparison: PASS, blocking=0, totalFindings=0 on common exact DA type
  signatures.
- canonical model inventory: 4 LD, 123 LN, 1186 DO, 6990 DA, 286 RCB, 1 current DataSet and
  1 discovered setting-group control.
- a separate C++ full RCB read-only snapshot represented all 286 RCBs with 6 Bound and 280
  Unbound at that instant, 0 NotRead, and 0 ReadFailed.

The 6/280 split is mutable runtime evidence from that capture. It must not be interpreted as
a permanent device capability or generalized to other IEDs.

## Physical interoperability evidence

`scripts/run-live-readonly-interop.py` reconnects and performs fresh discovery for each cycle,
retains per-cycle JSON, verifies stable structural fingerprints and warning policy, optionally
runs C# parity, and writes `interop-summary.json`. PowerShell and Bash wrappers are included.

Automated loopback and scripted tests do not replace physical IED evidence. Merge acceptance
still requires controlled repeated runs against a reachable IED or vendor simulator,
Windows/MSVC CI, large-model pagination when available, timeout/reconnect evidence, and a
reviewed C# parity capture against the same IED configuration. Multi-vendor physical evidence
remains required before any full-replacement or industrial interoperability claim.
