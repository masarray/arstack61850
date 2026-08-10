# Dynamic RCB Live Trial

This harness is for an isolated IEC 61850 laboratory IED/simulator. It intentionally does not share or modify the Smart Control branch.

## Safety model

Default execution is read-only. It performs live MMS association/discovery, probes RCB state, ranks only empty dynamic slots, and selects scalar ST/MX members. No mutation service is sent unless the exact arm token is present.

An armed run performs one bounded lifecycle with no automatic mutation retry:

1. discover and rank empty dynamic RCB candidates;
2. choose one RCB with empty `DatSet`, `RptEna=false`, and no active reservation;
3. create one uniquely named domain-specific Dynamic DataSet;
4. verify exact member count and order with `GetNamedVariableListAttributes`;
5. bind the selected RCB `DatSet`;
6. reserve URCB when required, enable `RptEna`, and request GI when exposed;
7. use bounded confirmed Read cycles to give asynchronous reports an opportunity to enter the association queue;
8. decode/drain queued reports through the normal reporting runtime;
9. disable the RCB and release only reservations acquired by this runtime;
10. set `DatSet` back to empty;
11. delete only the Dynamic DataSet created by this run.

Cleanup traffic is allowed after a failed mutation, but the user action itself is never retried on another RCB automatically.

## Windows build

```powershell
pwsh -ExecutionPolicy Bypass -File .\scripts\build_dynamic_rcb_trial.ps1
```

## First run: read-only plan

```powershell
.\build-dynamic-rcb-trial\Release\ariec61850_dynamic_rcb_trial.exe 192.168.1.10 --no-urcb-fallback --auto-members 4
```

Depending on the generator, the executable can also be directly under `build-dynamic-rcb-trial`.

Expected output includes `RCB selection`, one `Selected` candidate, `SMART_DYNAMIC_RCB_PLAN`, and `READ_ONLY_PLAN_OK`.

## Armed trial

After reviewing the selected RCB and member list:

```powershell
.\build-dynamic-rcb-trial\Release\ariec61850_dynamic_rcb_trial.exe 192.168.1.10 --no-urcb-fallback --auto-members 4 --arm IEC61850-LAB-DYNAMIC-RCB
```

Success evidence is:

- `DEFINE_OK`
- `RCB_ENABLE_OK`
- one or more `CONFIRM_READ` lines
- `REPORT_EVIDENCE`
- `RCB_DISABLE_OK`
- `RCB_UNBIND_OK`
- `DELETE_OK`
- `SMART_DYNAMIC_RCB_TRIAL_PASS`

If the lifecycle is accepted but no GI report arrives during the bounded confirmation window, the harness returns `TRIAL_LIFECYCLE_PASS_REPORT_PENDING` with exit code 4. That means the mutation/cleanup path worked but report reception still needs a longer or targeted evidence run.

## Targeting a specific logical device or RCB

```powershell
... 192.168.1.10 --rcb-ld LD0 --no-urcb-fallback --auto-members 4
```

```powershell
... 192.168.1.10 --preferred-rcb "LD0/LLN0.brcbA01" --no-urcb-fallback --auto-members 4
```

Use the exact reference printed by the read-only planner. Explicit DataSet members can be supplied with repeated `--member LD/LN$FC$DO$DA...` arguments.

## Capture filter

For retained physical evidence, capture MMS/TCP traffic to the IED and retain the PCAP/PCAPNG together with terminal output. A useful Wireshark display filter for this target is:

```text
ip.addr == 192.168.1.10 && tcp.port == 102
```

Do not run the armed mode against an operational substation IED.
