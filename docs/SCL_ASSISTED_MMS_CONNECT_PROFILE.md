# SCL-Assisted MMS Connect Wire Behavior Profile

## Purpose

This document records empirical, vendor-neutral interoperability behavior observed during a controlled IEC 61850/MMS connection in which an engineering client first loads a CID/SCL model locally and then connects to the corresponding MMS server.

It complements [`MMS_DISCOVERY_WIRE_PROFILE.md`](MMS_DISCOVERY_WIRE_PROFILE.md), which describes model discovery when no SCL model is assumed to be available locally.

The goal is to make the two connection paths unambiguous for future ARStack implementation work:

1. **live IED discovery** — build the model from MMS evidence;
2. **SCL-assisted connect** — treat SCL as the local structural model, validate the reachable server, then obtain the initial live snapshot efficiently.

This is empirical interoperability evidence, not a normative IEC 61850 profile and not a conformance claim. Exact values observed in one capture must not be generalized to all servers.

## Public-repository boundary

The source capture came from a controlled reference engineering environment. Public documentation deliberately omits commercial product names, logos, screenshots, and proprietary terminology. Only observable protocol behavior is retained.

## Executive decision rule

When a trusted CID/SCL model is already loaded, ARStack should **not repeat full MMS model discovery by default**.

The observed efficient path is:

```text
load CID/SCL locally
        |
        v
build structural model locally
        |
        v
resolve MMS endpoint + association parameters
        |
        v
TCP / COTP / Session / Presentation / ACSE / MMS Initiate
        |
        v
GetNameList(Domain, VMD)
        |
        v
validate reachable MMS domains against SCL
        |
        v
build LN -> FC-root read plan from SCL model
        |
        v
Read FC roots in bounded batches
        |
        v
map nested MMS Data to the SCL type tree
        |
        v
update online values and keep association alive
```

Full live discovery remains the fallback when SCL is absent, incomplete, stale, or mismatched beyond the configured policy.

## Observed association profile

### TCP and COTP

The SCL-assisted connection used the same basic transport profile as the independent live-discovery capture:

| Parameter | Observed value |
|---|---:|
| TCP destination port | 102 |
| COTP source TSAP | `0x0000` |
| COTP destination TSAP | `0x0001` |
| COTP TPDU-size parameter | `0x0A` |
| Represented TPDU size | 1024 bytes |

The transport path was:

```text
TCP
 -> RFC 1006 TPKT
 -> COTP CR/CC and DT
 -> ISO Session
 -> Presentation
 -> ACSE
 -> MMS Initiate
```

### MMS Initiate limits

Observed proposed limits were:

| MMS Initiate field | Observed value |
|---|---:|
| `localDetailCalling` | 65000 |
| `proposedMaxServOutstandingCalling` | 10 |
| `proposedMaxServOutstandingCalled` | 10 |
| `proposedDataStructureNestingLevel` | 5 |

These are compatibility observations only. ARStack must continue to honor negotiated peer limits and must not hard-code them as universal requirements.

## SCL-derived association addressing

The capture showed that ACSE association addressing may differ between generic live discovery and SCL-assisted connection.

Observed example:

```text
Generic live-discovery association:
    called AP-title      = 1.1.1.999.1
    called AE-qualifier  = 12

SCL-assisted association:
    called AP-title      = 1.3.9999.23
    called AE-qualifier  = 23
```

The calling AP-title remained `1.1.1.999` in the observed run, while the calling AE qualifier also changed to the SCL-assisted value.

### Implementation consequence

When SCL is available, association addressing must be resolved from the selected IED/AccessPoint/ConnectedAP engineering context where that information is present. A generic discovery profile should be only a fallback.

ARStack must not infer that the numeric example above is universal. The important behavior is **source selection**:

```text
SCL-assisted mode:
    SCL communication / association context
        > generic compatibility defaults
```

A future regression test should verify that SCL-derived AP-title/AE-qualifier values reach the AARQ encoder unchanged.

## Confirmed-service summary

The observed SCL-assisted initial connection contained **121 confirmed MMS requests**:

