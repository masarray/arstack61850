# SCL Export Reconstruction Profile

> **READ THIS BEFORE changing live IED identity resolution, live-model reconstruction, SCL type synthesis, Save/Export SCL, or Edition 2 / Edition 1 compatibility code.**

This document records the vendor-neutral protocol facts and architecture conclusions derived from controlled live MMS discovery plus multi-version SCL export observations. It is written for future human and AI development threads so the exporter is not repeatedly re-invented from partial memory.

The purpose is **not** to clone a commercial implementation. The purpose is to preserve minimum interoperability facts, reconstruct them independently, and turn them into a deterministic project-owned architecture.

Raw external-client captures and product-specific UI details are intentionally not part of the repository. Capture-derived values below are labelled as evidence and must not be hard-coded as universal IEC 61850 rules.

Related documents:

- [`../SCL_EXPORT.md`](../SCL_EXPORT.md) — 30-second entry point.
- [`MMS_DISCOVERY_WIRE_PROFILE.md`](MMS_DISCOVERY_WIRE_PROFILE.md) — live IP discovery wire behavior.
- [`ONLINE_MODEL_CONNECT_DECISION.md`](ONLINE_MODEL_CONNECT_DECISION.md) — full live discovery vs trusted-SCL online connect.
- [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md) — online connection when a trusted SCL model already exists.
- [`../LIVE_DISCOVERY_PROFILE.md`](../LIVE_DISCOVERY_PROFILE.md) — current live-model architecture.
- [`../RCB_REPORTING.md`](../RCB_REPORTING.md) — DataSet/RCB/reporting semantics relevant to live-state export.
- [`../AGENTS.md`](../AGENTS.md) — provenance and engineering discipline.

---

## 1. The mental model to remember

The observed behavior is best explained by **one live discovery and one canonical semantic model**, followed by local edition-specific export.

```text
                      LIVE MMS ENDPOINT
                              |
                              v
                     Discovery / evidence
              domains, variables, types, Reads,
                    DataSets, RCB state
                              |
                              v
                  CANONICAL LIVE IED MODEL
          identity + communication + LD/LN/DO/DA/types
                  + DataSets + RCB runtime state
                              |
                              v
                       CACHE LOCALLY
                              |
          +-------------------+-------------------+
          |                   |                   |
          v                   v                   v
   Edition 2 V3.1      Edition 1 V1.6      Edition 1 V1.5
          |                   |                   |
          |                   +-------------------+
          |                                       |
          |                                  Edition 1 V1.4
          v                                       |
   typed compatibility/profile transformation     |
          +-------------------+-------------------+
                              |
                              v
                   schema-specific serializer
                              |
                     IID / ICD output
```

**Do not implement one discovery algorithm per schema.**

**Do not make Save SCL perform hidden rediscovery merely because the user selects another output edition.**

**Do not use the output XML itself as the canonical internal model.**

---

## 2. Controlled evidence: discovery happened once, export stayed local

In the controlled session used for this analysis, meaningful MMS discovery traffic completed early in the association. The association then remained open for a much longer interval with only TCP keepalive activity while multiple SCL schema variants were saved.

No additional confirmed MMS discovery service was observed during the save interval.

Engineering conclusion:

```text
Save SCL
    !=
rediscover IED

Save SCL
    =
project cached canonical model through selected export profile
```

This is an **observed architecture clue**, not a requirement that every engineering client must keep the association open while saving.

Implementation implication for ARStack:

- discovery owns network I/O;
- model reconstruction owns semantic evidence;
- export owns local transformation/serialization;
- selecting a different schema must not mutate the live association or silently query the IED again;
- export should be possible after the network session has ended if the canonical model is complete enough.

---

## 3. Controlled discovery evidence used by the export

One controlled live-discovery run contained **419 confirmed MMS transactions** with strict sequential invoke IDs.

Observed service counts:

```text
GetNameList                         140
GetVariableAccessAttributes        119
Read                               157
GetNamedVariableListAttributes       3
--------------------------------------
confirmed transactions             419
```

The three NamedVariableList attribute reads corresponded to the three DataSets visible in that live snapshot: two previously present DataSets plus one dynamic DataSet that had been created during an earlier reporting experiment.

This matters because the resulting SCL export also contained **three DataSets** and reflected the current RCB binding to that dynamic DataSet.

