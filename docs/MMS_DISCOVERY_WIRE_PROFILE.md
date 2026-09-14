# MMS IED Discovery Wire Behavior Profile

## Scope

This document records empirical wire behavior observed during a controlled loopback MMS/IEC 61850 model-discovery session between a reference engineering client and a reference MMS simulator.

The purpose is interoperability engineering: preserve observable request ordering, association parameters, pagination behavior, and read-only service usage that may matter when communicating with real IEDs.

This is **not** a normative IEC 61850 profile and is **not** a conformance claim. Values below are evidence from one controlled capture and must not be generalized as requirements for every IED or engineering tool.

The public repository intentionally keeps this evidence vendor-neutral. Commercial product names, logos, screenshots, and proprietary marketing terminology are not part of this profile.

## Relationship to SCL-assisted connect

This file describes the path used when the client must obtain the IED model from the live MMS endpoint.

A separate capture showed that when a trusted CID/SCL model is already loaded, the client can take a shorter path: use the SCL structure and communication context locally, validate the online MMS domains, then execute the same style of LN/FC-root initial snapshot Reads without repeating NamedVariable enumeration, type discovery, or DataSet reconstruction.

That second path is documented in [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md).

The two profiles should be read together. They define two entry paths into one canonical online model/snapshot architecture:

```text
NO TRUSTED SCL
    live MMS discovery
        -> canonical model
        -> shared FC-root initial snapshot

TRUSTED SCL AVAILABLE
    local SCL parse + online validation
        -> canonical model
        -> shared FC-root initial snapshot
```

## Test boundary

Observed workflow:

1. start a reference MMS server/simulator;
2. establish a fresh client association to TCP port 102;
3. request discovery of the IED model from the network endpoint;
4. allow automatic discovery to complete without manual model browsing;
5. observe the complete request/response sequence on the loopback capture path.

The observed automatic discovery phase was read-only. No MMS Write, control operation, report enable/reservation, GI, or dynamic DataSet mutation was observed.

## Association evidence

### Transport and COTP

Observed client-side association parameters:

| Parameter | Observed value |
|---|---:|
| TCP destination port | 102 |
| COTP source TSAP | `0x0000` |
| COTP destination TSAP | `0x0001` |
| COTP TPDU size parameter | `0x0A` |
| Negotiated TPDU size represented by that parameter | 1024 bytes |

The connection follows the expected RFC 1006 / TPKT / COTP path, followed by ISO Session, Presentation, ACSE, and MMS Initiate negotiation.

### MMS Initiate

Observed proposed association limits:

| MMS Initiate field | Observed value |
|---|---:|
| `localDetailCalling` | 65000 |
| `proposedMaxServOutstandingCalling` | 10 |
| `proposedMaxServOutstandingCalled` | 10 |
| `proposedDataStructureNestingLevel` | 5 |

The reference server accepted matching limits in this capture.

These values are compatibility evidence only. ARStack must continue to support negotiated peer limits rather than assume that all servers use this exact profile.

## Confirmed-service summary

The automatic discovery session contained **418 confirmed MMS requests** and **418 matching confirmed responses**.

Observed invoke IDs were monotonic and contiguous:

```text
1, 2, 3, ... 418
```

Observed service counts:

| MMS service | Request count |
|---|---:|
| `GetNameList` | 140 |
| `GetVariableAccessAttributes` | 119 |
| `Read` | 157 |
| `GetNamedVariableListAttributes` | 2 |
| **Total** | **418** |

No mutating MMS service was observed in this automatic discovery trace.

## Discovery sequence

### 1. Domain inventory

The first confirmed discovery request was a `GetNameList` for MMS domains:

```text
objectClass = Domain
objectScope = VMD-specific
```

The response contained **32 domains** in the observed simulator model and completed without continuation:

```text
moreFollows = false
```

This establishes the initial Logical Device / MMS-domain inventory before deeper model discovery.

### 2. Per-domain named-variable enumeration

After domain inventory, the client issued `GetNameList` with:

```text
objectClass = NamedVariable
objectScope = Domain-specific
```

for each discovered domain.

A total of **105 NamedVariable GetNameList requests** were observed across the 32 domains. The request count is larger than the domain count because large domains were retrieved over multiple pages.

### 3. Pagination and `continueAfter`

Pagination behavior was consistent with the MMS `GetNameList` continuation model:

```text
GetNameList(domain X)
    -> listOfIdentifier = [...]
    -> moreFollows = true

GetNameList(domain X, continueAfter = last returned identifier)
    -> next listOfIdentifier = [...]
```

Approximately 73 continuation requests were observed across the NamedVariable enumeration. Large domains required multiple pages; one observed domain required 17 pages.

The generalized compatibility behavior is therefore:

1. preserve the identifiers in server-returned order;
2. when `moreFollows` is true, set `continueAfter` to the last identifier from the current page;
3. continue until `moreFollows` is false;
4. enforce page and object-count bounds;
5. reject `moreFollows=true` without forward progress.

This matches the safety direction already implemented in `MmsLiveDiscoveryClient`.

### 4. Type discovery is progressive and interleaved

The capture did not show a simple phase ordering of:

```text
all GetNameList
then all GetVariableAccessAttributes
then all Read
```

Instead, name enumeration, type probing, and selected Reads were interleaved as model regions became available.

Representative pattern:

```text
GetNameList ...
GetNameList ...
GetVariableAccessAttributes ...
GetNameList ...
GetVariableAccessAttributes ...
Read ...
GetNameList ...
...
```

This is best described as **progressive semantic discovery**.

A future ARStack progressive scheduler may use this behavior to reduce time-to-first-model, but the observation does not invalidate the current inventory-first implementation. Both approaches can be interoperable if the same service semantics, bounds, and final model are preserved.

