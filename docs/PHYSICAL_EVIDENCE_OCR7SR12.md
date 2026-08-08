# OCR7SR12 Physical Read-Only Interoperability Evidence

## Status

This document records physical evidence observed against the reachable `OCR7SR12` IED during
Phase 4C.1. It intentionally separates what has been physically demonstrated from what remains
pending. No Write, RCB reservation/enable, GI, control, dynamic DataSet mutation, or file-service
request is part of this evidence.

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
`CONTROL_BLOCK_VALUE_READ_PENDING` records a known implementation gap: GO/SV/SG/LG attribute
names are inventoried, while deep values such as DatSet, GoID, svID, APPID, and multicast address
are not yet read by that discovery path.

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

## Physically demonstrated

- TCP/TPKT/COTP/Session/Presentation/ACSE association to the real IED.
- `BalancedApTitle` accepted on the first association attempt by this IED.
- Live read-only MMS discovery of domains, variables, LD/LN/DO hierarchy, DataSet inventory,
  RCB inventory, and bounded RCB state reads.
- Ten bounded RCB reads completed 10/10 with zero read failures.
- Three repeated read-only contention probes remained stable and free on
  `OCR7SR12CTRL/BI6GGIO1.urcbA01`.

## Still pending before Phase 4C.1 physical acceptance is complete

- Primary-vendor ten-cycle fresh association/discovery run captured by the acceptance runner.
- Explicit slow/timeout failure followed by a clean reconnect.
- C# and C++ `live-ied-model-v1` exports against the same unchanged IED configuration and parity
  review.
- Packet-capture reference where practical.
- Multi-vendor or additional vendor-simulator evidence.
- Deep GO/SV/SG/LG control-block value reader.

Use `scripts/run-phase4c-physical-acceptance.py` or the Windows PowerShell wrapper to collect the
next repeated-association evidence without expanding the Phase 4C.1 read-only service boundary.
