# SCL Open / Import Normalization Profile

> **READ THIS BEFORE changing Open SCL, SCL parsing, canonical-model construction, edition conversion, Save SCL, SCL-assisted connect, or any code that tries to preserve/normalize source XML.**

This document extends the live-MMS-to-SCL reconstruction contract with a second ingestion path: **Open an existing SCL file, normalize it into the same generic semantic model, then export that model through the same edition profiles used by live discovery.**

The goal is not byte-for-byte cloning of any external tool. The goal is a deterministic, vendor-neutral, interoperable architecture in which both live discovery and SCL import converge on one semantic source of truth.

Related documents:

- [`../SCL_EXPORT.md`](../SCL_EXPORT.md) — 30-second import/export contract.
- [`SCL_EXPORT_RECONSTRUCTION_PROFILE.md`](SCL_EXPORT_RECONSTRUCTION_PROFILE.md) — live MMS -> canonical model -> multi-edition export.
- [`ONLINE_MODEL_CONNECT_DECISION.md`](ONLINE_MODEL_CONNECT_DECISION.md) — live discovery vs trusted-SCL online connect.
- [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md) — connect using a trusted SCL model.
- [`../RCB_REPORTING.md`](../RCB_REPORTING.md) — DataSet/RCB semantics and runtime state.
- [`../AGENTS.md`](../AGENTS.md) — engineering/provenance rules.

---

## 1. New interoperability observation

A controlled engineering-client workflow was observed to support both:

```text
A. Discover live IED -> Save SCL -> choose target edition
B. Open existing SCL -> Save SCL -> choose the same target editions
```

The SCL produced from path B was reported to be semantically the same or very similar in structure/style to the SCL produced from path A.

This is strong architecture evidence for **normalization through a generic internal model**, but it is not yet proof that every input field, private extension, lexical form, or engineering-only detail is preserved identically.

Engineering conclusion:

```text
LIVE DISCOVERY -----------+
                           |
                           v
                    CANONICAL MODEL
                           ^
                           |
OPEN SCL -> parse/normalize+
                           |
                           v
                 SAME EXPORT PROFILES
                           |
              +------------+------------+
              |            |            |
            Ed2          Ed1.6        Ed1.5/1.4
```

**Do not build separate output engines for discovered models and opened SCL files.**

---

## 2. The architecture to implement

There should be two primary model-ingress adapters and one canonical semantic core:

```text
                     INPUT PATH A
                   LIVE MMS ENDPOINT
                         |
                  discovery/evidence
                         |
                         v
                  LiveEvidenceAdapter
                         |
                         |
                         v
+---------------------------------------------------+
|             CANONICAL IEC 61850 MODEL             |
|                                                   |
| identity                                          |
| communication                                     |
| LD / LN / DO / DA semantic tree                   |
| canonical basic types / FC / CDC semantics        |
| DataSets                                          |
| report/control capability semantics               |
| declared configuration                            |
| optional runtime snapshot                         |
| provenance / confidence / diagnostics             |
+---------------------------------------------------+
                         ^
                         |
                         |
                  SclImportNormalizer
                         ^
                         |
                    SCL XML parser
                         |
                   EXISTING SCL FILE
                     INPUT PATH B

                         |
                         v
                  SCL EXPORT PROFILES
                         |
        +----------------+----------------+
        v                v                v
    Ed2 V3.1         Ed1 V1.6        Ed1 V1.5/V1.4
```

The canonical model is the product boundary. XML trees and MMS wire trees are **inputs/evidence**, not the long-term source of truth.

---

## 3. Open SCL must be semantic import, not XML retention

Do not implement Open SCL as:

```text
load XML DOM
    -> keep DOM as application model
    -> edit a few strings
    -> save DOM
```

That architecture makes edition conversion fragile and creates a second source of truth beside live discovery.

Instead:

```text
XML
 -> detect source edition/profile
 -> parse typed SCL semantics
 -> resolve references
 -> normalize edition-specific representations
 -> build canonical semantic model
 -> retain provenance/source-only extras separately
```

The exporter then consumes the same canonical model regardless of whether it came from live MMS or an opened file.

---

## 4. Source-edition adapters are inverse compatibility adapters

