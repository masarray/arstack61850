# Static DataSet / RCB Live Trial

`ariec61850_static_rcb_trial` validates the client-side static reporting path
without changing the IED's DataSet model. It discovers populated DataSet
directories and their bound RCBs, applies Smart RCB ranking, then performs
bounded repeated pre-claim Reads immediately before any optional activation.

## Safety boundary

The default command is read-only. It sends association, discovery, DataSet
directory, RCB-state, and contention-probe Reads only. Armed execution requires
the exact token `IEC61850-LAB-STATIC-RCB`.

An armed trial may reserve a selected free URCB, write `RptEna=true`, and issue
`GI=true`. Shutdown writes only the inverse state touched by that runtime:
`RptEna=false` and, for a runtime-reserved URCB, `Resv=false`.

The static trial never:

- writes the RCB `DatSet` attribute;
- defines or deletes a DataSet;
- disables or takes over an RCB observed as busy;
- switches to another RCB after any mutation begins.

If a candidate becomes enabled, reserved, or flaps during the pre-claim window,
the command excludes it and reranks another eligible static candidate within a
hard attempt bound.

## Build

```powershell
.\scripts\build_dynamic_rcb_trial.ps1 -BuildDir build-static-rcb-trial -Configuration Release
```

The shared harness builds both the dynamic and static reporting executables and
runs their help smoke tests plus the dynamic member-planner regression.

## Read-only planning

```powershell
.\build-static-rcb-trial\Release\ariec61850_static_rcb_trial.exe <ied-host> --no-urcb-fallback
```

Success ends with `READ_ONLY_STATIC_PLAN_OK` and prints the selected RCB, mode,
bound DataSet, member count, and every bounded pre-claim attempt.

Use `--dataset-ref <reference>` to require a particular static DataSet or
`--preferred-rcb <reference>` to rank one RCB first. A preferred RCB is not an
ownership override: a busy preferred candidate is still skipped.

## Guarded subscription

Run only against an authorized lab IED:

```powershell
.\build-static-rcb-trial\Release\ariec61850_static_rcb_trial.exe <ied-host> --no-urcb-fallback --arm IEC61850-LAB-STATIC-RCB
```

The strongest successful result is `SMART_STATIC_RCB_TRIAL_PASS`, which means
the selected static RCB was enabled, its binding remained populated during
bounded confirmation Reads, an InformationReport was decoded, and cleanup
completed without deferred state.

`STATIC_TRIAL_LIFECYCLE_PASS_REPORT_PENDING` exits with code 4. It means guarded
enable/confirm/disable cleanup passed, but no GI report arrived within the
bounded observation window. That is lifecycle evidence, not report-delivery
acceptance.

## Portable runtime API

Applications can embed `MmsStaticReportSessionRuntime` directly:

1. retain the `MmsLiveDiscoveryResult` for the session lifetime;
2. call `prepare()` for read-only selection and pre-claim failover;
3. inspect `snapshot()` and the selected static binding;
4. call `start()` only when remote activation is authorized;
5. poll or drain reports, then call `stop()` before disconnecting.

The runtime rejects `write_data_set_reference=true` at construction, so static
and dynamic DataSet ownership cannot be accidentally mixed.