### 5. GetVariableAccessAttributes targets Logical Node roots

A total of **119 `GetVariableAccessAttributes` requests** were observed.

The useful behavioral pattern is that probes target Logical Node roots such as:

```text
<domain> / LLN0
<domain> / <other-LN>
```

rather than issuing a separate type request for every `LN$FC$DO$DA...` leaf.

This allows one nested MMS `TypeSpecification` to describe the FC/DO/DA hierarchy below a Logical Node and is consistent with the current ARStack logical-node type-probe planner.

### 6. Selective semantic Reads occur during discovery

The trace contained Reads before the final online snapshot stage. These Reads included selected metadata and functional-constraint structures used to enrich the discovered model.

Examples included references under patterns such as:

```text
LLN0$EX$NamPlt$ldNs
```

and grouped FC-root references for selected Logical Nodes.

The important design point is that discovery is not based solely on names. Model construction can combine:

```text
GetNameList evidence
    +
TypeSpecification evidence
    +
selected Read evidence
```

ARStack should keep those evidence classes distinct so heuristics never become unmarked standards claims.

### 7. DataSet discovery

The discovery trace used `GetNameList` for `NamedVariableList` identities and then issued two observed `GetNamedVariableListAttributes` requests for discovered DataSets.

The generalized flow is:

```text
GetNameList(NamedVariableList)
        |
        v
discover DataSet identities
        |
        v
GetNamedVariableListAttributes
        |
        v
retrieve DataSet member composition
```

The number of DataSets is capture-specific.

## Final initial-live-snapshot phase

A particularly important cross-capture result is that the **last 120 Read requests** in this full live-discovery session matched the **120 initial snapshot Read requests** observed in the SCL-assisted connection for the same simulated IED configuration.

The identifier order matched exactly in the compared traces.

This establishes a strong architectural hypothesis:

```text
full live discovery
    -> build canonical model from MMS
    -> shared LN/FC-root initial snapshot

SCL-assisted connect
    -> build canonical model from SCL
    -> validate online domains
    -> shared LN/FC-root initial snapshot
```

Implementation should therefore prefer a reusable initial FC-read planner instead of duplicating snapshot logic inside the discovery engine and the SCL connection path.

Details of the FC-root batching and SCL mapping behavior are maintained in [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md).

## Behavioral fingerprint from this capture

```text
Transport
---------
TCP destination        102
COTP source TSAP       0x0000
COTP destination TSAP  0x0001
COTP TPDU size         1024 bytes

MMS Initiate
------------
localDetailCalling                 65000
maxOutstandingCalling                10
maxOutstandingCalled                 10
nestingLevel                           5

Confirmed services
------------------
GetNameList                       140
GetVariableAccessAttributes       119
Read                              157
GetNamedVariableListAttributes      2
Total                             418

Invoke behavior
---------------
start invokeID                      1
end invokeID                      418
observed progression        contiguous +1

Discovery behavior
------------------
initial Domain/VMD GetNameList
per-domain NamedVariable enumeration
continueAfter pagination
LN-root TypeSpecification probes
selected semantic Reads
NamedVariableList/DataSet directory discovery
final shared FC-root live snapshot

Mutation during automatic discovery
-----------------------------------
Write                              0
Control                            0
GI                                 0
RCB enable/reservation             0
```

## Comparison with current ARStack discovery orchestration

Current `MmsLiveDiscoveryClient::discover()` is primarily inventory-first:

```text
domain inventory
 -> per-domain NamedVariable inventory
 -> per-domain NamedVariableList inventory
 -> report/DataSet inventory
 -> LN-root type probes
 -> optional DataSet directories
 -> optional RCB reads
```

The reference capture shows a more interleaved scheduler.

The distinction should be explicit:

```text
Current ARStack:
    inventory-first staged discovery

Observed reference behavior:
    progressive semantic discovery
```

This is an orchestration difference, not evidence that the current codecs are wrong.

Any implementation change should preserve existing tested behavior and introduce progressive scheduling as an explicit, reviewable capability rather than rewriting the working discovery path casually.

## Safety and implementation rules

1. Discovery remains read-only unless the caller explicitly selects a different workflow.
2. Respect negotiated association and PDU limits.
3. Bound domain count, pages, names, type depth, response sizes, and total work.
4. Support arbitrary TCP chunking and COTP segmentation/reassembly.
5. Preserve invoke-ID correlation and tolerate transport fragmentation/coalescing.
6. Treat `moreFollows` without forward progress as an error.
7. Do not infer a permanent IED capability from one runtime snapshot.
8. Keep wire facts, configured expectations, heuristics, and interoperability claims separate.
9. Reuse the canonical initial FC-root snapshot planner across live-discovery and SCL-assisted modes.
10. Do not turn capture-specific object counts or identifiers into hard-coded model assumptions.

## Recommended regression targets

Future tests should cover:

- byte/semantic association-profile vectors;
- large multi-page `GetNameList` responses;
- `continueAfter` correctness;
- duplicate/no-progress continuation rejection;
- LN-root nested `TypeSpecification` mapping;
- interleaved confirmed-request sequencing;
- segmented COTP responses;
- reconnect after timeout/failure;
- structural equivalence between staged and progressive discovery schedulers;
- equivalence of the final FC-root snapshot planner after live discovery vs SCL-assisted connect.

## Evidence status

The profile is based on one controlled loopback capture against one simulator configuration. It is high-value behavioral evidence but insufficient for a universal compatibility claim.

Before making progressive discovery or SCL-assisted snapshot behavior a strict default across all targets, repeat the evidence against multiple physical IEDs and/or independent simulators and preserve per-device differences as explicit compatibility profiles when necessary.
