# MMS IED Discovery Wire Behavior Profile

## Scope

This document records empirical wire behavior observed during a controlled loopback MMS/IEC 61850 model-discovery session between a reference engineering client and a reference MMS simulator.

The purpose is interoperability engineering: preserve observable request ordering, association parameters, pagination behavior, and read-only service usage that may matter when communicating with real IEDs.

This is **not** a normative IEC 61850 profile and is **not** a conformance claim. Values below are evidence from one controlled capture and must not be generalized as requirements for every IED or engineering tool.

The public repository intentionally keeps this evidence vendor-neutral. Commercial product names, logos, screenshots, and proprietary marketing terminology are not part of this profile.

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
    -> moreFollows = true/false
```

From the observed 105 NamedVariable requests over 32 domains, **73 requests were continuation pages** beyond the first request for each domain.

Important interoperability properties:

- continuation is scoped to the same object class and object scope;
- the continuation token is the last identifier returned by the previous page;
- discovery continues until `moreFollows = false`;
- a client must guard against `moreFollows = true` without forward progress;
- response fragmentation at lower OSI/TCP layers must not change MMS pagination semantics.

ARStack already implements the essential bounded continuation rule in `MmsLiveDiscoveryClient::get_name_list()`: when `more_follows` is true, the next request uses the last returned name as `continue_after`.

## Progressive semantic discovery

A key observation is that discovery was **not** a simple set of fully separated phases such as:

```text
all GetNameList
then all GetVariableAccessAttributes
then all Read
```

Instead, requests were interleaved. Representative behavior was:

```text
GetNameList pages
GetVariableAccessAttributes
GetNameList pages
GetVariableAccessAttributes
Read
GetNameList
Read
GetVariableAccessAttributes
...
```

This suggests a progressive semantic-discovery strategy:

```text
discover names
    -> recognize model structure / Logical Node candidates
    -> probe type information
    -> read selected semantic attributes
    -> continue with the next model region
```

For ARStack this is an interoperability/performance observation, not a standards requirement. The current staged discovery implementation remains valid, but an optional progressive orchestration profile may reduce time-to-first-model and may better match behavior seen in mature engineering clients.

## Type discovery behavior

`GetVariableAccessAttributes` was observed **119 times**.

The reference client primarily used type probes at Logical Node roots rather than blindly probing every leaf Data Attribute. This allows a nested MMS `TypeSpecification` to describe a larger subtree with fewer requests.

This is aligned with the existing ARStack logical-node probe planner, which intentionally selects one type-tree probe per Logical Node root where possible.

Implementation implications:

- retain recursive and bounded `TypeSpecification` decoding;
- prefer LN-root type probes where the server exposes a useful nested type tree;
- preserve fallback behavior for servers that require more granular probes;
- never assume one vendor's type-tree shape is universal.

## Read behavior

`Read` was observed **157 times** during automatic model discovery.

The Reads were selective rather than a brute-force read of every discovered leaf. Observed patterns included:

- selected semantic metadata below `LLN0` and other Logical Nodes;
- grouped Functional Constraint-oriented reads;
- model/identity metadata needed to enrich the discovered engineering hierarchy.

This indicates that model discovery is formed from multiple evidence sources:

```text
MMS object names
    + TypeSpecification evidence
    + selected Read results
    + DataSet directory evidence
```

ARStack should continue to distinguish structural evidence from mutable runtime values so that a temporary runtime change does not alter structural model identity.

## DataSet discovery

Named-variable-list discovery was performed with `GetNameList` using object class `NamedVariableList`. The trace contained **34 NamedVariableList GetNameList requests**, including scope-specific inventory work and continuation where needed.

After candidate DataSets were identified, the client issued **2 `GetNamedVariableListAttributes` requests** to retrieve the member composition of discovered DataSets.

The observed pattern is therefore:

```text
GetNameList(NamedVariableList)
    -> discover DataSet names
    -> GetNamedVariableListAttributes(DataSet)
    -> retrieve member references
