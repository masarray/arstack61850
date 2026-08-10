# IEC 61850 Control Interoperability Acceptance Record

## Scope

This record captures the Phase 4D-C5 live simulator interoperability evidence gathered on 2026-08-11 against an isolated IEC 61850 vendor simulator.

It documents observed interoperability of the guarded C++ control path. It is not an IEC 61850 conformance certificate and is not evidence of physical primary-plant or production-relay control.

## Tested profile

- Endpoint: `192.168.1.10:102`
- Simulator model/profile: `BCU7SL`
- Control object: `BCU7SLCTRL/CSWI1.Pos`
- Discovered CDC: `SPC`
- Live `ctlVal` TypeSpecification: `boolean`
- Status object: `BCU7SLCTRL/CSWI1$ST$Pos$stVal`
- `sboTimeout`: 30000 ms
- `operTimeout`: 1000 ms
- Origin used by the harness: `ARSTACK-C5`, station control
- Interlock-check: enabled
- Synchro-check: enabled
- Harness safety gate: exact `--arm IEC61850-LAB-CONTROL`
- Automatic command retry: disabled

The live descriptor discovery also confirmed exact named structures for `Oper`, `SBOw` where applicable, and `Cancel`. The control path followed the live TypeSpecification instead of guessing a positional layout.

## Acceptance matrix

| ctlModel | Model | Tested path | Result | Observed command Writes |
|---|---|---|---|---|
| 1 | Direct with normal security | OFF and ON | PASS | exactly one `Oper` per deliberate action |
| 2 | SBO with normal security | Select/Operate OFF, Select/Operate ON, Select/Cancel | PASS | exactly one `Oper` per operate action; exactly one `Cancel` for cancel |
| 3 | Direct with enhanced security | OFF and ON | PASS | exactly one `Oper` per deliberate action |
| 4 | SBO with enhanced security | SBOw/Operate OFF and ON | PASS | exactly `SBOw`, then `Oper` per successful action |

Overall tested-profile state: **SIMULATOR_INTEROP_PASSED**.

## Direct normal — ctlModel 1

Raw discovery reported `numeric=1`, interpreted as `direct-normal`.

### OFF

- status before: `0x0680`
- status after: `0x0640`
- completion: `accepted`
- accepted: `true`
- termination: `false`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

### ON

- status before: `0x0640`
- status after: `0x0680`
- completion: `accepted`
- accepted: `true`
- termination: `false`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

This matches the normal-security completion boundary: successful MMS Oper service acceptance completes the command; CommandTermination is not required.

Local evidence filenames produced by the harness:

- `bcu7sl-direct-normal-off.json`
- `bcu7sl-direct-normal-on.json`

## SBO normal — ctlModel 2

Raw discovery reported `numeric=2`, interpreted as `sbo-normal`.

The model required Select and did not require CommandTermination.

### Select / Operate OFF

- completion: `accepted`
- accepted: `true`
- termination: `false`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

### Select / Operate ON

- completion: `accepted`
- accepted: `true`
- termination: `false`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

For this simulator, status could converge shortly after the normal-security MMS completion boundary. A subsequent deliberate command observed the expected new state. The harness therefore does not redefine normal-security completion as a status-change wait.

### Select / Cancel