The export architecture already needs typed target profiles. Import requires the corresponding semantic decode path.

Conceptually:

```text
SclImportProfile
    Edition2Schema31
    Edition1Schema16
    Edition1Schema15
    Edition1Schema14
```

Each import profile is responsible for decoding source representation into canonical semantics, for example:

```text
Ed2 ObjRef
Ed1 compatible VisString form
        -> Canonical ObjectReference

Ed2 EntryID
Ed1 compatible Octet8 form
        -> Canonical EntryId

Ed2 SE
older Edition 1 compatible SG representation
        -> Canonical SettingEdit semantic

explicit GI attribute
or older profile where GI cannot be represented the same way
        -> canonical trigger state with source/provenance
```

Never normalize by blind global text replacement.

---

## 5. Unknown is not false

This is a critical downgrade/upgrade rule.

When an older source edition cannot represent a semantic field that exists in a newer profile, reopening that old file must **not invent** the missing value.

Use explicit state such as:

```text
KnownTrue
KnownFalse
Unknown
NotRepresentableInSourceProfile
DefaultedByStandardProfile   // only when independently justified
```

Example principle:

```text
newer semantic field existed before downgrade
    -> older file omitted it
    -> re-open older file
    -> value may now be unknown/not-representable
    -> do NOT silently restore the old value from memory
```

Cross-edition export can therefore be intentionally lossy. Loss must be visible in diagnostics.

---

## 6. Generic/interoperable output versus source-preserving output

The observed workflow strongly suggests a **normalized interoperable output** mode: opened files are re-emitted through the same generic style used by discovered models.

That should be the primary architecture for ARStack.

Define the distinction clearly:

```text
NormalizedInteroperable export
    semantic model is authoritative
    edition profile chooses representation
    deterministic generic ordering/IDs/style
    source lexical quirks are not authoritative

SourcePreserving export (optional future mode)
    attempts to retain original lexical/private details
    must never become the canonical model
```

Do not promise byte-for-byte round trip unless a dedicated source-preserving mode is implemented and tested.

---

## 7. Preserve source evidence without polluting the canonical core

Opening an SCL may reveal information that live MMS cannot reconstruct, for example:

- original type IDs;
- descriptions;
- private/vendor XML extensions;
- Substation topology;
- original Header/history metadata;
- tool-specific metadata;
- source lexical ordering/formatting.

Do not throw this information away silently, but do not let it define canonical semantics either.

Keep a side envelope conceptually like:

```text
SclSourceEvidence
    sourceEdition
    sourceFileIdentity
    originalTypeIdAliases
    descriptions
    headerMetadata
    topologyMetadata
    opaqueExtensions
    unknownElements
    sourceDiagnostics
```

The normalized exporter may omit unsupported source-only material, but must be able to report what was not represented.

---

## 8. Type identity rule for opened SCL

For live discovery, type IDs may need deterministic synthesis because original SCL IDs are not observable.

For Open SCL, original type IDs **are** observable. Preserve them as source aliases/provenance, but do not make semantic equivalence depend on their spelling.

Recommended model:

```text
CanonicalTypeIdentity
    semanticFingerprint
    canonicalStableId
    sourceAliases[]
        { sourceFile, originalTypeId }
```

This allows:

```text
live discovery -> deterministic synthetic canonical ID
open SCL       -> preserve original ID as alias
normalized save-> same canonical naming policy if desired
```

Two types with different source IDs but identical canonical semantics may therefore normalize to the same semantic type identity.

---

## 9. IED identity rules differ by ingress path

### Live discovery

Use the evidence-scored resolver described in `SCL_EXPORT_RECONSTRUCTION_PROFILE.md`.

### Open SCL

`IED@name` is authoritative **for that input file**.

Record:

```text
value      = IED@name
source     = TrustedSclInput / ParsedSclInput
confidence = authoritative-for-file
```

Do not re-infer a different IED name from local strings merely to make the output look like a discovered model.

### Open SCL + live connect

When the imported model is used for an online connection:

```text
SCL identity
    +
live domain inventory / endpoint evidence
    -> validation/reconciliation
```

A mismatch must be explicit. Do not silently rename the imported IED or mutate the file-derived model to make the live endpoint fit.

---

## 10. Open SCL + Connect is a third workflow, not a third model