```

This is consistent with the current ARStack separation between DataSet inventory and DataSet directory/member retrieval.

## Behavioral fingerprint

The following compact profile can be used as a regression reference for future capture comparison:

```text
MMS IED DISCOVERY WIRE PROFILE v1

Transport
---------
TCP destination         102
COTP source TSAP        0x0000
COTP destination TSAP   0x0001
COTP TPDU size          1024 bytes

MMS Initiate
------------
localDetailCalling                 65000
maxOutstandingCalling                10
maxOutstandingCalled                 10
nestingLevel                           5

Confirmed requests
------------------
invokeID range                         1..418
invokeID behavior                      monotonic +1
GetNameList                            140
GetVariableAccessAttributes            119
Read                                   157
GetNamedVariableListAttributes           2
Total                                  418

Initial model inventory
-----------------------
GetNameList(Domain, VMD-specific)
observed domains                         32
initial domain response moreFollows      false

NamedVariable enumeration
-------------------------
per-domain first-page requests            32
continuation pages                         73
total NamedVariable requests              105
continueAfter = last identifier from prior page

Mutation during automatic discovery
-----------------------------------
Write                                     0
Control                                   0
RCB enable/reservation                    0
GI                                        0
Dynamic DataSet mutation                  0
```

## Current ARStack comparison

The current `MmsLiveDiscoveryClient::discover()` is primarily **inventory-first staged discovery**:

```text
1. discover domains
2. enumerate NamedVariable and NamedVariableList names per domain
3. build report/DataSet inventory
4. probe selected variable types
5. read DataSet directories
6. optionally read selected RCB state
```

The observed reference trace behaves more like **progressive semantic discovery**, where type probes and selected Reads are interleaved with name discovery.

This is the main orchestration difference identified by this capture. It does **not** imply that the current implementation is incorrect. A future compatibility/performance profile can add progressive scheduling while preserving the same bounded, read-only services and the same normalized output model.

## Recommended implementation direction

Keep one standards-facing discovery engine with explicit scheduling policies rather than embedding a commercial-client imitation into protocol code.

Suggested architecture:

```text
MMS codecs / association runtime
            |
            v
bounded discovery primitives
  - GetNameList + continuation
  - GetVariableAccessAttributes
  - GetNamedVariableListAttributes
  - Read
            |
            v
discovery scheduler policy
  - staged inventory policy
  - progressive semantic policy
            |
            v
canonical live-ied-model-v1 mapper
```

A progressive policy should remain read-only and should be evaluated using measurable outcomes such as:

- time to first usable LD/LN hierarchy;
- total confirmed-request count;
- redundant request count;
- total discovery duration;
- correctness of the final structural fingerprint;
- stability across reconnects;
- behavior with multi-page `GetNameList` responses;
- behavior under fragmented/coalesced TCP delivery;
- interoperability across multiple independent IEDs/simulators.

## Regression invariants

Future discovery changes should preserve these invariants:

1. no Write/control/report-enable/DataSet-mutation request is generated by read-only discovery;
2. every confirmed request is correlated by invoke ID;
3. negotiated PDU/outstanding/nesting limits are respected;
4. `GetNameList` continuation is bounded and must make forward progress;
5. duplicate identifiers do not create duplicate model objects;
6. lower-layer fragmentation/coalescing does not affect MMS transaction semantics;
7. a discovery failure in an optional enrichment probe does not corrupt already established structural evidence;
8. structural and mutable runtime fingerprints remain separate;
9. capture-derived behavior remains empirical evidence, not silently promoted to an IEC conformance requirement.

## Evidence boundary

This wire profile is one loopback interoperability observation. It should be strengthened with additional controlled captures covering:

- association only, without model discovery;
- model discovery against additional independent simulators/IEDs;
- large models with repeated pagination;
- lazy/manual tree expansion after automatic discovery;
- one-value polling behavior;
- timeout during discovery and clean reconnect;
- malformed/partial responses in a controlled negative-test server;
- comparison of staged versus progressive ARStack scheduling against the same endpoint.

Physical multi-vendor interoperability evidence remains necessary before making industrial replacement or conformance claims.
