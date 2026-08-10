# IEC 61850 Reporting Profile

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

## Phase 4C.1 live-model integration

Read-only discovery includes DataSet directory and RCB state evidence in `live-ied-model-v1`.
This inventory is descriptive only. The Phase 4C.1 parity and physical evidence runners do not
reserve, enable, disable, or otherwise mutate a report control block.

The C#↔C++ parity checker compares DataSet presence/member counts and report-control presence/
mode. Runtime report subscription acceptance remains separate from read-only model parity.

## Pending reporting work

- BRCB EntryID resume and replay;
- purge policy and buffer-overflow recovery;
- live proof where a contended preferred RCB is skipped for a second candidate;
- reconnect and automatic resubscribe policy;
- long-duration multi-vendor report interoperability.
