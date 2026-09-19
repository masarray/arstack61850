# external IEC 61850 client External Acceptance R2

Date: 2026-09-17

This ledger records external external vendor external IEC 61850 client evidence separately from synthetic/CI evidence. It is intentionally conservative: capabilities are only marked accepted when observed with the real client.

## Locked external baseline

Tested Windows runtime lineage: `b8cfe4442e1c570de4158657599ba72962873bca` (runtime-identical CI-only follow-up `23130da7fbfb1d748dc63e26f8676bc549936a0d`).

Model: `Siprotec_084F06BCU_AA1E1F06R4.cid`
Endpoint: `192.168.81.103:102`

Observed with real external IEC 61850 client:

- MMS association/discovery succeeds.
- Reports hierarchy is visible in external IEC 61850 client.
- Application BRCBs `Unbuffer01`, `Unbuffer02`, `Buffer01`, and `Buffer02` are visible in the Reports workspace.
- MMS FileDirectory/Open/Read/Close succeeds against the configured ARStack file-service root.
- URCB enable/GI produces an InformationReport; simulator diagnostics recorded `LLN0$RP$Unbuffer01`, `SqNum 1`, reason `4`.

These surfaces are treated as retained regression requirements. Reporting fixes must not weaken discovery, report inventory, or file transfer.

## Original R2 reporting blocker captured on wire

Capture: `Arstack_Reporting.pcapng` supplied from the real-client session.

external IEC 61850 client performs the following BRCB edit sequence on `LLN0$BR$Buffer01`:

1. Disable `RptEna`.
2. Send one MMS Write containing two variables in order:
   - `TrgOps` as MMS BIT STRING, unused-bits `2`, value `0x7C`.
   - `RptEna = true`.
3. The pre-fix ARStack server returns Confirmed-Error instead of a two-result WriteResponse.

Root cause was twofold:

- Production simulator dispatch policy allowed only one variable per MMS Write.
- BRCB `TrgOps` was exposed read-only even though the URCB path already supported mutable trigger options.

## BRCB fix under external verification

Implementation head: `ab8768338e17fd630c9c464e3df9314ae3d177cf`

The fix:

- allows a bounded host-side MMS Write transaction of up to 16 variables (one complete BRCB attribute set),
- makes BRCB `TrgOps` mutable only while the report control is disabled,
- keeps BRCB configuration behind the existing association/Owner reservation gate,
- validates the six-bit Trigger Options BIT STRING and reserved bits,
- makes the runtime scheduler consume the mutable TrgOps value,
- preserves BRCB `IntgPd` when compiling the simulator manifest into the runtime definition,
- adds a hard-profile regression reproducing the real external IEC 61850 client `TrgOps=0x7C + RptEna=true` two-variable Write transaction.

The BRCB hard-profile regression passed before this source commit was pushed.

## P0 reporting hardening checkpoint

The external acceptance boundary below remains conservative, but protocol-level hardening is retained as regression requirements.

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

Focused regression coverage negotiates a 128-byte TPDU, requires multi-segment association and MMS responses, verifies every emitted TPKT remains inside the negotiated TPDU limit, and reassembles a segmented BRCB InformationReport before decoding it. This directly protects the field-capture case where external IEC 61850 client negotiates a bounded COTP TPDU and the server must not emit oversized single-frame responses.

A follow-up regression-only lifetime defect was corrected in `3e65ba19b6f8682640b711425e6f24ecef6ccfee`: the decoded `MmsInformationReportView` now keeps its segmented reassembly backing storage alive for the complete assertion scope instead of referencing a local buffer that had already gone out of scope. No production protocol behavior changed in that follow-up.

### P0.2/P0.3 exact-head synthetic closure

Synthetic/protocol acceptance for P0.2 and P0.3 closed on exact head `3e65ba19b6f8682640b711425e6f24ecef6ccfee`.

All 15 PR workflows attached to that exact head completed successfully, including MMS R1-R2 Server CI, external IEC 61850 client Parity Server CI, BRCB Hard Profile CI, Embedded Profile CI, Dynamic RCB Trial Harness CI, Control Interop Harness CI, C++ CI, IED Simulator Qt, and IED Simulator Release Hardening.

The exact-head Windows RC was produced by IED Simulator Release Hardening run `35205603573`:

- artifact: `arstack-iec61850-workbench-windows-rc`
- artifact ID: `10490192212`
- digest: `sha256:8e3533b7d681f504a3a50739f05252366740c86ede8903fbf4e5852835c3f1fa`
- contains `ARStack-IEC61850-Workbench-Setup-win64.exe`
- contains `ARStack-IEC61850-Workbench-portable-win64.zip`

These P0.2/P0.3 results are synthetic/protocol regression evidence, not a replacement for real-client acceptance.

## R4 real-client reporting evidence

Capture: `Arstack_DiscoveryIED_R4_rcb.pcapng` plus simulator diagnostics from the same real external vendor external IEC 61850 client session.

### BRCB result — externally accepted

`LLN0$BR$Buffer01` now enables and reports successfully both when external IEC 61850 client leaves Trigger Options / Optional Fields at their IED values and when the user modifies those fields in external IEC 61850 client. This closes the original BRCB write-interoperability symptom for the tested R4 session. BRCB behavior remains a retained regression requirement.

### URCB result — remaining OptFlds interoperability gap isolated

`LLN0$RP$Unbuffer01` enables and reports when Trigger Options / Optional Fields are left unchanged. The R4 diagnostics also show repeated `Unbuffer01` reports after enable, so the remaining failure is not the URCB report scheduler or InformationReport emission path.

When the user modifies Trigger Options / Optional Fields, external IEC 61850 client sends one grouped MMS Write containing, in order:

1. `LLN0$RP$Unbuffer01$IntgPd = 5000`,
2. `LLN0$RP$Unbuffer01$TrgOps` BIT STRING bytes `02 FC`,
3. `LLN0$RP$Unbuffer01$OptFlds` BIT STRING bytes `06 7B 80`,
4. `LLN0$RP$Unbuffer01$RptEna = true`.

The captured ARStack WriteResponse returns success for `IntgPd`, success for `TrgOps`, failure code `11` (`object-value-invalid`) for `OptFlds`, then success for `RptEna`. external IEC 61850 client surfaces that third result as `parameter-value-inconsistent`. Therefore this R4 failure is specifically the URCB `OptFlds=0x7B80` write, not `TrgOps` and not generic report enable.

The external IEC 61850 client value uses its generic RCB Options editor and includes first-octet bits for `BufferOverflow` and `EntryID`, which have meaning for BRCB but are not emitted by URCB reports. ARStack's strict URCB validator previously accepted only first-octet mask `0x7C`, so `0x7B80` was rejected.

### P0.4 — URCB generic OptFlds normalization

Implementation commit: `2361809ab319a7a57fd23100d0e1cb7b96972cb2`.

The URCB runtime now:

- accepts client first-octet Optional Fields through mask `0x7F`, so external IEC 61850 client's generic `0x7B80` request is interoperable,
- normalizes the effective URCB first octet through `0x7C`, therefore captured `0x7B80` becomes effective URCB `0x7880`,
- continues to reject the unsupported segmentation bit in the second octet,
- never advertises or encodes BRCB-only `BufferOverflow` / `EntryID` metadata in an URCB InformationReport,
- applies the same normalization during initialization and later OptFlds writes.

Focused host URCB regression, embedded URCB hard-profile regression, and the production external IEC 61850 client server build all passed in bootstrap run `35219711196` before the source commit was pushed. Temporary bootstrap files were removed after that proof.

P0.4 remains externally open until a new exact-head Windows build proves the modified URCB path in real external IEC 61850 client.

## External closure condition

R2/R4 reporting remains open until the new P0.4 exact-head Windows artifact is tested with real external IEC 61850 client and proves:

- modified `LLN0$RP$Unbuffer01` Trigger Options / Optional Fields no longer produce `parameter-value-inconsistent`,
- the grouped URCB write and subsequent `RptEna=true` result in an enabled URCB,
- changed trigger selection actually controls emitted URCB reports,
- GI/report delivery remains functional,
- the already-accepted BRCB modified Trigger Options / Optional Fields path does not regress,
- segmented responses remain transparent to external IEC 61850 client under its negotiated COTP TPDU size,
- the locked discovery/report-inventory/file-transfer baseline still passes.

Do not close Issue #95 or merge PR #82 solely from synthetic CI evidence.
