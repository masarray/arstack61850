# Sampled Values Interoperability Roadmap

ARStack61850 should simulate and analyze Sampled Values streams from standards-described merging-unit configurations **without vendor-specific code paths**.

The interoperability target is not a list of brands. The target is a growing, testable set of IEC 61850 / instrument-transformer profile semantics that can be compiled from engineering intent and verified against observed wire behavior.

## North-star behavior

```text
SCL / CID / SCD / IID                  optional lab capture
        |                                      |
        v                                      v
secure SCL parser                     observed wire fingerprint
        |                                      |
        v                                      |
DataSet + type resolver                       |
        |                                      |
        +-- explicit FCDA leaf                 |
        |                                      |
        +-- structured FCDA -> ordered leaf    |
        |                                      |
        +------------------+-------------------+
                           v
                 profile/evidence resolver
                           |
                           v
                SvPublisherProfile compiler
                           |
                           +-- L2 address / VLAN / APPID
                           +-- svID / confRev / nofASDU
                           +-- sample-rate and sample-mode semantics
                           +-- SmvOpts field-presence policy
                           +-- ordered wire leaf layout
                           +-- validated sample-counter policy
                           +-- synchronization policy inputs
                           |
                           v
                 versioned binary device profile
                           |
                           v
                  RTOS deterministic publisher
                           |
                           v
                  hardware TX timestamp evidence
```

XML parsing, string resolution, type-template traversal and capture fingerprinting belong on the host. The embedded realtime hot path receives an already validated, immutable profile and prebuilt packet layout.

## Three truth domains

ARStack must never collapse these into one source of truth:

1. **Configured intent** — what SCL/CID/SCD/IID says should exist.
2. **Observed wire facts** — what an independent capture actually contains: field presence, identity, counter progression, cadence and other measurable behavior.
3. **Profile/standard rules** — semantics required by the applicable standards/profile family.

A conflict is diagnostic evidence, not permission to silently overwrite one domain with another. The engineer or an explicit profile rule decides which behavior is deployed.

This separation is essential for broad interoperability because an engineering file may not encode every runtime behavior, and a capture may reflect a different configuration revision than the available engineering file.

## Tolerance contract: forgiving representation, strict semantics

Real vendor SCL/CID files often carry different namespace prefixes, `Private` blocks, foreign-namespace metadata, lexical formatting and tool-specific ordering around the same IEC 61850 engineering intent. ARStack should understand those differences without weakening the semantic gates that protect deterministic publishing.

The compatibility engine therefore follows this rule:

```text
malformed / unsafe XML                -> REJECT
well-formed SCL + unfamiliar metadata -> TOLERATE + QUARANTINE/DIAGNOSE
known semantic equivalent             -> NORMALIZE + PROVENANCE
unknown mandatory semantic            -> MISSING_CONTEXT / UNSUPPORTED
fully resolved runtime profile         -> CLASS A / DEPLOYABLE
```

Tolerance applies only where equivalence can be established safely. The engine may normalize namespace prefixes, standard lexical equivalents, harmless whitespace, equivalent L2 text representations and known edition/profile differences. It must not make XML element names arbitrarily case-insensitive, invent missing references, infer unknown payload layouts or promote synchronization state merely to make a file parse.

Vendor extensions are quarantined from the canonical model unless an explicit, validated adapter maps them to a known semantic. Unknown extension data is bounded, non-executable metadata with path/namespace/type provenance; it cannot silently override core IEC 61850 values.

Each normalized field should eventually carry explainable provenance such as:

- `exact_scl`;
- `standard_equivalent`;
- `tolerated_lexical`;
- `profile_rule`;
- `vendor_extension`;
- `observed_wire`;
- `user_confirmed`;
- `unresolved`.

The practical product rule is: **parse broadly, explain normalization, deploy narrowly**. A CID may be accepted for inspection even when some semantics remain unresolved; the same profile is not device-deployable until every mandatory runtime field has passed the Class-A gates.

## Interoperability rules

1. **SCL is configured intent.** Do not replace SCL values with convenient defaults when the configuration provides an explicit value.
2. **Resolved type order is wire order.** FCDA order is preserved. When an FCDA points at a structured data object without `daName`, ARStack expands the matching functional-constraint leaves in `DataTypeTemplates` order.
3. **Unsupported means rejected, not guessed.** Unknown basic types, ambiguous DataSet bindings, incomplete address bindings and unresolved structures must produce explicit diagnostics.
4. **Optional fields are profile data.** `SmvOpts` is preserved so canonical ASDU field presence is not inferred from what a particular analyzer prefers.
5. **Timing is rational, not truncated.** A requested rate such as 4800 samples/s must not become a fixed 208 us period. The scheduler distributes integer timer ticks so long-term rate has no truncation drift.
6. **Counter policy is independently validated.** `SmpPerSec` can establish publisher rate, but it must not be treated as universal proof of the `smpCnt` wrap rule. A rate-sized modulus may be shown as a candidate for inspection; device deployment requires an applicable profile rule or observed-evidence confirmation.
7. **Synchronization is evidence-driven.** Configuration and packet shape cannot promote `smpSynch`; measured clock state controls synchronization claims.
8. **Diagnostic traffic is separate.** Bench mirrors/probes must never change canonical stream identity or semantics.
9. **Regression evidence is vendor-neutral.** Real lab configurations may inform anonymous fixtures, but proprietary configuration files, commercial names and product comparisons are not committed.
10. **Tolerance is bounded.** Representation quirks can be normalized when equivalence is known; unresolved mandatory semantics remain non-deployable.

