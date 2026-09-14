# RCB Reporting Quick Reference and Wire Contract

> **READ THIS FIRST before changing RCB, DataSet, InformationReport, reconnect, or report-decoder code.**

This document is the compact implementation contract for IEC 61850 reporting in ARStack61850. It combines standards-facing invariants with vendor-neutral black-box wire observations from controlled laboratory captures.

It is intentionally written so a new engineer or AI development thread can recover the correct mental model quickly without replaying the original analysis.

The observations are interoperability evidence, **not** a claim that one capture defines every IEC 61850 implementation. Exact object names, counts, timings, and configuration values from a capture are examples only.

Related documents:

- [`../REPORTING_PROFILE.md`](../REPORTING_PROFILE.md) — current ARStack reporting scope and runtime boundary.
- [`../DYNAMIC_DATASET_PROFILE.md`](../DYNAMIC_DATASET_PROFILE.md) — guarded dynamic NamedVariableList lifecycle.
- [`STATIC_RCB_LIVE_TRIAL.md`](STATIC_RCB_LIVE_TRIAL.md) — guarded static-RCB laboratory workflow.
- [`../ASSOCIATION_RUNTIME_PROFILE.md`](../ASSOCIATION_RUNTIME_PROFILE.md) — persistent association and asynchronous receive boundary.

## 1. Thirty-second mental model

Reporting is **not** `RptEna=true` plus a blocking response loop.

```text
                     ONE MMS ASSOCIATION
                            |
             +--------------+---------------+
             |                              |
    confirmed operations              unconfirmed traffic
    Read / Write / etc.                InformationReport
             |                              |
      invoke-ID routing                no invoke ID
             |                              |
             +--------------+---------------+
                            |
                     report state machine
```

The receive pump must remain able to dispatch `InformationReport` while confirmed requests are in flight.

The four common activation paths are:

| DataSet / RCB mode | Observed compatibility sequence |
|---|---|
| Static DataSet + URCB | `Read RCB -> Resv=true -> RptEna=true -> readback -> listen` |
| Static DataSet + BRCB | `Read RCB -> RptEna=true -> readback -> listen` |
| Dynamic DataSet + URCB | `Define DataSet -> verify/probe -> Read RCB -> Resv=true -> configure + RptEna last -> readback -> listen` |
| Dynamic DataSet + BRCB | `Define DataSet -> verify/probe -> Read RCB -> configure + RptEna last -> readback -> listen` |

**Do not collapse URCB and BRCB into one blind enable sequence.**

## 2. Repository implementation rule

ARStack should model reporting with explicit phases:

```text
inventory / select candidate
        |
        v
read current RCB state
        |
        v
prove candidate is usable
        |
        +---- static DataSet ----+
        |                        |
        +---- dynamic DataSet --> Define + verify exact ordered members
        |
        v
claim when required
        |
        v
configure while disabled
        |
        v
enable with RptEna last
        |
        v
read back effective server state
        |
        v
receive asynchronous InformationReports
        |
        v
track continuity / reconnect / cleanup
```

A successful MMS `Write` is not enough to claim reporting works. Production/report-authority evidence requires a correctly decoded and correctly mapped `InformationReport`.

## 3. Static URCB behavior

A controlled static-URCB activation showed:

```text
Read URCB root
    -> verify RptEna=false and reservation/Owner free
Write Resv=true
Write RptEna=true
Read URCB root
Read URCB root again in the observed client
    -> effective RptEna=true
    -> effective Resv=true
    -> Owner identifies the client
listen for InformationReport
```

The client did **not** rewrite `DatSet`, `TrgOps`, `OptFlds`, `BufTm`, or `IntgPd` for an already configured static URCB.

Implementation invariant:

- reserve an URCB only after a fresh read says it is safe to claim;
- do not overwrite another client's enabled/reserved RCB;
- do not assume a static RCB requires configuration writes when its existing binding is already the intended one.

## 4. Static BRCB behavior

A controlled static-BRCB activation showed:

```text
Read BRCB root
Write RptEna=true
Read BRCB root
Read BRCB root again in the observed client
listen for InformationReport
```

No explicit `Resv=true` write was observed. Ownership/reservation-related BRCB state became visible after enable.

Implementation invariant:

- do not force the URCB `Resv -> RptEna` sequence onto BRCB;
- model BRCB ownership and `ResvTms`/Owner semantics separately from URCB reservation semantics.

The duplicate readback seen in the reference client is **not** treated as a protocol requirement. At least one authoritative post-write readback is the useful compatibility invariant.

## 5. Dynamic DataSet activation

### 5.1 DataSet definition

Observed dynamic activation begins with `DefineNamedVariableList` for an exact ordered member list.

ARStack's safer default should remain:

```text
DefineNamedVariableList
    -> successful confirmed response
GetNamedVariableListAttributes
    -> exact member count
    -> exact member identity
    -> exact member order
```

One observed reference client did not immediately verify the definition with `GetNamedVariableListAttributes`. That is useful behavioral evidence, but it is **not** a reason to weaken ARStack's guarded default.

DataSet member order is semantic because report values can be mapped by inclusion index when `DataReference` is absent.

### 5.2 Dynamic URCB

Observed sequence:

```text
Define dynamic DataSet
Read candidate URCB
    -> disabled
    -> unreserved
    -> free Owner
Write Resv=true
ONE multi-variable Write:
    1. DatSet
    2. IntgPd
    3. TrgOps
    4. OptFlds
    5. RptEna=true     <-- last
inspect every WriteResult
Read RCB back
listen for reports
```

### 5.3 Dynamic BRCB

Observed sequence:

```text
Define dynamic DataSet
Read candidate BRCB
    -> disabled / usable
ONE multi-variable Write:
    1. DatSet
    2. IntgPd
    3. TrgOps
    4. OptFlds
    5. RptEna=true     <-- last
inspect every WriteResult
Read RCB back
listen for reports
```

No separate URCB-style `Resv=true` write was observed for BRCB.

### 5.4 Multi-variable Write rule

A reporting client must support a single MMS `Write` containing multiple RCB attribute targets and must inspect the result for **each** target.

Do not treat this as atomic merely because the attributes were sent in one confirmed request. A partial result must remain visible and must drive explicit cleanup/diagnostics.

`RptEna` should be planned last so configuration fields precede activation.

## 6. Requested state is not effective state

Always distinguish:

```text
requested configuration
        !=
confirmed Write success
        !=
effective server readback
```

Observed examples:

- a dynamic URCB accepted requested `OptFlds` containing BRCB-oriented fields and normalized the effective readback by clearing non-applicable bits;
- a dynamic BRCB retained the BRCB-oriented `BufferOverflow` and `EntryID` options;
- `ConfRev` changed server-side after DataSet/configuration binding without the client directly writing `ConfRev`.

Therefore:

1. preserve the requested configuration for evidence;
2. inspect every Write result;
3. re-read the RCB;
4. treat readback as the effective operational state;
5. never fabricate equality between requested and effective state.

## 7. OptFlds quick map

The observed report profile follows the usual IEC 61850 optional-field ordering. For the first payload octet:

| Payload mask | Semantic field |
|---:|---|
| `0x40` | sequence number |
| `0x20` | report timestamp |
| `0x10` | reason for inclusion |
| `0x08` | DataSet name |
| `0x04` | data reference |
| `0x02` | buffer overflow |
| `0x01` | EntryID |

The next significant bit selects `ConfRev`; segmentation is separate.

Observed compatibility example:

```text
requested URCB OptFlds payload:  0x7B 0x80
effective URCB readback:         0x78 0x80

requested BRCB OptFlds payload:  0x7B 0x80
effective BRCB readback:         0x7B 0x80
```

These bytes are capture evidence, not universal defaults.

## 8. Trigger options: preserve raw bits and semantic bits separately

`TrgOps` is a BIT STRING whose leading significant bit is reserved. Do not map the first significant bit directly to `data-change`.

Six significant positions:

| BIT STRING bit index | Payload mask | Meaning |
|---:|---:|---|
| 0 | `0x80` | reserved |
| 1 | `0x40` | data-change (`dchg`) |
| 2 | `0x20` | quality-change (`qchg`) |
| 3 | `0x10` | data-update (`dupd`) |
| 4 | `0x08` | integrity / period |
| 5 | `0x04` | general interrogation |

A compatibility capture contained a request whose significant payload had the reserved high bit set along with all standard trigger bits. The server accepted it. Treat this as an interoperability quirk, **not** a reason to redefine the standard semantic map.

Recommended policy:

```text
strict mode:
    reject or diagnose invalid reserved/non-applicable bits

explicit compatibility mode:
    retain raw request evidence
    tolerate a known quirk only by policy
    normalize effective state where appropriate
    expose the normalized readback
```

Do not silently make the standards-facing codec permissive globally.

## 9. ReasonForInclusion: critical bit numbering

This is a high-priority implementation invariant.

The observed report wire encoding and IEC bit ordering have a reserved bit at index 0:

| BIT STRING bit index | Payload mask | Meaning |
|---:|---:|---|
| 0 | `0x80` | reserved |
| 1 | `0x40` | data-change |
| 2 | `0x20` | quality-change |
| 3 | `0x10` | data-update |
| 4 | `0x08` | integrity |
| 5 | `0x04` | general interrogation |

Observed event evidence:

```text
ReasonForInclusion payload = 0x40
changed member only
=> data-change
```