Engineering conclusion:

> Live-to-SCL reconstruction is based on the **current discovered MMS model**, not only on a static manufacturer design model.

Therefore mutable DataSet/RCB state belongs to the runtime snapshot side of the canonical model. A changed runtime DataSet/RCB export does not automatically mean the structural IED model or firmware changed.

---

## 4. Canonical semantic structure was stable across all four exports

The four exports from one discovered model had the same semantic object counts:

```text
IED               1
LDevice          32
LN0              32
LN               87
DataSet           3
FCDA             71
ReportControl    32

LNodeType        38
DOType           60
DAType           17
EnumType         19
```

The stable counts strongly support this architecture:

```text
one canonical model
    -> several schema projections
```

They do **not** prove that every IED will yield the same count or that every schema pair will preserve every feature without loss. These numbers are capture-specific regression evidence only.

---

## 5. IED-name resolution: what the evidence supports

### 5.1 What was observed

The live `GetNameList(Domain, VMD)` response returned 32 MMS domains. All domains shared one stable leading naming component, and removing that component produced the Logical Device instance portion used in the reconstructed SCL.

No dedicated MMS request carrying an explicit `IEDName` field was observed in the discovery trace.

The export nevertheless produced a precise and globally consistent IED name used by both:

```text
IED@name
ConnectedAP@iedName
```

### 5.2 Architecture conclusion

The robust design is an **evidence-scored identity resolver**, not a UI worker trick.

A background worker/thread may keep the UI responsive, but it must not define the semantic identity rule.

Do not implement:

```text
iedName = longestCommonPrefix(allDomains)
```

as the entire algorithm.

That shortcut fails for ambiguous names such as:

```text
MYIEDLD1
MYIEDLD2
MYIEDLD3
```

where a raw string prefix does not prove the intended IED/LD split.

### 5.3 Recommended resolver model

```text
MMS domain directory
       |
       v
generate plausible IED/LD split candidates
       |
       v
score each candidate using independent evidence
       |
       +-- all domains decompose consistently
       +-- resulting LD instances are valid/non-empty
       +-- RptID / DatSet references agree
       +-- absolute object references agree
       +-- reconstructed LN/DO paths remain consistent
       +-- communication/model references do not contradict it
       |
       v
IedIdentityResolution
    value
    source
    confidence
    supportingEvidence
    contradictions
```

Recommended confidence/source model:

```text
Trusted SCL IED@name                 -> authoritative-for-that-file
Multi-domain naming consensus        -> high when cross-validated
Single-domain naming inference       -> low/medium unless reinforced
User explicit override               -> explicit source, never hidden inference
Conflicting evidence                 -> ambiguous / unresolved
```

A resolver result should remain typed and inspectable, for example conceptually:

```text
value              = resolved name
source             = MultiDomainNamingConsensus
confidence         = High
supportingDomains  = N
supportingRefs     = ...
contradictions     = none
```

**Never hide identity ambiguity by inventing a confident name.**

---

## 6. MMS domain -> SCL LDevice decomposition

Observed live naming followed the normal IEC 61850 MMS domain pattern in which the domain carries the concatenated IED/logical-device identity.

The exporter reconstructed:

```text
MMS domain
    -> resolved IED identity + LD instance
    -> <LDevice inst="...">
```

The important implementation rule is that decomposition must use the **resolved identity model**, not repeated local string slicing in every consumer.

Create one typed mapping:

```text
MmsDomainIdentity
    fullDomain
    iedName
    logicalDeviceInstance
    resolutionSource
    confidence
```

and reuse it in model building, reference resolution, DataSet mapping, RCB mapping, and SCL export.

---

## 7. LN / FC / DO / DA reconstruction

Live MMS names of the form:

```text
LN$FC$DO$DA...
```

provide the semantic path needed to reconstruct the SCL data model when combined with type evidence.

The intended architecture is:

```text
MMS variable name
       |
       +-- LN identity
       +-- functional constraint
       +-- DO/DA hierarchy
       |
       v
GVAA / TypeSpecification tree
       |
       v
canonical LN / DO / DA semantic tree
       |
       v
SCL LNodeType / DOType / DAType / EnumType synthesis
```

Do not couple the discovery parser directly to XML element creation.

---

## 8. Type IDs must be deterministic synthetic identities