## Compatibility classes

### Class A — deterministic publishable

A stream can be compiled completely:

- destination MAC and APPID resolved;
- VLAN binding either complete or absent;
- svID resolved;
- sample rate/mode understood;
- nofASDU understood;
- DataSet binding resolved;
- every transmitted leaf has a deterministic supported wire width;
- counter policy is validated, not merely guessed from rate;
- optional-field policy is known;
- any configured-vs-observed conflict required for deployment is resolved explicitly.

Class A profiles can proceed to the device compiler/runtime.

### Class B — valid configuration needing runtime/profile context

The stream is structurally understood but deployment needs additional context. Examples include:

- samples-per-period mode without nominal system frequency;
- an unresolved sample-counter wrap policy;
- a capture-derived runtime behavior that conflicts with an older engineering file;
- a profile family whose optional-field semantics require an explicit selection.

Class B is not guessed. The host UI/CLI must request, resolve or validate the missing context explicitly before deployment.

### Class C — unsupported or ambiguous

Any unresolved type/layout, ambiguous binding, unknown mandatory profile rule or inconsistent configuration is rejected with a diagnostic. Class C streams are interoperability backlog items, not silent fallbacks.

## P3-A0 — normalized host profile foundation

Current goals:

- preserve `SmvOpts` in the SCL model;
- expand structured FCDA definitions to ordered leaf entries;
- compile `SclSampledValuesStream` into `SvPublisherProfile`;
- derive publisher rate from supported sampling semantics without hard-coding 4000;
- keep sample-counter policy explicit and independently validated;
- provide drift-free rational timer scheduling;
- inspect arbitrary SCL/CID/SCD/IID files with a read-only CLI;
- maintain anonymous fixtures for 4000 and 4800 samples/s families;
- add bounded SCL dialect normalization and vendor-extension quarantine tracked by #40.

The initial fixed-width compiler intentionally supports only leaf types whose wire size is explicit in ARStack. New types should be added from standards/profile evidence with regression tests rather than broad assumptions.

## P3-A1 — compiled device profile

Next:

- define a versioned binary schema;
- include schema version, total length and integrity check;
- serialize only resolved runtime values, never XML;
- require a validated counter policy before a profile is device-deployable;
- validate on host and device;
- activate profiles atomically;
- prebuild packet templates and patch tables before realtime start;
- move destination MAC, APPID, VLAN, svID, confRev, nofASDU, sample rate, validated counter modulus and dataset layout out of embedded hard-coded constants;
- integrate `RationalTickSchedule` with the ESP32-P4 timer path for non-integer-microsecond rates.

## P3-A2 — observed fingerprint + interoperability matrix

Add a read-only capture fingerprint layer that can measure, per stream:

- destination/source MAC and VLAN visibility;
- APPID and ASDU field presence;
- svID / confRev / nofASDU;
- sample-counter progression, wrap candidates, duplicates and gaps;
- observed frame cadence and rate distribution;
- payload length/layout consistency;
- synchronization values as **observed fields**, not synchronization proof.

The fingerprint augments SCL; it never silently replaces SCL. Conflicts are surfaced as configured-vs-observed diagnostics.

Grow coverage by anonymous standards/profile families:

| Capability | Minimum regression evidence |
|---|---|
| 4000 samples/s | exact 250 us nominal schedule + validated counter policy |
| 4800 samples/s | drift-free rational schedule + validated counter policy |
| Explicit FCDA leaves | ordered primitive + quality layout |
| Structured FCDA | recursive type expansion filtered by FC |
| Optional ASDU fields | each supported `SmvOpts` combination encoded intentionally |
| Address variants | APPID, multicast MAC, tagged/untagged VLAN handling |
| Multiple streams | independent identity, counter and timing state |
| nofASDU variants | deterministic frame construction and counter semantics |
| Additional fixed leaf types | standard-derived width + golden-wire tests |
| Config vs capture conflict | explicit diagnostic, no silent override |
| Vendor metadata / dialect variance | equivalent canonical profile + normalization provenance |

A profile family enters the supported matrix only when parser/profile tests, encoded-wire tests and physical interoperability evidence agree.

## Relation to hardware timing work

Profile correctness and timing correctness are separate gates that converge at the embedded publisher:

```text
configured/profile truth              timing truth
SCL -> SvPublisherProfile             timer deadline
          |                              |
          +-----------+------------------+
                      v
             deterministic TX
                      |
                      v
             EMAC HW timestamp
                      |
                      v
                evidence report
```

A stream can be byte-correct but badly timed, or well timed but configured incorrectly. ARStack must be able to distinguish those failure modes.

## Definition of success

The injector should eventually let an engineer select a standards-described SV stream, validate it, optionally compare it with a lab capture fingerprint, bind signal values/scenarios, deploy it to dedicated hardware, and receive deterministic timing evidence without manually editing packet bytes or creating device-specific firmware builds.

The analyzer should consume the same normalized profile/evidence model for expected-vs-observed checks, so injector and analyzer share one interpretation of stream identity, dataset order, validated counter policy, optional fields and timing expectations.