| MMS service | Request count |
|---|---:|
| `GetNameList` | 1 |
| `GetVariableAccessAttributes` | 0 |
| `GetNamedVariableListAttributes` | 0 |
| `Read` | 120 |
| **Total** | **121** |

No `Write`, control, GI, RCB reservation/enable, dynamic DataSet mutation, or MMS file-transfer service was observed.

The automatic initial connect was therefore read-only.

## Step 1 — validate online MMS domain inventory

Immediately after association, the client issued one `GetNameList` request:

```text
invokeID   = 1
objectClass = Domain
objectScope = VMD-specific
```

The response contained **32 MMS domains** in the observed model and completed with:

```text
moreFollows = false
```

No per-domain `NamedVariable` enumeration followed.

### Interpretation

The client already possessed the structural IED model from SCL. The domain query therefore behaved as an online validation/sanity step rather than as the start of a complete network discovery.

Recommended ARStack interpretation:

```text
local SCL model says these Logical Devices should exist
        |
        v
online GetNameList(Domain, VMD)
        |
        v
compare expected vs observed MMS domains
        |
        +-- match ----------> continue SCL-assisted connect
        |
        +-- partial mismatch -> warn/mark unavailable objects according to policy
        |
        +-- severe mismatch -> optionally fall back to live discovery
```

Do not silently replace the SCL model with network-derived guesses after a mismatch.

## Step 2 — do not repeat type discovery when SCL already supplies it

The observed SCL-assisted connection issued:

```text
GetVariableAccessAttributes = 0
```

This is a major distinction from full live discovery.

With a valid SCL model, the client already knows the structural hierarchy and the expected type tree. It therefore avoids re-querying each Logical Node root with `GetVariableAccessAttributes` during initial connection.

### ARStack rule

In SCL-assisted mode, do not run the normal live type-probe stage by default.

Use live type discovery only when explicitly requested, when the SCL model is incomplete, or as a controlled mismatch-diagnostic path.

## Step 3 — do not repeat DataSet directory discovery during initial connect

The observed initial SCL-assisted connection issued:

```text
GetNamedVariableListAttributes = 0
```

The DataSet model is already available from the loaded SCL and therefore did not need to be rebuilt from MMS during this initial-connect sequence.

This does not prohibit later runtime validation or dynamic-DataSet inspection. It only defines the observed **initial connect** path.

## Step 4 — build an LN/FC-root live-read plan from SCL

The client read online values by grouping MMS variable references at the Functional Constraint root of each Logical Node.

Examples of the observed request shape:

```text
<LD> / LLN0$CF
<LD> / LLN0$DC
<LD> / LLN0$EX
<LD> / LLN0$RP
<LD> / LLN0$SP
<LD> / LLN0$ST
```

Other LN examples included roots such as:

```text
GAPC1$CF
GAPC1$DC
GAPC1$EX
GAPC1$ST
```

and, where structurally relevant:

```text
XCBR1$BL
XCBR1$CF
XCBR1$DC
XCBR1$EX
XCBR1$OR
XCBR1$ST
XCBR1$SV
```

Measurement Logical Nodes included roots such as:

```text
<MMXU>$CF
<MMXU>$DC
<MMXU>$EX
<MMXU>$MX
<MMXU>$ST
```

### Important consequence

The planner should **not** blindly request every possible FC for every LN.

Instead:

```text
SCL model
  -> determine which FC groups actually exist for the LN
  -> construct only those FC-root references
  -> send bounded MMS Read requests
```

This reduces request count while preserving a structurally complete initial live snapshot.

## Observed initial-read inventory

The capture represented:

```text
MMS domains          = 32
Logical Nodes        = 119
FC-root references   = 617
MMS Read requests    = 120
```

Observed FC-root occurrence counts were:

| FC | Count |
|---|---:|
| `CF` | 119 |
| `DC` | 119 |
| `ST` | 119 |
| `EX` | 107 |
| `SP` | 32 |
| `CO` | 28 |
| `OR` | 19 |
| `MX` | 18 |
| `SV` | 16 |
| `BL` | 14 |
| `SE` | 11 |
| `SG` | 11 |
| `RP` | 2 |
| `BR` | 1 |
| `SR` | 1 |

