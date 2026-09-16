# IEDScout External Acceptance R2

Date: 2026-09-16

This ledger records external OMICRON IEDScout evidence separately from synthetic/CI evidence. It is intentionally conservative: capabilities are only marked accepted when observed with the real client.

## Locked external baseline

Tested Windows runtime lineage: `b8cfe4442e1c570de4158657599ba72962873bca` (runtime-identical CI-only follow-up `23130da7fbfb1d748dc63e26f8676bc549936a0d`).

Model: `Siprotec_084F06BCU_AA1E1F06R4.cid`
Endpoint: `192.168.81.103:102`

Observed with real IEDScout:

- MMS association/discovery succeeds.
- Reports hierarchy is visible in IEDScout.
- Application BRCBs `Unbuffer01`, `Unbuffer02`, `Buffer01`, and `Buffer02` are visible in the Reports workspace.
- MMS FileDirectory/Open/Read/Close succeeds against the configured ARStack file-service root.
- URCB enable/GI produces an InformationReport; simulator diagnostics recorded `LLN0$RP$Unbuffer01`, `SqNum 1`, reason `4`.

These surfaces are treated as retained regression requirements. Reporting fixes must not weaken discovery, report inventory, or file transfer.

## Remaining R2 reporting blocker captured on wire

Capture: `Arstack_Reporting.pcapng` supplied from the real-client session.

IEDScout performs the following BRCB edit sequence on `LLN0$BR$Buffer01`:

1. Disable `RptEna`.
2. Send one MMS Write containing two variables in order:
   - `TrgOps` as MMS BIT STRING, unused-bits `2`, value `0x7C`.
   - `RptEna = true`.
3. The pre-fix ARStack server returns Confirmed-Error instead of a two-result WriteResponse.

Root cause was twofold:

- Production simulator dispatch policy allowed only one variable per MMS Write.
- BRCB `TrgOps` was exposed read-only even though the URCB path already supported mutable trigger options.

## Fix under verification

Implementation head: `ab8768338e17fd630c9c464e3df9314ae3d177cf`

The fix:

- allows a bounded host-side MMS Write transaction of up to 16 variables (one complete BRCB attribute set),
- makes BRCB `TrgOps` mutable only while the report control is disabled,
- keeps BRCB configuration behind the existing association/Owner reservation gate,
- validates the six-bit Trigger Options BIT STRING and reserved bits,
- makes the runtime scheduler consume the mutable TrgOps value,
- preserves BRCB `IntgPd` when compiling the simulator manifest into the runtime definition,
- adds a hard-profile regression reproducing the real IEDScout `TrgOps=0x7C + RptEna=true` two-variable Write transaction.

The BRCB hard-profile regression passed before this source commit was pushed.

## External closure condition

R2 reporting remains open until a new Windows artifact from this fix is tested with real IEDScout and proves:

- editing Trigger Options returns no IEDScout error,
- the subsequent `RptEna=true` succeeds,
- changed trigger selection actually controls emitted reports,
- GI/report delivery remains functional,
- the locked discovery/report-inventory/file-transfer baseline still passes.

Do not close Issue #95 or merge PR #82 solely from synthetic CI evidence.