Observed periodic evidence:

```text
ReasonForInclusion payload = 0x08
full periodic DataSet snapshot
=> integrity
```

**Never map wire bit index 0 to `data-change`.**

If an implementation supports an additional extension/application-trigger reason, model it only from explicit grammar/evidence. Do not shift the standard reasons to make room for it.

## 10. InformationReport mapping

Observed reports used an MMS Unconfirmed-PDU with `InformationReport` and a VMD-specific `variableListName` of `RPT`.

The receive path must not expect an invoke ID for reports.

When `DataReference` is absent:

```text
DataSet directory member order
        +
inclusion BIT STRING
        +
included AccessResults in order
        =
correct member/value mapping
```

Example conceptual mapping:

```text
DataSet[0] = member A
DataSet[1] = member B
DataSet[2] = member C
DataSet[3] = member D

inclusion = [0, 2]
report values = [value-for-A, value-for-C]
```

A DataSet member can decode to a nested MMS `Structure`; do not assume each report member is a scalar leaf.

ReasonForInclusion is associated with the included members, not with absent members.

## 11. Integrity and event scheduling are independent

Observed BRCB behavior contained both:

- periodic integrity reports that included the full DataSet; and
- selective event reports containing only changed members.

A data-change report was observed immediately before a scheduled integrity report. The event did not reset or suppress the integrity cadence.

Model this conceptually as:

```text
event trigger ---------+
                       +--> report capture/queue --> delivery
integrity timer -------+
```

Do not implement integrity as a side effect of the event scheduler.

## 12. BRCB retention, reconnect, and replay

A controlled reconnect capture established the following behavioral model:

```text
association lost
    -> RptEna no longer active for that client
    -> BRCB configuration remains
    -> DataSet binding remains
    -> retained report capture continues
    -> EntryID continues advancing

new association
    -> associate
    -> validate/read BRCB
    -> observe persisted configuration and latest entry state
    -> choose resume policy
    -> RptEna=true
    -> retained reports replay
    -> live reporting continues
```

### 12.1 Replay is not paced by IntgPd

`IntgPd` governs periodic report generation. It does **not** pace replay.

In one capture, minutes of retained history replayed in tens of milliseconds after re-enable.

The receive loop must tolerate a burst of reports immediately after `RptEna=true`.

### 12.2 Replay can interleave with confirmed traffic

Observed ordering included:

```text
InformationReport
InformationReport
client Read request
InformationReport
Read response
InformationReport
...
```

Therefore confirmed response routing and unconfirmed report dispatch must be independent.

### 12.3 EntryID and SqNum have different continuity roles

Observed reconnect behavior:

```text
EntryID: ... 36, 37, 38 | 39, 40, ...
SqNum:   ... 35, 36, 37 |  0,  1, ...
                         ^ re-enable/new delivery stream
```

Use this mental model:

```text
SqNum   -> delivery-stream continuity
EntryID -> buffered-record identity / recovery continuity
```

Do not use `SqNum` as the durable BRCB reconnect cursor.

### 12.4 Resume policy

If no last-consumed EntryID is supplied, enabling a BRCB may replay retained history from the server's default/oldest delivery point. This can include duplicates already consumed before disconnect.

A robust client should keep an explicit policy:

```text
no trusted resume EntryID
    -> accept/diagnose replay from available retained history

trusted last-consumed EntryID and server supports resume
    -> resume after that EntryID
    -> validate continuity

requested EntryID unavailable / overflow gap
    -> expose recovery gap
    -> do not pretend continuity
```

### 12.5 Byte-stable replay evidence

One retained report was replayed byte-for-byte identically to its original captured encoding.

That is high-value interoperability evidence but not a universal protocol requirement. ARStack's current static-BRCB design of retaining the encoded MMS report PDU in bounded slots is consistent with this observation and is a useful deterministic implementation choice.

### 12.6 BufferOverflow is historical evidence

A replayed report retains its original `BufferOverflow` flag. Do not interpret the flag as necessarily meaning that an overflow occurred at the moment of reconnect/replay.

Track the flag together with EntryID/replay continuity.

## 13. URCB and BRCB must remain distinct

| Property | URCB | BRCB |
|---|---|---|
| Explicit `Resv` claim observed | yes | no in the observed activation paths |
| Offline retained history | no durable BRCB-style replay | yes |
| EntryID recovery role | not the BRCB recovery cursor | important |
| BufferOverflow field | non-applicable / may be normalized away | relevant |
| Reconnect | re-establish live subscription | may replay retained history before live continuation |

This table is an implementation guide, not a substitute for the standard or device-specific evidence.

## 14. Known ARStack implementation deltas: do not use current code as an oracle here

The new wire evidence exposes correction work that must remain explicit until fixed and regression-tested.