These counts describe one captured model only. They are useful evidence for planner shape, not fixed expectations for future IEDs.

## Step 5 — bound each MMS Read request

One observed Logical Node exposed eleven FC roots. The client split them into two requests:

```text
Read #1: 10 FC-root references
Read #2:  1 FC-root reference
```

Across the trace, no initial-read request exceeded ten variable references.

This provides strong behavioral evidence for a compatibility default equivalent to:

```text
maxVariableReferencesPerRead = 10
```

### Important distinction

This batching bound must not be confused with:

```text
proposedMaxServOutstandingCalling = 10
```

They are different concepts. The captured client negotiated support for up to ten outstanding services, but still used strictly sequential confirmed exchanges.

ARStack should represent these as separate configuration values.

## Step 6 — keep confirmed exchanges sequential by default

Despite negotiating:

```text
proposedMaxServOutstandingCalling = 10
```

the captured initial-connect scheduler kept only one confirmed MMS request outstanding at a time:

```text
request N
    -> wait for response N
    -> request N+1
```

Observed maximum outstanding confirmed requests during the sequence:

```text
1
```

### ARStack rule

Do not automatically pipeline initial SCL-assisted reads merely because the negotiated outstanding-service limit is greater than one.

Compatibility-first default:

```text
maxOutstandingInitialReads = 1
```

Parallel/pipelined operation may be an explicit optimization after interoperability evidence proves it safe.

## Step 7 — decode nested MMS Data against the local SCL type tree

The value reads target FC roots, not individual Data Attributes. Therefore responses can contain nested MMS structures.

Expected processing model:

```text
MMS Read response
    |
    v
AccessResult / MMS Data tree
    |
    v
known LD/LN/FC SCL type structure
    |
    v
map nested children to DO/SDO/DA/BDA identities
    |
    v
update online model values and quality/time fields
```

This is the key reason SCL-assisted connect can avoid thousands of leaf-level requests while still populating an engineering model.

Do not flatten or guess nested response ordering independently of the SCL type tree.

## Step 8 — COTP DT reassembly remains mandatory

Although client requests fit in individual COTP Data transfers in the observed trace, several server responses required segmentation.

Observed server-side response segmentation summary:

```text
117 messages -> one COTP DT segment
  3 messages -> two COTP DT segments
  2 messages -> three COTP DT segments
```

The largest observed response was approximately 2791 bytes and required three COTP DT segments with a 1024-byte negotiated TPDU profile.

Therefore MMS decode must occur only after COTP EOT reassembly:

```text
DT, EOT=0 -> append
DT, EOT=0 -> append
DT, EOT=1 -> complete user data -> decode upper layers
```

Never treat every TPKT as an independent MMS PDU.

## Step 9 — keep the association alive after the initial snapshot

After the final initial Read response, the observed connection remained established. No immediate TCP FIN/RST or application-layer release was observed.

The initial-connect operation therefore behaved as:

```text
associate
 -> validate
 -> obtain initial snapshot
 -> transition UI/runtime to ONLINE
 -> keep association for subsequent operations
```

ARStack should preserve the same lifecycle distinction between:

- **connect + initial synchronization**, and
- **disconnect/release**.

## Relationship to full live discovery

The strongest cross-capture observation is that the SCL-assisted initial snapshot matched the final FC-root read phase of the independent full-discovery trace.

Conceptually:

```text
FULL LIVE DISCOVERY
===================
associate
 -> GetNameList domains
 -> NamedVariable discovery + pagination
 -> LN-root GetVariableAccessAttributes
 -> selective semantic/metadata Reads
 -> NamedVariableList/DataSet discovery
 -> build network-derived model
 -> FC-root initial Reads
 -> ONLINE

SCL-ASSISTED CONNECT
====================
parse CID/SCL locally
 -> associate using SCL engineering context where available
 -> GetNameList domains
 -> validate against local model
 -> FC-root initial Reads
 -> ONLINE
```

The 120 FC-root Read requests observed at the end of the full-discovery capture matched the 120 SCL-assisted initial Reads in identifier order for the same simulated IED configuration.

This observation should guide implementation architecture: the **initial live snapshot planner should be reusable by both modes**.

