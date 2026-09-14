# IEC 61850 Reporting Profile

> **Implementation entry point:** read [`docs/RCB_REPORTING_REFERENCE.md`](docs/RCB_REPORTING_REFERENCE.md) before changing RCB activation, dynamic DataSet binding, InformationReport decoding, BRCB reconnect/replay, or report bit-field semantics.

The quick reference is the compact vendor-neutral contract derived from standards-facing semantics plus controlled wire evidence. Capture-specific values remain evidence, not universal IEC 61850 constants.

## Offline reporting

The reporting layer builds DataSet and BRCB/URCB inventory, decodes
GetNamedVariableListAttributes and InformationReport, maps OptFlds-ordered report fields,
preserves per-item errors, and tracks sequence/configuration/DataSet/overflow/segmentation
continuity under bounded retention.

## Subscription runtime

The explicit report-subscription surface re-probes the selected RCB, refuses unsafe takeover,
optionally reserves an URCB, configures only explicitly requested attributes, enables RptEna,
optionally issues GI, receives InformationReports, and cleans up only state touched by the
runtime. Lost-association cleanup is recorded rather than assumed.

The association receive path must continue routing asynchronous unconfirmed
`InformationReport` traffic while confirmed Read/Write operations are outstanding. Reporting is
therefore a persistent state machine, not a blocking `Write(RptEna=true)` transaction.

Before an armed Dynamic RCB lifecycle, the smart pre-claim failover layer runs
bounded repeated Read probes. A candidate that becomes enabled, reserved, or
flaps between probes is excluded from the current command and the pool selector
may rank another candidate. Once any mutation is attempted, automatic switching
is disabled; an uncertain write must be cleaned up and diagnosed on the original
RCB instead of risking a second remote mutation.

## Static DataSet report session

`MmsStaticReportSessionRuntime` composes the static pool selector, bounded
pre-claim contention/failover, populated DataSet-directory evidence, and the
persistent subscription runtime. `prepare()` is read-only and may rerank after
a busy or flapping candidate. `start()` uses the RCB's existing static binding;
the runtime rejects any request to rewrite `DatSet`.

The guarded `ariec61850_static_rcb_trial` host tool exposes this lifecycle for
authorized lab validation. Its default mode performs discovery and pre-claim
Reads only. Armed mode enables the selected free BRCB/URCB, optionally requests
GI, observes InformationReports, and cleans up only state acquired by the
runtime. See [docs/STATIC_RCB_LIVE_TRIAL.md](docs/STATIC_RCB_LIVE_TRIAL.md).

The current guarded single-profile run used a populated static URCB: three
pre-claim Reads remained stable, GI produced one decoded report with zero decode
failures, and disable/reservation release completed without deferred cleanup.
This is not yet long-duration or multi-vendor acceptance.

## Wire-evidence reporting model

Controlled vendor-neutral captures establish the following useful compatibility patterns:

```text
STATIC URCB
Read -> Resv=true -> RptEna=true -> readback -> async reports

STATIC BRCB
Read -> RptEna=true -> readback -> async reports

DYNAMIC URCB
Define+verify DataSet -> Read -> Resv=true
-> grouped Write(DatSet, IntgPd, TrgOps, OptFlds, RptEna-last)
-> inspect every WriteResult -> readback -> async reports

DYNAMIC BRCB
Define+verify DataSet -> Read
-> grouped Write(DatSet, IntgPd, TrgOps, OptFlds, RptEna-last)
-> inspect every WriteResult -> readback -> async reports
```

Post-write readback is authoritative because the effective server state may normalize requested
fields and may advance `ConfRev` without a direct client write.

For BRCB, observed reconnect behavior also showed configuration persistence, continued retained
report capture while the previous association was absent, fast retained-history replay after
re-enable, EntryID continuity across a SqNum reset, and replay interleaving with confirmed MMS
traffic. The detailed recovery contract is maintained in
[`docs/RCB_REPORTING_REFERENCE.md`](docs/RCB_REPORTING_REFERENCE.md).

## Critical bit-field invariant

`TrgOps` and `ReasonForInclusion` have a reserved leading significant bit. Standard report-reason
semantics begin at bit index 1, not bit index 0. In particular, observed wire evidence confirms:

```text
0x40 = data-change
0x20 = quality-change
0x10 = data-update
0x08 = integrity
0x04 = general interrogation
0x80 = reserved
```

Do not use current implementation constants as an interoperability oracle where the quick
reference marks a known correction item. Preserve raw BIT STRING evidence separately from
semantic names and add exact regression vectors before changing behavior.

## Phase 4C.1 live-model integration

Read-only discovery includes DataSet directory and RCB state evidence in `live-ied-model-v1`.
This inventory is descriptive only. The Phase 4C.1 parity and physical evidence runners do not
reserve, enable, disable, or otherwise mutate a report control block.

The C#↔C++ parity checker compares DataSet presence/member counts and report-control presence/
mode. Runtime report subscription acceptance remains separate from read-only model parity.

## Known correction / extension work

- correct `ReasonForInclusion` semantic bit indexing and add exact wire-vector tests;
- align static-BRCB emitted reason masks with the reserved-leading-bit mapping;
- extend static-BRCB scheduling so periodic integrity/GI and selective event capture can feed the same retained queue;
- complete client-side BRCB EntryID resume policy and live reconnect/replay acceptance;
- exercise purge and buffer-overflow recovery against controlled retained-history scenarios;
- prove a contended preferred RCB can be skipped safely for a second candidate before any mutation;
- define bounded reconnect and automatic resubscribe policy without hiding continuity gaps;
- complete long-duration multi-vendor report interoperability.

Existing bounded retained-slot/replay primitives are useful foundations and should not be rewritten casually while correcting these semantics.
