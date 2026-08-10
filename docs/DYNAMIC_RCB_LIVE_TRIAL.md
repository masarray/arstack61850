# Dynamic RCB Live Trial

This harness is for an isolated IEC 61850 laboratory IED/simulator. It intentionally does not share or modify the Smart Control branch.

## Safety model

Default execution is read-only. It performs live MMS association/discovery,
ranks only empty dynamic slots, and repeats bounded pre-claim Read probes on the
selected candidate. If that candidate is busy or changes state between probes,
it is excluded from the current command and the next safe candidate is ranked.
No mutation service is sent unless the exact arm token is present.

An armed run performs one bounded lifecycle with no automatic mutation retry:

1. discover and rank empty dynamic RCB candidates;
2. repeatedly probe the selected RCB immediately before mutation;
3. skip busy/flapping candidates, with a hard candidate-attempt bound;
4. choose one stable RCB with empty `DatSet`, `RptEna=false`, and no active reservation;
5. create one uniquely named domain-specific Dynamic DataSet;
6. verify exact member count and order with `GetNamedVariableListAttributes`;
7. bind the selected RCB `DatSet`;
8. reserve URCB when required, enable `RptEna`, and request GI when exposed;
9. use bounded confirmed Read cycles to give asynchronous reports an opportunity to enter the association queue;
10. decode/drain queued reports through the normal reporting runtime;
11. disable the RCB and release only reservations acquired by this runtime;
12. set `DatSet` back to empty;
13. delete only the Dynamic DataSet created by this run.

Automatic members are projected from the bounded Logical-Node
`GetVariableAccessAttributes` type trees. The planner walks nested FC/DO/DA
structures, keeps only scalar `ST`/`MX` leaves in the selected RCB domain, and
prefers primary values such as `stVal`, `general`, and measurement magnitude
leaves. Arrays, structures, unknown types, and other domains are excluded.
The canonical `LD/LN.DataSet` reference used for directory services is
converted to the IEC 61850 RCB attribute value `LD/LN$DataSet` before the
`DatSet` write.
Incoming reports accept both `listOfVariable [0]` and the
`variableListName [1]` form used by IEDScout, while still decoding only the
trailing `listOfAccessResult` as report data.

Cleanup traffic is allowed after a failed mutation, but the user action itself
is never retried on another RCB automatically. Smart failover applies only to
the read-only pre-claim window, where no remote state has been changed.

## Windows build

```powershell
pwsh -ExecutionPolicy Bypass -File .\scripts\build_dynamic_rcb_trial.ps1
```

Windows PowerShell 5.1 can run the same script when `pwsh` is not installed:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\build_dynamic_rcb_trial.ps1
```

## First run: read-only plan

```powershell
.\build-dynamic-rcb-trial\Release\ariec61850_dynamic_rcb_trial.exe 192.168.1.10 --no-urcb-fallback --auto-members 4
```

Depending on the generator, the executable can also be directly under `build-dynamic-rcb-trial`.

Expected output includes `RCB selection`, `PRECLAIM_ATTEMPT`, one stable
candidate, `SMART_DYNAMIC_RCB_PLAN`, and `READ_ONLY_PLAN_OK`. When a candidate
is contended, `RCB_FAILOVER_SKIP` records the reason before another candidate is
ranked.

The defaults probe up to four candidates, with three Read samples per candidate
and 250 ms between samples. They can be bounded explicitly:

```powershell
... --max-claim-candidates 4 --preclaim-probes 3 --preclaim-delay-ms 250 --contention-cooldown 60
```

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