### 14.1 ReasonForInclusion semantic shift

At the time this document was written, `src/mms/reporting.cpp` associates semantic reason names starting at BIT STRING bit index 0. Wire evidence requires bit index 0 to remain reserved and `data-change` to start at bit index 1.

**Required correction:** preserve raw bits, then map standard semantic names from the correct indexes.

### 14.2 Static BRCB report-reason masks

At the time this document was written, `src/mms/static_brcb_runtime.cpp` uses report-reason masks shifted one bit toward the reserved position.

**Required correction:** align emitted reason masks with the table in section 9 and add exact wire regression tests.

### 14.3 Static BRCB periodic reporting

The current static BRCB runtime has retained slots and event-driven capture/replay primitives, but its trigger/capture path is not yet the complete observed BRCB behavior for periodic integrity/GI scheduling.

**Required extension:** separate event and integrity/GI scheduling paths that feed the same bounded retained queue.

### 14.4 Existing BRCB queue design is directionally correct

The current static BRCB runtime already has bounded retained report storage and operations such as replay/resume/rewind/purge. Keeping encoded report bytes with EntryID/SqNum/overflow metadata is consistent with the observed byte-stable replay behavior.

Do not rewrite the retained-queue architecture merely to fix bit semantics or add integrity scheduling.

## 15. Implementation invariants

Before merging reporting changes, verify all applicable rules:

1. One association receive pump can route confirmed responses and unsolicited reports concurrently.
2. Confirmed responses remain correlated strictly by invoke ID.
3. RCB state is freshly read before mutation.
4. Occupied/foreign-owned RCBs are not silently taken over.
5. Static bindings are not rewritten without an explicit reason.
6. Dynamic DataSet member identity and order are verified before report mapping authority is granted.
7. URCB reservation and BRCB activation are not treated as identical.
8. Configuration occurs while disabled; `RptEna` is the final activation field in a grouped configuration write.
9. Every individual `WriteResult` is inspected.
10. Effective RCB state is read back after mutation.
11. `ConfRev` is treated as server-derived effective state.
12. `TrgOps` bit index 0 is reserved.
13. `ReasonForInclusion` bit index 0 is reserved; `0x40` means data-change and `0x08` means integrity.
14. DataSet member ordering is preserved exactly.
15. Inclusion indexes drive value/member mapping when DataReference is absent.
16. Nested MMS Data values are supported in report members.
17. Event and integrity scheduling are independent.
18. BRCB replay can arrive immediately and in bursts.
19. BRCB replay can interleave with confirmed Read/Write responses.
20. EntryID, not SqNum, is the durable buffered-record recovery identity.
21. Duplicate replay is possible when no resume cursor is supplied.
22. BufferOverflow is retained report evidence, not necessarily a reconnect-time event.
23. Strict standards-facing validation and optional compatibility normalization remain separate policies.
24. Capture-specific counts, timings, and example values are never hard-coded as IEC 61850 constants.

## 16. Suggested regression suite

High-value tests derived from the observations:

```text
URCB static claim:
    free -> Resv -> RptEna -> readback

BRCB static enable:
    free -> RptEna -> readback

Dynamic URCB grouped write:
    DatSet/IntgPd/TrgOps/OptFlds/RptEna-last
    per-item success/failure
    effective-state normalization

Dynamic BRCB grouped write:
    same ordering without URCB Resv

ReasonForInclusion exact vectors:
    0x40 -> data-change
    0x20 -> quality-change
    0x10 -> data-update
    0x08 -> integrity
    0x04 -> GI
    0x80 -> reserved/diagnostic, never data-change

Report routing:
    InformationReport arrives while confirmed Read is outstanding

Selective report:
    sparse inclusion -> exact DataSet indexes -> exact included values/reasons

Integrity report:
    full inclusion at configured cadence

BRCB reconnect:
    configuration persists
    retained reports replay before live continuation
    EntryID continuity survives SqNum reset

Resume:
    known EntryID -> resume-after semantics
    unknown/expired EntryID -> explicit gap/failure

Overflow:
    replayed BufferOverflow remains attached to its historical record
```

## 17. Evidence discipline and public-repository boundary

This document intentionally omits commercial product names and raw proprietary captures.

Public implementation work may retain:

- protocol service names;
- sanitized object shapes;
- exact ASN.1/BER facts needed for interoperability;
- synthetic project-owned regression vectors derived independently from the protocol grammar and minimum observed facts.

Do not commit raw external-client captures as permanent public fixtures. Reconstruct minimal synthetic tests instead.

Priority order for future reporting work:

```text
standards grammar
    > reproducible project-owned tests
    > measured vendor-neutral wire evidence
    > deterministic state-machine behavior
    > compatibility policy
    > convenience
```