- completion: `accepted`
- accepted: `true`
- termination: `false`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Cancel`
- no `Oper` Write followed

The SBO normal Select service is a Read of `SBO`, so the read is intentionally not counted in `mmsControlWriteCount`.

Local evidence filenames produced by the harness:

- `bcu7sl-sbo-normal-off.json`
- `bcu7sl-sbo-normal-on.json`
- `bcu7sl-sbo-normal-cancel.json`

## Direct enhanced — ctlModel 3

Raw discovery reported `numeric=3`, interpreted as `direct-enhanced`.

The descriptor reported `requiresSelect=false`, `enhanced=true`, and `commandTermination=true`.

### OFF

- status before: `0x0680`
- status after: `0x0640`
- completion: `positive-termination`
- accepted: `true`
- termination: `true`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

### ON

- status before: `0x0640`
- status after: `0x0680`
- completion: `positive-termination`
- accepted: `true`
- termination: `true`
- command Writes: 1
- Write: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

Both directions completed only after a positive correlated CommandTermination. MMS Write acceptance alone was not treated as enhanced-security completion.

Local evidence filenames produced by the harness:

- `bcu7sl-direct-enhanced-off.json`
- `bcu7sl-direct-enhanced-on.json`

## SBO enhanced — ctlModel 4

Raw discovery reported `numeric=4`, interpreted as `sbo-enhanced`.

The descriptor reported `requiresSelect=true`, `enhanced=true`, and `commandTermination=true`.

Live TypeSpecification discovery reported the named structures:

```text
Oper=structure(ctlVal:boolean,origin:structure(orCat:integer,orIdent:octet-string),ctlNum:unsigned,T:utc-time,Test:boolean,Check:bit-string)
SBOw=structure(ctlVal:boolean,origin:structure(orCat:integer,orIdent:octet-string),ctlNum:unsigned,T:utc-time,Test:boolean,Check:bit-string)
Cancel=structure(ctlVal:boolean,origin:structure(orCat:integer,orIdent:octet-string),ctlNum:unsigned,T:utc-time,Test:boolean)
```

### OFF

- status before: `0x0680`
- status after: `0x0640`
- completion: `positive-termination`
- accepted: `true`
- termination: `true`
- command Writes: 2
- Write 1: `BCU7SLCTRL/CSWI1$CO$Pos$SBOw`
- Write 2: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

### ON after OFF

- status before: `0x0640`
- status after: `0x0680`
- completion: `positive-termination`
- accepted: `true`
- termination: `true`
- command Writes: 2
- Write 1: `BCU7SLCTRL/CSWI1$CO$Pos$SBOw`
- Write 2: `BCU7SLCTRL/CSWI1$CO$Pos$Oper`

### Safe rejection / no-retry evidence

An earlier SBOw attempt was rejected by the simulator before Oper. The harness recorded:

- completion: `rejected`
- accepted: `false`
- command Writes: 1
- only Write: `BCU7SLCTRL/CSWI1$CO$Pos$SBOw`
- no `Oper` Write followed
- no automatic retry followed

The exact application-side reason was not established by retained LastApplError/PCAP evidence, so this record does not assign a specific rejection cause.

Local evidence filenames produced by the harness:

- `bcu7sl-sbo-enhanced-off.json`
- `bcu7sl-sbo-enhanced-on-after-off.json`
- `bcu7sl-sbo-enhanced-on.json` (rejected SBOw case before the successful opposite-direction sequence)

## Type-safety evidence

Before the successful SPC tests, an intentionally incorrect `--value-kind dpc` attempt was rejected locally because the live `ctlVal` TypeSpecification was Boolean:

```text
Control value does not match live ctlVal TypeSpecification at ctlVal.
```

No valid control sequence was sent for the mismatched value type. This confirms the live TypeSpecification gate prevents the client from guessing a DPC encoding for an SPC/Boolean command.

## No-retry evidence

Across the accepted cases, the harness reported exact command Write counts matching the expected control model:

- Direct normal: 1 `Oper`
- SBO normal Operate: 1 `Oper` after Select Read
- SBO normal Cancel: 1 `Cancel` after Select Read
- Direct enhanced: 1 `Oper`
- SBO enhanced: 1 `SBOw` + 1 `Oper`

The rejected SBO enhanced selection stopped after the single rejected `SBOw`. No automatic second selection or Oper was generated.

## Claim boundary

This evidence is sufficient to label this specific tested simulator profile:

`SIMULATOR_INTEROP_PASSED`

It does **not** establish:

- IEC 61850 conformance certification;
- interoperability with every vendor implementation;
- physical IED / relay acceptance;
- energized primary-plant control suitability;
- negative LastApplError/AddCause coverage for every rejection mode;
- association-loss coverage for every control phase.

Those remain separate acceptance gates.

## Retention note

The JSON filenames above were generated on the operator workstation. For audit-grade retention, pair each JSON with its matching PCAP/PCAPNG and simulator/event evidence according to `docs/CONTROL_INTEROP_RUNBOOK.md`. The JSON `mmsControlWriteCount` / `mmsControlWrites` values must agree with the packet capture before upgrading any physical-IED claim.
