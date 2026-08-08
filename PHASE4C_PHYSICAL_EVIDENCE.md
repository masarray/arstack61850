# Phase 4C Physical Read-Only Evidence

This document records laboratory evidence that has actually been observed on a physical IEC 61850 IED. It is intentionally narrower than the implementation feature matrix: a capability is listed here only when a real device run has produced evidence.

## Device under test

- IED identity observed through MMS discovery: `OCR7SR12`.
- MMS endpoint used in the lab: `192.168.1.10:102` (private laboratory address).
- Association profile selected by arstack61850: `BalancedApTitle`.
- Tests in this document are read-only. They do not send MMS Write, control, RCB reservation, RptEna, GoEna, SvEna, GI, DataSet mutation, or file mutation requests.

## RCB contention probe

Command:

```powershell
.\build\Release\ariec61850_rcb_contention_probe.exe 192.168.1.10 102 --probe-count 3 --probe-delay-ms 1000 --cooldown-sec 60 --timeout-ms 30000
```

Observed result:

```text
Read-only RCB contention probe: endpoint=192.168.1.10:102, associationProfile=BalancedApTitle, associationAttempts=1, RCB=OCR7SR12CTRL/BI6GGIO1.urcbA01, probes=3, busy=false, flapping=false, contended=false, decision=StableProceed, cooldownSec=0.
  probe=1 RptEna=false Resv=false ResvTms=- DatSet=- ConfRev=1
  probe=2 RptEna=false Resv=false ResvTms=- DatSet=- ConfRev=1
  probe=3 RptEna=false Resv=false ResvTms=- DatSet=- ConfRev=1
Reason: RCB remained stable and free across pre-claim probes.
Recommended action: Safe to continue with guarded claim attempt.
```

Accepted conclusion: the selected URCB remained stable and free over three fresh read-only probes. This proves the `StableProceed` contention decision on this device for this observation window only; it does not prove that all RCBs on the IED are free or that a future claim will necessarily succeed.

## Setting Group Control Block deep read

Command:

```powershell
.\build\Release\ariec61850_control_block_read_probe.exe 192.168.1.10 102 --kind sg --max-control-blocks 4 --max-attributes 32 --timeout-ms 30000
```

Observed result:

```text
Read-only control-block value probe: endpoint=192.168.1.10:102, associationProfile=BalancedApTitle, associationAttempts=1, discovered=1.
CB OCR7SR12PROT/LLN0.SP.SGCB kind=SettingGroupControl attributes=5/5 status=Complete
  ActSG=1 [LLN0$SP$SGCB$ActSG]
  CnfEdit=false [LLN0$SP$SGCB$CnfEdit]
  EditSG=0 [LLN0$SP$SGCB$EditSG]
  LActTm=<utc-time> [LLN0$SP$SGCB$LActTm]
  NumOfSG=1 [LLN0$SP$SGCB$NumOfSG]
Control-block read summary: attempted=1, complete=1, partial=0, failed=0.
```

Accepted conclusion: the physical OCR7SR12 exposes one Setting Group Control Block at `OCR7SR12PROT/LLN0.SP.SGCB`, and all five exposed MMS attributes were read successfully in one bounded read-only run. The observed values were `ActSG=1`, `CnfEdit=false`, `EditSG=0`, `NumOfSG=1`, with `LActTm` decoded as IEC 61850 UTC time.

## Implementation consequence

The C++ live-model path now treats GO/SV/SG/LG deep-read values as runtime evidence rather than structural identity. The optional `--control-block-values` path can project successful, partial, failed, and bounded/not-read states without changing the structural fingerprint. Runtime attributes are exported as machine-readable attribute/value/status entries so applications do not need to parse the human message text.

## Still pending

- Integrated `ariec61850_live_discover --control-block-values` physical run on the same IED.
- Ten repeated primary-vendor discovery cycles with reconnect evidence.
- Deliberate timeout/reconnect test.
- Same-IED C# versus C++ model export comparison.
- Physical GOOSE/SV capture interoperability evidence.
- Any active RCB claim, reporting enable, control, or mutation test; these remain outside the read-only Phase 4C acceptance boundary.