The product has three workflows but only one canonical model architecture:

```text
1. DISCOVER LIVE
   wire evidence -> canonical structural model + runtime snapshot

2. OPEN SCL OFFLINE
   parsed SCL -> canonical structural/configuration model

3. OPEN SCL + CONNECT
   parsed SCL -> canonical structural model
              + live validation
              + live runtime overlay
```

Do not create a separate `SclConnectedModel` that duplicates the semantic tree.

Use overlays/evidence layers:

```text
CanonicalStructuralModel
DeclaredConfiguration
LiveValidationEvidence
LiveRuntimeSnapshot
```

The UI may present one merged view, but the engine must retain source boundaries.

---

## 11. Declared configuration is not live runtime state

An opened SCL may declare DataSets, ReportControls, control models, services, and communication parameters.

That does not prove current online state such as:

```text
RptEna
Resv / Owner
current EntryID
buffer contents
current dynamic DataSet directory
current values
current association state
```

Offline import must not fabricate live evidence.

When connected, live evidence may validate or overlay declared configuration, but the exporter/provenance layer must still know which value came from which source.

---

## 12. One exporter for both origins

The same components should be used after canonicalization:

```text
CanonicalIedModel
    -> SclSemanticModelBuilder
    -> SclEditionCompatibilityTransformer
    -> SclXmlSerializer
    -> SclSchemaValidator
    -> export diagnostics/evidence
```

No branch like:

```text
if source == LiveDiscovery:
    use ExporterA
else if source == OpenScl:
    use ExporterB
```

Origin belongs in provenance, not in semantic serialization logic.

---

## 13. Semantic equivalence, not byte equality

The primary round-trip goal is **semantic idempotence**.

For same-edition normalized round trip:

```text
Open SCL
 -> canonicalize
 -> Save same edition
 -> Open generated SCL
 -> canonicalize

semantic fingerprint A == semantic fingerprint B
```

XML byte equality is not required.

Whitespace, attribute order, generated type IDs, compatible lexical forms, and normalized ordering may differ while semantics remain equal.

---

## 14. Cross-ingress equivalence target

This is the strongest test for the generic model architecture.

For an IED that can be represented by both paths:

```text
Path A:
Discover live
 -> canonical model A

Path B:
Save normalized SCL from A
 -> Open that SCL
 -> canonical model B

Expected:
canonicalSemanticFingerprint(A, representableSubset)
    ==
canonicalSemanticFingerprint(B, representableSubset)
```

Provenance and live-only runtime fields are excluded from the comparison.

This proves that live discovery and SCL import converge on one semantic representation.

---

## 15. Cross-edition equivalence target

For every supported target edition:

```text
Canonical A
 -> export profile X
 -> import profile X
 -> Canonical B
```

Compare only the semantic subset representable in profile X.

Required result:

```text
RepresentableSemanticSubset(A, X)
    ==
RepresentableSemanticSubset(B, X)
```

Any dropped feature must appear in an explicit loss report.

---

## 16. Normalization must be deterministic

For a fixed canonical model and export profile:

```text
same input semantics
 + same export profile
 + same deterministic options
 = same normalized output
```

Avoid dependence on:

- thread scheduling;
- UI order;
- hash-map iteration order;
- discovery arrival order when semantics are equivalent;
- current wall-clock time unless intentionally stored in metadata;
- random synthetic IDs.

A worker may make parsing/export responsive; it must not change the result.

---

## 17. Suggested engine components

Names are illustrative; reuse existing project models where possible.

```text
SclEditionDetector
SclImportProfile
SclSemanticImporter
SclReferenceResolver
SclImportNormalizer
SclSourceEvidence
CanonicalIedModel
CanonicalModelFingerprint
CanonicalEvidenceReconciler
LiveRuntimeOverlay
SclExportProfile
SclEditionCompatibilityTransformer
SclXmlSerializer
SclSchemaValidator
SclConversionLossReport
```

Do not create duplicate models merely to match this list.

---

## 18. Recommended implementation sequence

### Step 1 — Define canonical model boundary

Audit current `SclDocument`/live-model types and identify which type can become or feed the shared canonical semantic model.

Do not write a new exporter until this boundary is explicit.