## Recommended ARStack architecture

Avoid two unrelated code paths for online value synchronization.

Preferred decomposition:

```text
                    +--------------------------+
                    | Canonical IED model      |
                    +--------------------------+
                       ^                    ^
                       |                    |
              from SCL parser         from live discovery
                       |                    |
         +-------------+                    +-------------+
         |                                                |
         v                                                v
SCL-assisted validator                         network discovery engine
         |                                                |
         +-------------------+----------------------------+
                             |
                             v
                  InitialFcReadPlanner
                             |
                  LN -> available FC roots
                             |
                  <= 10 references / Read
                             |
                  sequential confirmed exchange
                             |
                             v
                  SCL/model-aware Data mapper
                             |
                             v
                           ONLINE
```

### Suggested component responsibilities

`SclAssistedConnectPlanner`

- select IED/AccessPoint from parsed SCL;
- resolve endpoint and association addressing;
- compare online MMS domains with expected LD/domain identities;
- choose mismatch policy;
- feed the canonical model into the initial-read planner.

`InitialFcReadPlanner`

- accept the canonical model regardless of whether it came from SCL or live discovery;
- enumerate existing FC roots per LN;
- preserve deterministic ordering;
- batch no more than the configured maximum references per Read;
- default compatibility profile: 10 references per Read;
- remain independent from the association outstanding-request limit.

`InitialSnapshotRuntime`

- execute Reads sequentially by default;
- reassemble segmented COTP payloads before upper-layer decode;
- map nested MMS Data through the canonical model/type tree;
- surface per-reference access errors without corrupting unrelated model values;
- keep the association active after synchronization.

## Explicit non-goals of SCL-assisted initial connect

Unless separately requested by a feature or validation mode, initial SCL-assisted connect should not automatically perform:

```text
full NamedVariable discovery
full GetVariableAccessAttributes probing
full NamedVariableList/DataSet reconstruction
Write
control
GI
RCB reservation or enable
runtime DataSet mutation
MMS file transfer
```

Those operations belong to explicit workflows, not to the minimum initial-connect path.

## Mismatch policy

SCL is an engineering source of truth, but an online device may not match the file exactly. ARStack should make mismatches explicit rather than silently changing semantic identity.

Recommended classes:

### Domain missing online

- keep the SCL model identity;
- mark the LD/domain unavailable;
- emit a diagnostic;
- do not issue child Reads for that domain.

### Extra domain online

- report it as additional online evidence;
- do not silently merge unknown structure into the SCL model;
- offer explicit live-discovery enrichment when requested.

### FC root missing or access denied

- preserve the local type/model definition;
- mark the live value group unavailable/read-failed;
- continue independent FC reads according to policy.

### Broad structural mismatch

- stop SCL-assisted synchronization or offer an explicit fallback to full live discovery;
- never silently reinterpret nested MMS response order using an incompatible local type tree.

## Implementation invariants

Future work should preserve these invariants:

1. **SCL structural knowledge prevents redundant live type discovery by default.**
2. **Online domain inventory validates reachability/model identity before bulk Reads.**
3. **Association addressing is sourced from the selected SCL engineering context where available.**
4. **Initial live reads target existing LN/FC roots, not every DA leaf.**
5. **Read batching bound and outstanding-service limit are separate settings.**
6. **Compatibility default is at most 10 variable references per initial Read.**
7. **Initial confirmed Reads are sequential by default.**
8. **COTP DT segmentation must be reassembled before MMS decode.**
9. **Nested Read responses are mapped through the canonical model/type tree.**
10. **The association remains open after successful initial synchronization.**
11. **No mutating service is part of the minimum SCL-assisted initial-connect sequence.**
12. **The same initial FC-read planner should be reusable after full live discovery.**

## Evidence-to-implementation rule

The exact observed counts, identifiers, and timing values belong to one captured configuration. Preserve them as regression evidence, but implement the generalized behavior:

```text
wire observation
    -> explicit compatibility hypothesis
    -> configurable implementation
    -> regression test
    -> multi-device interoperability evidence
```

Do not turn one capture into an undocumented universal assumption.
