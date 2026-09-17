# IEDScout External Acceptance R2

Date: 2026-09-17

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

## P0 reporting hardening checkpoint

The external acceptance boundary above is unchanged, but two additional protocol-level hardening items are now implemented on the PR branch and retained as regression requirements.

### P0.2 — canonical InformationReport payload order

Implementation commit: `6a7dfec7b9cb82460169eb2188a4e384835e104f`.

The owning report mapper now consumes the IEC 61850 report access-result groups in canonical order after the inclusion bitstring:

1. `DataRef*` when selected by `OptFlds`,
2. process `Value*`,
3. `ReasonForInclusion*` when selected.

Regression coverage includes all four DataRef/Reason combinations and rejects the former false-green `Value-before-DataRef` fixture layout. The selective BRCB encoder regression separately locks the same wire order. The runtime client fixture was subsequently aligned in `bff75e60694ccf4997223016683863d760fa7d98` so retained end-to-end tests no longer encode the legacy ordering.

### P0.3 — negotiated COTP TPDU segmentation

Implementation commit: `e1937d90c2c57bdb53f840845cce3746e22a62af`.

Outbound server TSDUs are now segmented into one or more complete TPKT/COTP Data frames according to the negotiated COTP TPDU-size parameter. Intermediate Data TPDUs clear EOT and only the final segment sets EOT. The implementation uses bounded span-based encoding and does not introduce a hidden heap-backed transport queue.

Focused regression coverage negotiates a 128-byte TPDU, requires multi-segment association and MMS responses, verifies every emitted TPKT remains inside the negotiated TPDU limit, and reassembles a segmented BRCB InformationReport before decoding it. This directly protects the field-capture case where IEDScout negotiates a bounded COTP TPDU and the server must not emit oversized single-frame responses.

A follow-up regression-only lifetime defect was corrected in `3e65ba19b6f8682640b711425e6f24ecef6ccfee`: the decoded `MmsInformationReportView` now keeps its segmented reassembly backing storage alive for the complete assertion scope instead of referencing a local buffer that had already gone out of scope. No production protocol behavior changed in that follow-up.

### Exact-head synthetic closure

Synthetic/protocol acceptance for P0.2 and P0.3 is now closed on exact head `3e65ba19b6f8682640b711425e6f24ecef6ccfee`.

All 15 PR workflows attached to that exact head completed successfully, including the protocol and product surfaces most relevant to this correction:

- MMS R1-R2 Server CI
- IEDScout Parity Server CI
- BRCB Hard Profile CI
- Embedded Profile CI
- Dynamic RCB Trial Harness CI
- Control Interop Harness CI
- C++ CI
- IED Simulator Qt
- IED Simulator Release Hardening

The exact-head Windows RC was produced by IED Simulator Release Hardening run `35205603573`:

- artifact: `arstack-iec61850-workbench-windows-rc`
- artifact ID: `10490192212`
- digest: `sha256:8e3533b7d681f504a3a50739f05252366740c86ede8903fbf4e5852835c3f1fa`
- contains `ARStack-IEC61850-Workbench-Setup-win64.exe`
- contains `ARStack-IEC61850-Workbench-portable-win64.zip`

These P0.2/P0.3 results are synthetic/protocol regression evidence, not a replacement for the real IEDScout closure condition below.

## External closure condition

R2 reporting remains open until the exact-head Windows artifact above is tested with real IEDScout and proves:

- editing Trigger Options returns no IEDScout error,
- the subsequent `RptEna=true` succeeds,
- changed trigger selection actually controls emitted reports,
- GI/report delivery remains functional,
- segmented responses remain transparent to IEDScout under its negotiated COTP TPDU size,
- the locked discovery/report-inventory/file-transfer baseline still passes.

Do not close Issue #95 or merge PR #82 solely from synthetic CI evidence.