### Step 2 — Build source-edition import profiles

Parse supported editions into typed semantics and normalize edition-specific forms.

### Step 3 — Add canonical semantic fingerprint

The fingerprint should ignore source lexical trivia and include the semantics needed for cross-ingress/round-trip tests.

### Step 4 — Reuse the existing target export profiles

Open-SCL export and discovery export must use the same transformer/serializer.

### Step 5 — Add source-loss diagnostics

Every unrepresentable source semantic/private field must be visible.

### Step 6 — Add SCL + live validation overlay

Reuse the SCL-assisted connection path. Do not mutate the structural model silently on mismatch.

### Step 7 — Only then add UI workflow

UI flow can be:

```text
Open SCL
 -> normalized model loaded
 -> Save SCL...
 -> choose target edition
 -> preview conversion warnings
 -> export
```

The dialog is presentation; the conversion engine must remain UI-independent.

---

## 19. Regression tests to add

Project-owned synthetic fixtures should cover:

```text
Open Ed2 -> save Ed2 -> reopen -> semantic idempotence
Open Ed1.6 -> save Ed2 -> loss/unknown semantics explicit
Open Ed2 -> save Ed1.4 -> reopen -> representable subset stable
Discover synthetic live model -> save -> reopen -> same semantic subset
same canonical model -> all supported target editions
source IED@name preserved as identity evidence
SCL/live identity mismatch fails or diagnoses explicitly
unknown/private source extension retained as source evidence or reported dropped
source type IDs preserved as aliases but not required for semantic equality
runtime-only RCB state never invented by offline Open SCL
threaded and synchronous normalization produce identical fingerprint
```

---

## 20. Acceptance invariants

Before merging Open-SCL/Save-SCL work, verify:

1. Live discovery and SCL import converge on one canonical semantic model.
2. Open SCL parses semantics; it does not keep XML DOM as the protocol source of truth.
3. Source edition is decoded through a typed import profile.
4. Target edition is encoded through the same typed export profile used for discovered models.
5. Unknown/not-representable semantics are not silently converted to false/default.
6. Source-only metadata is preserved separately or explicitly diagnosed when dropped.
7. Original type IDs are source aliases, not semantic identity.
8. `IED@name` is authoritative for the opened file and is cross-validated, not silently rewritten, on live connect.
9. Declared SCL configuration is distinct from live runtime state.
10. Save SCL requires no network I/O.
11. Repeated normalization is deterministic.
12. Same-edition round trip is judged by semantic fingerprint, not byte equality.
13. Cross-edition round trip compares only the representable semantic subset.
14. Conversion loss is explicit.
15. Worker/thread scheduling cannot change semantic output.
16. The exporter does not have separate logic branches for live-origin and file-origin models after canonicalization.
17. Public tests/fixtures remain project-owned and provenance compliant.

---

## 21. What is observed versus what remains to prove

Observed/reported workflow evidence supports:

```text
Open SCL
 -> same multi-edition Save SCL choices
 -> normalized output similar to discovery-derived output
```

This strongly supports the common-model architecture above.

Still to prove with controlled project evidence:

- exact semantic diff between source SCL and normalized re-export for each edition;
- whether all private/vendor extensions are intentionally dropped, preserved, or transformed;
- exact ordering/type-ID policy after Open SCL;
- whether normalized Open-SCL output and discovery output are fully semantically equal for the same IED, or only equal on a common subset;
- downgrade/upgrade loss behavior for features unavailable in older schemas.

These are future test targets, not reasons to create a second model architecture.

---

## 22. Final mental model

Remember this shape:

```text
              LIVE DISCOVERY
                    |
                    v
             typed wire evidence
                    |
                    +-------------------+
                                        |
                                        v
                              CANONICAL IED MODEL
                                        ^
                                        |
                    +-------------------+
                    |
                    v
              SCL IMPORT/NORMALIZE
                    ^
                    |
                 OPEN SCL

CANONICAL IED MODEL
        |
        +--> live validation/runtime overlay when connected
        |
        +--> Edition 2 export
        +--> Edition 1.6 export
        +--> Edition 1.5 export
        `--> Edition 1.4 export
```

**One model. Multiple ingress paths. Multiple export profiles. No semantic duplication.**
