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

## Phase 4C.1 live-model integration

Read-only discovery includes DataSet directory and RCB state evidence in `live-ied-model-v1`.
This inventory is descriptive only. The Phase 4C.1 parity and physical evidence runners do not
reserve, enable, disable, or otherwise mutate a report control block.

The C#↔C++ parity checker compares DataSet presence/member counts and report-control presence/
mode. Runtime report subscription acceptance remains separate from read-only model parity.

## Pending reporting work

- BRCB EntryID resume and replay;
- purge policy and buffer-overflow recovery;
- dynamic DataSet lifecycle;
- long-duration multi-vendor report interoperability.