MMS can reveal semantic structure and type information, but it cannot reconstruct all original engineering-file identifiers and metadata.

In particular, do not claim that live MMS discovery recovers the original vendor:

- `LNodeType@id` values;
- `DOType@id` values;
- `DAType@id` values;
- original descriptions;
- private/vendor extensions;
- original enum labels when they are not observable;
- Substation topology;
- original SCL authoring history.

Therefore live export should synthesize **stable deterministic type IDs** from the reconstructed semantic model.

Keep provenance explicit:

```text
semantic type tree     = recovered from live evidence
synthetic type ID      = generated by ARStack
original vendor typeID = unknown / not recoverable
```

Do not use a synthetic type ID as evidence that it existed in the source engineering file.

---

## 9. DataSet reconstruction

The correct pipeline is:

```text
NamedVariableList identity
       |
GetNamedVariableListAttributes
       |
ordered MMS members
       |
resolve each member through canonical domain/LN/FC/DO/DA model
       |
ordered FCDA[]
       |
SCL DataSet
```

**DataSet member order is semantic and must be preserved exactly.**

This is also required by reporting: when `DataReference` is absent from an InformationReport, included values are mapped through the known ordered DataSet directory.

Do not sort members alphabetically for prettier XML.

---

## 10. RCB reconstruction and live-state export

The exporter can reconstruct ReportControl semantics from discovered RCB identity plus live RCB state evidence:

```text
RP / BR identity
       +
RptID
DatSet
ConfRev
BufTm
TrgOps
OptFlds
IntgPd
buffered/unbuffered mode
       |
       v
canonical ReportControl model
       |
       v
edition-specific SCL ReportControl projection
```

The controlled evidence showed that a dynamic DataSet and its current RCB binding appeared in the later export.

Important consequence:

```text
structural IED model
    !=
runtime DataSet/RCB snapshot
```

Maintain this distinction in fingerprints, caching, diagnostics, and export provenance.

---

## 11. Edition-profile architecture

Edition support belongs in a typed export profile, conceptually:

```text
SclExportProfile
    Edition2Schema31
    Edition1Schema16
    Edition1Schema15
    Edition1Schema14
```

The profile should define at least:

- root/version metadata;
- allowed elements and attributes;
- service-capability representation;
- functional-constraint compatibility mapping;
- basic-type compatibility mapping;
- report-control capability representation;
- reference serialization rules;
- communication-address representation;
- default filename extension;
- schema validation target.

Pipeline:

```text
CanonicalSclSemanticModel
       |
       v
EditionCompatibilityTransformer
       |
       v
EditionSpecificSclModel
       |
       v
XmlSerializer
       |
       v
schema validation / diagnostics
```

Do **not** spread edition checks through discovery and runtime code.

Do **not** implement edition conversion as blind XML search/replace.

---

## 12. Controlled four-way export matrix

The observed export choices were:

```text
Edition 2  Schema V3.1 -> IID
Edition 1  Schema V1.6 -> ICD
Edition 1  Schema V1.5 -> ICD
Edition 1  Schema V1.4 -> ICD
```

Treat the extension choice as an observed engineering-tool convention for this workflow, not as a statement that file extension alone defines semantic validity.

### 12.1 Edition 1 V1.4 vs V1.5

For this specific discovered model, the full exported content was effectively identical except for the schema-version comment.

**Do not generalize this into `V1.4 == V1.5` for every model.**

A model using features that differ between those schemas may require additional transformation or rejection.

### 12.2 Edition 1 V1.5 vs V1.6

For this specific model, observed differences were minimal:

- schema-version metadata/comment changed;
- the lexical representation of `OSI-AP-Title` changed from a quoted representation in the older export to an unquoted representation in V1.6.

Again, this is capture-specific evidence, not a universal complete schema-difference list.

### 12.3 Edition 1 V1.6 vs Edition 2 V3.1

This comparison showed the meaningful typed compatibility transformation.

Observed Edition 2 additions/changes included:

```text
SCL root: version="2007" revision="B"
MMS-Port present
SGEdit/ConfSG reservation-time capability represented
ConfReportControl buffer-configuration capability represented
ReportSettings owner/reservation-time capability represented
```

Capture-specific semantic/lexical transformation counts were:

