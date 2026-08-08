# OCR7SR12 Physical Read-Only Interoperability Evidence

## Status

This document records physical evidence observed against the reachable `OCR7SR12` IED during
Phase 4C.1. It intentionally separates what has been physically demonstrated from what remains
pending. No Write, RCB reservation/enable, GI, control, dynamic DataSet mutation, setting-group
mutation, or file-service request is part of this evidence.

## Target

- IED identity reported by live MMS discovery: `OCR7SR12`
- MMS endpoint used during the recorded runs: `192.168.1.10:102`
- C++ branch: `agent/phase-4c-tcp-live-discovery`
- Physical evidence date: 2026-08-08

## Association and bounded discovery evidence

A bounded live discovery run completed with:

```text
associationProfile=BalancedApTitle
associationAttempts=1
domains=4
variables=9067
DataSets=1
RCBs=286
rcbReads=10/10
diagnostics=0
```

The resulting structural inventory included:

```text
LD=4
LN=123
DO=1186
RCB=286
SGCB=1
RCBReadFailed=0
```

The run used `--no-types --no-datasets --max-rcb 10`; therefore only ten RCB states were read.
All ten read RCBs were observed as `Unbound`, while the remaining 276 were intentionally
`NotRead`. This evidence must not be represented as 286/286 RCBs being unbound.

`DATASET_MEMBERS_NOT_READ` was expected because DataSet directory reads were explicitly disabled.
At the time of this earlier run, `CONTROL_BLOCK_VALUE_READ_PENDING` correctly recorded that the
main `live_discover` path had only inventoried GO/SV/SG/LG attribute names. The standalone deep
reader has since been implemented and physically proven for SGCB; the current integrated
`--control-block-values` path still requires one physical regression run after that wiring change.

## Read-only RCB contention evidence

A three-probe contention run against the first discovered URCB completed as:

```text
associationProfile=BalancedApTitle
associationAttempts=1
RCB=OCR7SR12CTRL/BI6GGIO1.urcbA01
probes=3
busy=false
flapping=false
contended=false
decision=StableProceed
cooldownSec=0
```

All three observations were stable:

```text
RptEna=false
Resv=false
ResvTms=-
DatSet=-
ConfRev=1
```

This physically demonstrates the C#-parity pre-claim contention evaluator on this RCB. The tool
performed discovery plus repeated MMS Read requests only. `StableProceed` is evidence that the
selected RCB remained free and stable during the observation window; it is **not** authorization
to perform a claim/write in Phase 4C.1.

## Read-only SGCB deep-value evidence

The standalone bounded control-block reader was run against the real IED with:

```text
ariec61850_control_block_read_probe.exe 192.168.1.10 102 \
  --kind sg --max-control-blocks 4 --max-attributes 32 --timeout-ms 30000
```

The IED exposed exactly one matching Setting Group Control Block and all five discovered MMS
attributes were read successfully:

```text
associationProfile=BalancedApTitle
associationAttempts=1
discovered=1
CB OCR7SR12PROT/LLN0.SP.SGCB kind=SettingGroupControl attributes=5/5 status=Complete
  ActSG=1 [LLN0$SP$SGCB$ActSG]
  CnfEdit=false [LLN0$SP$SGCB$CnfEdit]
  EditSG=0 [LLN0$SP$SGCB$EditSG]
  LActTm=<utc-time> [LLN0$SP$SGCB$LActTm]
  NumOfSG=1 [LLN0$SP$SGCB$NumOfSG]
Control-block read summary: attempted=1, complete=1, partial=0, failed=0.
```

This is physical evidence that exact live NameList-derived SGCB leaf identifiers can be read by
MMS without any setting-group mutation. It proves read support for this specific SGCB on this IED;
it does not prove that all vendors expose the same attribute set or that GO/SV address parameters
such as APPID/MAC/VLAN are universally MMS-readable.

The current C++ integration keeps these mutable values as runtime evidence overlay. Structural
control-block inventory and canonical model fingerprint remain independent of changing values such
as `ActSG`, `EditSG`, or activation time.

## Physically demonstrated

- TCP/TPKT/COTP/Session/Presentation/ACSE association to the real IED.
- `BalancedApTitle` accepted on the first association attempt by this IED.
- Live read-only MMS discovery of domains, variables, LD/LN/DO hierarchy, DataSet inventory,
  RCB inventory, and bounded RCB state reads.
- Ten bounded RCB reads completed 10/10 with zero read failures.
- Three repeated read-only contention probes remained stable and free on
  `OCR7SR12CTRL/BI6GGIO1.urcbA01`.
- SGCB deep-value read on `OCR7SR12PROT/LLN0.SP.SGCB` completed 5/5 with zero partial/failure
  attributes.

## Still pending before Phase 4C.1 physical acceptance is complete

- Physical regression of the newly integrated `live_discover --control-block-values` path on the
  same OCR7SR12, confirming runtime projection reports `CBValueComplete=1` for the SGCB-only
  bounded run.
- Primary-vendor ten-cycle fresh association/discovery run captured by the acceptance runner.
- Explicit slow/timeout failure followed by a clean reconnect.
- C# and C++ `live-ied-model-v1` exports against the same unchanged IED configuration and parity
  review.
- Packet-capture reference where practical.
- Multi-vendor or additional vendor-simulator evidence.
- Physical GO/SV/LG deep-value evidence when a target IED actually exposes those control blocks;
  SCL-only APPID/MAC/VLAN values must not be invented as MMS evidence.

Use `scripts/run-phase4c-physical-acceptance.py` or the Windows PowerShell wrapper to collect the
next repeated-association evidence without expanding the Phase 4C.1 read-only service boundary.
