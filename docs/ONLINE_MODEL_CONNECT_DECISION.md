# Online IED Model Connection Decision Map

## Why this file exists

This is the short implementation contract for choosing the correct ARStack online-model path.

Read this first when changing connection or model-discovery code. Detailed wire evidence lives in:

- [`MMS_DISCOVERY_WIRE_PROFILE.md`](MMS_DISCOVERY_WIRE_PROFILE.md) — build the model from the live MMS endpoint;
- [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md) — use a trusted CID/SCL model and perform only the online validation + initial snapshot work that is still needed.

## Decision

```text
Do we already have a trusted, selected CID/SCL model for this IED?

NO
 |
 v
LIVE DISCOVERY
  1. associate
  2. GetNameList(Domain,VMD)
  3. enumerate NamedVariables with continuation
  4. probe LN-root TypeSpecification
  5. perform selected semantic Reads
  6. discover DataSets when enabled
  7. build canonical model
  8. run shared InitialFcReadPlanner
  9. keep association online

YES
 |
 v
SCL-ASSISTED CONNECT
  1. parse/select IED + AccessPoint locally
  2. resolve endpoint + association context from SCL where available
  3. associate
  4. GetNameList(Domain,VMD)
  5. validate online domains against SCL
  6. DO NOT repeat full NamedVariable discovery by default
  7. DO NOT repeat GVAA/type discovery by default
  8. DO NOT rebuild DataSets from MMS by default
  9. run shared InitialFcReadPlanner
 10. map nested MMS Data through the canonical SCL/model type tree
 11. keep association online
```

## Shared initial snapshot contract

Both paths converge here:

```text
Canonical model
    |
    v
InitialFcReadPlanner
    - enumerate only FC roots that exist for each LN
    - deterministic ordering
    - compatibility default: <= 10 variable references per Read
    - batching limit is NOT the same setting as max outstanding services
    |
    v
InitialSnapshotRuntime
    - one confirmed Read outstanding at a time by default
    - wait response before next request
    - COTP EOT reassembly before upper-layer decode
    - map nested MMS Data against canonical model/type tree
    - preserve per-reference failures as diagnostics
    - leave association established after successful synchronization
```

## Never confuse these two workflows

### Full live discovery needs network model construction

It may use:

```text
GetNameList
GetVariableAccessAttributes
GetNamedVariableListAttributes
Read
```

because the structural model is not already trusted locally.

### SCL-assisted initial connect already has the model

The observed reference sequence for one controlled capture was:

```text
1 x GetNameList(Domain,VMD)
120 x Read
0 x GetVariableAccessAttributes
0 x GetNamedVariableListAttributes
```

Those exact counts are capture-specific. The generalized rule is what matters: **validate online identity, then read the live snapshot from the SCL-derived model instead of rediscovering the model.**

## Safety boundary

Minimum discovery/connect paths are read-only. Do not silently add:

```text
Write
control
GI
RCB reservation/enable
dynamic DataSet mutation
file-service mutation
```

Mutating or operational services require an explicit separate workflow.

## Mismatch behavior

SCL-assisted mode must not silently reinterpret a mismatched device.

```text
missing expected domain -> mark unavailable + diagnostic
extra online domain      -> report as extra evidence; do not auto-merge
missing FC root          -> mark read unavailable/failed; preserve local model
broad mismatch           -> stop or explicitly offer full live discovery fallback
```

## Implementation invariants

1. SCL is the structural source of truth in SCL-assisted mode.
2. Live MMS evidence validates and populates runtime state; it does not silently rewrite SCL identity.
3. Full live discovery is for the case where the model must be learned from the endpoint.
4. Both paths reuse the same canonical initial FC-root read planner.
5. `maxVariableReferencesPerRead` and `maxOutstandingConfirmedRequests` are separate controls.
6. Compatibility-first initial reads are sequential.
7. COTP segmentation/reassembly is mandatory before MMS decode.
8. Association addressing should come from the selected SCL engineering context when available.
9. Keep the association open after successful initial synchronization.
10. Capture-specific counts and identifiers are regression evidence, not universal IEC 61850 constants.