```text
32  TrgOps instances gained explicit GI representation
31  reference values changed from absolute LD-style form to relative @-style form
13  basic types changed from compatible VisString129 form to ObjRef
 7  functional constraints changed from older SG-compatible form to SE
 1  EntryID basic type changed from Octet8-compatible form to EntryID
```

These counts are useful regression evidence for the captured model only.

---

## 13. Typed compatibility examples

The internal model should carry semantic meaning, not whichever textual representation one schema happens to use.

Conceptually:

```text
CanonicalBasicType::ObjectReference
    Edition 2 -> ObjRef
    Edition 1 -> compatible VisString129 form when required by target profile

CanonicalBasicType::EntryId
    Edition 2 -> EntryID
    Edition 1 -> compatible Octet8 form when required by target profile

CanonicalFunctionalConstraint::SettingEdit
    Edition 2 -> SE
    older Edition 1 profile -> compatible SG representation where required

CanonicalTrigger::GeneralInterrogation
    Edition 2 -> explicit gi attribute where profile supports it
    older target -> omit/transform according to target schema semantics
```

The compatibility transformer must retain diagnostics whenever a target profile cannot represent a canonical semantic feature without loss.

---

## 14. Reference serialization is profile behavior

One controlled difference showed the same semantic source reference serialized differently between target editions:

```text
older Edition 1 form -> absolute logical-device style reference
Edition 2 form       -> @relative form
```

Therefore references must first be represented semantically:

```text
SclObjectReference
    IED identity
    LD identity
    LN / DO / DA path as applicable
    scope / relative-base context
```

Then the selected profile chooses the lexical representation.

Do not store only the already-rendered string and attempt to reverse-parse it later.

---

## 15. Communication-section reconstruction

Observed export reconstructed communication/association context including values such as:

- IP address;
- transport/session/presentation selectors;
- AP-title;
- AE qualifier;
- MMS port where the target profile represented it.

This aligns with the SCL-assisted-connect architecture in which communication addressing is a typed model separate from wire encoders.

Important rule:

```text
association evidence
    -> typed communication model
    -> edition-specific SCL representation
```

Do not have the XML writer inspect raw COTP/ACSE bytes directly.

---

## 16. Services/capability projection

The four-way export showed that some capability attributes exist only in the richer target profile while the underlying canonical capability remains one semantic fact.

Therefore model capabilities as typed booleans/enums/limits with provenance, for example:

```text
ReportSettingsCapabilities
    rptIdMode
    optFieldsMode
    bufTimeMode
    trgOpsMode
    intgPdMode
    ownerSupported
    resvTmsSupported
```

Then let the edition profile decide whether a field is:

```text
representable exactly
representable through compatibility mapping
not representable -> omit with diagnostic
not known -> do not invent
```

---

## 17. Provenance must be first-class

Every exported field should be traceable to one of these source classes:

```text
ObservedWire
DerivedFromObservedWire
TrustedSclInput
ProfileCompatibilityTransform
ProfileDefault
SyntheticIdentifier
UserOverride
Unknown
```

This prevents a generated file from silently presenting inference as original engineering truth.

At minimum, export diagnostics/evidence should be able to explain:

- why the IED name was chosen;
- how each MMS domain was split into IED + LD;
- which DataSets were read live and in what order;
- which RCB values were live state vs inferred/default;
- which type IDs were synthesized;
- which fields changed representation because of the selected edition;
- which canonical facts could not be represented in the target schema.

---

## 18. Cache and fingerprint rules

Because Save SCL is a local projection, cache the canonical model independently from the generated XML.

Recommended separation:

```text
StructuralModel
    LD/LN/DO/DA/type/control identities

RuntimeSnapshot
    current DataSet directory
    current RCB binding/state
    mutable online evidence

CommunicationContext
    endpoint and OSI addressing evidence

IdentityResolution
    IED-name decision + confidence
```

Do not invalidate the structural model merely because a dynamic DataSet or RCB runtime binding changed.

Do invalidate/rebuild export material when its source snapshot changes.

---

## 19. Suggested implementation components

Names are illustrative; keep existing project naming conventions when implementing.

```text
LiveIedCanonicalModel
IedIdentityResolver
MmsDomainIdentityResolver
LiveTypeTreeBuilder
LiveDataSetProjector
LiveReportControlProjector
SclSemanticModelBuilder
SclTypeIdSynthesizer
SclExportProfile
SclEditionCompatibilityTransformer
SclXmlSerializer
SclSchemaValidator
SclExportEvidence
```

