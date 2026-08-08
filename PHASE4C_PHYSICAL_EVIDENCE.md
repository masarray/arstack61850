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

## Integrated live-discovery control-block values

The integrated `ariec61850_live_discover --control-block-values` path was subsequently run against the same OCR7SR12. The run reported `diagnostics=0`, `CBValueComplete=1`, `CBValuePartial=0`, `CBValueFailed=0`, and `CBValueNotRead=0`. The same SGCB was projected into the live model with all five attributes complete. The prior pending-value warning was no longer emitted; the only warning in the bounded run was the intentional `DATASET_MEMBERS_NOT_READ` warning caused by `--no-datasets`.

Accepted conclusion: deep-read evidence is not limited to the standalone probe. The integrated discovery -> runtime overlay -> human/JSON projection path has been physically observed on the target IED.

## Same-IED C# versus C++ parity

A same-device OCR7SR12 comparison using the C# repository as the behavioral oracle was accepted with zero blocking structural/type/runtime findings for the compared evidence profile. This establishes the accepted parity slice only; it is not a claim of complete C# feature parity.

## Ten-cycle primary-vendor acceptance

The bounded Phase 4C physical acceptance runner was executed for ten full discovery cycles followed by three contention cycles. Discovery used full RCB reading (`--max-rcb 286 --require-rcb-complete`), bounded integrated control-block values (`--require-control-block-complete`), and same-IED C# parity checks. DataSet member directories were intentionally skipped with `--no-datasets`, so one expected warning was allowed per discovery cycle.

Observed discovery result across cycles 1 through 10:

```text
success=True
structuralFingerprint=934b555dff76a46f
runtimeSnapshotFingerprint=48a836ad2d39b72e
rcbComplete=True
cbComplete=True
warnings=1
```

All ten cycles reported the same structural and runtime fingerprints. Final discovery acceptance:

```text
Read-only interoperability acceptance: PASS
(structuralStable=True, runtimeStable=True, rcbComplete=True, controlBlockComplete=True)
```

The three subsequent contention cycles all used `BalancedApTitle`, required one association attempt, reported `contended=False`, and returned `decision=StableProceed`.

Final Phase 4C result:

```text
Phase 4C physical read-only acceptance: PASS
(discovery=True, contention=True, controlBlocks=True, freshAssociations=13/13)
```

Accepted conclusion: on this OCR7SR12 observation window, arstack61850 completed ten fresh stable discovery associations with full 286/286 RCB read completeness and complete control-block evidence, followed by three successful fresh contention associations. This is evidence for repeated association/discovery stability on this target; it is not timeout-recovery or multi-vendor evidence.

## Implementation consequence

The C++ live-model path treats GO/SV/SG/LG deep-read values as runtime evidence rather than structural identity. The optional `--control-block-values` path can project successful, partial, failed, and bounded/not-read states without changing the structural fingerprint. Runtime attributes are exported as machine-readable attribute/value/status entries so applications do not need to parse human message text.

A dedicated controlled timeout/recovery evidence runner is now implemented separately. It establishes a healthy baseline, injects a post-association server-response stall through a local TCP proxy, requires the client-side MMS request deadline to expire, then performs a fresh direct recovery association and compares the recovered structural fingerprint with the baseline. This runner is not counted as physical evidence until it is executed successfully against the IED.

## Still pending

- Controlled timeout/recovery physical acceptance on the OCR7SR12.
- Pagination continuation evidence on a target response that actually requires more than one page.
- Physical GOOSE/SV capture interoperability evidence.
- Any active RCB claim, reporting enable, control, or mutation test; these remain outside the read-only Phase 4C acceptance boundary.
