# external IEC 61850 client Discovery Profile v1

This file records the measured vendor/external vendor-style external IEC 61850 client discovery pattern used as an external interoperability target for the ARStack IEC 61850 Workbench simulator.

It is an evidence profile, not a claim that external external IEC 61850 client interoperability is complete. Real-client validation remains the release gate.

## Transport

| Field | Measured value |
| --- | ---: |
| TCP port | 102 |
| COTP Source TSAP | `0000` |
| COTP Destination TSAP | `0001` |
| COTP TPDU size | 1024 bytes |

## MMS Initiate

| Field | Measured value |
| --- | ---: |
| `localDetailCalling` | 65000 |
| `maxOutstandingCalling` | 10 |
| `maxOutstandingCalled` | 10 |
| `nestingLevel` | 5 |

ARStack IED Simulator parity runtime currently negotiates the same server ceilings: MMS PDU 65000, outstanding calling/called 10/10, nesting level 5, and COTP TPDU code `0x0A` (1024 bytes). Negotiated values remain the minimum of peer and server limits.

## Invoke behavior

- first confirmed-service invoke ID: **1**
- last observed invoke ID: **418**
- progression: **strict +1**
- request flow: predominantly synchronous / ordered

Server contract: echo the request invoke ID exactly. Do not require this specific starting value for generic MMS interoperability.

## Discovery service distribution

| MMS service | Count |
| --- | ---: |
| GetNameList | 140 |
| GetVariableAccessAttributes | 119 |
| Read | 157 |
| GetNamedVariableListAttributes | 2 |

No mutation was observed in this discovery capture:

- Write: 0
- Control: 0
- RCB enable: 0

## Initial discovery

The first directory request is:

- `GetNameList`
- object class: **Domain**
- scope: **VMD**

Observed result: **32 MMS domains**.

## Named-variable discovery

external IEC 61850 client then walks NamedVariables per domain.

Required behavior:

- domain-specific `GetNameList(NamedVariable)`;
- pagination supported;
- `continueAfter` is the **last identifier returned by the previous page**;
- no duplicate or missing identifiers across page boundaries;
- stable deterministic ordering for the lifetime of the association/model generation.

Measured profile:

- NamedVariable pages: **105**
- continuation pages: approximately **73**

This exceeds the assumptions of a dispatcher that first materializes an entire domain directory into a small fixed identifier array. ARStack therefore must page directly from the bounded static object table rather than require the complete directory to fit in one intermediate buffer.

## Type discovery

- `GetVariableAccessAttributes` is used primarily at Logical Node roots.
- Type discovery is interleaved with `GetNameList`; it is not a separate all-at-once phase.

The simulator must therefore keep root TypeSpecification and leaf aliases mutually consistent throughout discovery.

## DataSet discovery

Observed sequence:

1. `GetNameList(NamedVariableList)`
2. `GetNamedVariableListAttributes`

The measured discovery performs two `GetNamedVariableListAttributes` operations.

ARStack also retains compatibility for VMD-specific, AA-specific and domain-specific NamedVariableList probes observed in the known-good ARIEC61850 interoperability trace.

## Professional interoperability gates

A build may be described as matching this discovery profile only when all of the following hold:

1. COTP and MMS association negotiation completes without fallback hacks.
2. Initial VMD Domain discovery returns the complete domain set.
3. Domain-specific NamedVariable pagination can exceed the internal one-page identifier capacity and still complete using `continueAfter`.
4. Invoke IDs are echoed exactly across the full ordered walk.
5. GVAA and Read operations remain aligned with the requested objects and one Read AccessResult is retained per requested variable/member.
6. DataSet discovery completes through `GetNamedVariableListAttributes`.
7. The association remains active through the complete discovery sequence.
8. A real external IEC 61850 client client reproduces the progression; CI/loopback evidence alone is not relabelled as vendor interoperability.

## Current implementation evidence

The external-profile hardening series that consumes this profile includes:

- Read compatibility for list-of-variable, DataSet `variableListName`, and the proven unwrapped compatibility form;
- bounded 128-variable bulk Read support covering the observed 78-variable discovery Read;
- VMD / AA / domain DataSet discovery compatibility;
- streaming domain-specific NamedVariable pagination so a large directory is not rejected merely because its total identifier count exceeds `MmsServiceSpanCodec::maximum_identifiers`;
- an embedded hard-profile regression with 160 NamedVariables in one domain, multiple ordered invoke IDs, exact `continueAfter`, and final `moreFollows=false`.

The real-client acceptance gate remains open until an external external IEC 61850 client run progresses past the previous `GetNameList -> Read invoke 2 -> close` failure point and completes the later discovery stages.