Core rule:

> There must be one semantic source of truth. Discovery, online connect, reporting, and SCL export may consume it, but must not each invent a different IED/LDevice/type interpretation.

---

## 20. What a UI worker may and may not do

A worker/task/thread may:

- perform network discovery without blocking the UI;
- report progress;
- build the canonical model off the UI thread;
- serialize large XML off the UI thread;
- validate schema asynchronously.

A worker must **not** be the semantic reason that IED identity is correct.

Correctness must remain deterministic if the same evidence is processed synchronously in a unit test.

This should be testable as a pure operation:

```text
same evidence input
    -> same identity candidates
    -> same chosen result
    -> same confidence
    -> same canonical model
```

---

## 21. Regression fixtures to create independently

Do not commit raw external-client captures as permanent public fixtures. Reconstruct small project-owned synthetic cases.

Recommended tests:

### Identity resolver

```text
many domains with clean common IED identity
ambiguous common prefix
single-LD IED
similar IED and LD prefixes
contradictory RptID/DataSet evidence
explicit user override
trusted-SCL identity path
```

### DataSet projection

```text
ordered member preservation
cross-LD FCDA mapping
unknown member remains diagnostic
runtime-added DataSet appears only in runtime snapshot/export
```

### Type synthesis

```text
same semantic type tree -> stable deterministic synthetic IDs
input enumeration order change -> same canonical IDs when semantics unchanged
unknown engineering-only metadata remains absent/unknown
```

### Edition profiles

```text
ObjectReference semantic -> Ed2 ObjRef / older compatible representation
EntryId semantic -> Ed2 EntryID / older compatible representation
SettingEdit semantic -> profile-dependent FC representation
GI capability -> explicit only when target profile supports it
communication field present/omitted according to profile
unrepresentable semantic -> diagnostic, never silent corruption
```

### Save behavior

```text
one canonical model -> four exports
no network transport required during export
repeated export is deterministic
switching schema does not mutate cached canonical model
```

---

## 22. Acceptance invariants for future patches

Before merging live-to-SCL changes, verify all of these remain true:

1. Live discovery and SCL serialization are separate layers.
2. One canonical semantic model feeds every export profile.
3. Save/export does not perform hidden rediscovery by default.
4. IED-name resolution is evidence-based and exposes confidence/source.
5. A naive longest-common-prefix rule is not the sole identity algorithm.
6. Domain -> IED/LD decomposition is centralized and typed.
7. DataSet member order is preserved exactly.
8. Current live DataSet/RCB state is distinguished from structural model identity.
9. Type IDs synthesized from MMS are labelled synthetic, not original.
10. Edition conversion is typed semantic transformation, not XML string replacement.
11. Output-profile losses/omissions produce diagnostics.
12. Export remains deterministic from the same canonical snapshot.
13. Communication addressing is projected from typed evidence, not parsed from raw wire inside the serializer.
14. Engineering-only metadata not observable through MMS is not invented.
15. Capture-specific counts/quirks are not hard-coded as IEC rules.
16. Public fixtures are project-owned/synthetic and provenance-compliant.

---

## 23. Known unknowns / not yet proven

The current evidence is strong enough for the architecture above, but does not yet prove:

- the exact decision algorithm used by any external engineering client for single-LD or highly ambiguous IED names;
- the complete schema-difference set between every Edition 1 V1.4/V1.5/V1.6 feature combination;
- the behavior when the canonical model contains features impossible to represent in an older schema;
- the exact policy for conflicting live evidence vs user override;
- whether every server exposes enough live type information to synthesize a high-fidelity SCL model;
- recovery of original vendor type IDs, descriptions, private extensions, or topology from MMS alone.

Treat these as explicit future evidence targets, not invitations to guess.

---

## 24. Final architecture reminder

When working on this area, think in this order:

```text
WIRE FACTS
    -> typed evidence
    -> identity resolution
    -> canonical semantic model
    -> runtime-vs-structural separation
    -> SCL semantic reconstruction
    -> edition compatibility profile
    -> deterministic XML serialization
    -> schema/evidence diagnostics
```

Never reverse that order by starting from an example XML file and teaching discovery to imitate its strings.
