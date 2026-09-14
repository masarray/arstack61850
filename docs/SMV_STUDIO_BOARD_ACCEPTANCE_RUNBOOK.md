# ARStack Studio P0 physical board acceptance runbook

This runbook is for the frozen P0 release candidate only.

Frozen production SHA:

`ed46f3bed73d398d71be2eacf34c4995325c3cad`

Release remains blocked until issue #83 records both RC1 physical USB/session acceptance and RC2 physical functional/performance evidence. The QA tooling in `qa/p0-board-acceptance` is intentionally outside the frozen production branch and must not be merged into the product merely to complete acceptance.

## 1. Test assets

Use the artifacts pinned by the release freeze:

- Windows candidate from Release run `34863664544`, artifact `10358737281`.
- ESP32-P4 firmware package from the same run, artifact `10358024903`.
- RC2 evaluator kit from run `34863664307`, artifact `10355888453`.
- Board acceptance QA kit built from branch `qa/p0-board-acceptance`.

Do not substitute a local development build for physical release acceptance.

## 2. Bench prerequisites

Required:

- one Windows x64 test PC;
- the intended ESP32-P4 injector;
- USB connection used by ARStack Studio;
- independent Ethernet observer/capture path for IEC 61850 SV traffic;
- the frozen Windows candidate and firmware package;
- enough uninterrupted bench time for a retained run of at least 3600 seconds.

For the wrong-device recovery case, a second ARStack injector is required. If a second injector is unavailable, record that case as **not executed**; do not convert it to PASS.

## 3. RC1 — physical USB/session matrix

Run these against the frozen Windows candidate, not the headless QA runner.

### RC1-A — current-firmware cold/open cycles

Repeat **10 times**:

1. ensure Studio is closed;
2. power-cycle or physically reconnect the intended board;
3. launch the frozen Studio candidate;
4. verify semantic identity reaches `READY` or an actionable bounded terminal error;
5. verify **no false Install Firmware prompt** appears for current firmware;
6. record COM port, `device_id`, firmware version, and result;
7. close Studio and confirm no orphan process remains.

Acceptance: 10/10 current-firmware cycles, zero false Install prompts, no unbounded CONNECTING/PREPARING.

### RC1-B — READY unplug/replug

1. reach `READY`;
2. record `device_id` and COM port;
3. unplug USB;
4. verify Studio leaves READY and does not claim RUNNING;
5. reconnect the same board;
6. allow Windows to reuse or renumber COM;
7. verify the same semantic `device_id` returns to READY;
8. verify output remains STOPPED.

### RC1-C — RUNNING unplug/replug

1. reach READY and Start;
2. confirm RUNNING;
3. unplug USB;
4. verify output/session drops;
5. reconnect the same board;
6. verify recovery returns to READY/STOPPED;
7. verify Studio **never automatically Starts** after recovery.

### RC1-D — wrong-device recovery

Requires a second injector.

1. verify injector A and record its `device_id`;
2. unplug A during recovery state;
3. connect injector B;
4. verify B is rejected fail-closed as a different semantic device;
5. reconnect A and verify explicit recovery remains possible.

### RC1-E — serial/firmware ownership handoff

Using the frozen Studio candidate and pinned firmware package:

1. start from a semantically verified board;
2. invoke the explicit firmware update/recovery flow;
3. verify output is stopped before handoff;
4. verify serial ownership is released before `espflash` is allowed to start;
5. verify there is no Windows `Access is denied` COM contention;
6. verify flash/reset/re-enumeration completes;
7. verify the board returns with the current semantic identity;
8. verify it returns STOPPED, never auto-Started.

### RC1-F — single-instance/hard-crash

1. start frozen Studio with the board connected;
2. launch a second Studio process and verify it is blocked;
3. hard-kill the primary process;
4. launch Studio again and verify stale-lock reclamation works;
5. verify the board can be reacquired without a stranded COM owner.

Only after RC1-A..F have physical evidence should the RC1 physical section in issue #83 be marked complete.

## 4. RC2 — automated functional + retained-run board path

Close Studio before starting the headless runner. The runner acquires the same production single-instance lock and therefore fails if Studio still owns the session.

From the Board Acceptance QA kit:

```powershell
.\arstack_board_acceptance.exe --output .\rc2-board-evidence.json
```

Default retained-run duration is **3600 seconds**.

The runner uses the same frozen production `SclProfileModel`, `StudioDeviceController`, `DeviceIoWorker`, `SmartSessionController`, and `FirmwareManager` implementation and performs:

1. automatic semantic discovery;
2. bounded convergence to READY;
3. verification that current firmware does not present an install/update requirement;
4. built-in 4I+4V profile synchronization;
5. 10 clean Start/Stop cycles;
6. Zero;
7. RUNNING live magnitude edit;
8. RUNNING live phase edit;
9. RUNNING frequency edit;
10. RUNNING Quality edit;
11. RUNNING CT-saturation enable/disable;
12. Stop;
13. profile re-sync while STOPPED, because production intentionally blocks profile deployment while RUNNING;
14. explicit conservative `smpSynch=0` policy confirmation;
15. Start again;
16. retained RUNNING soak;
17. continuous telemetry checks for fps, missed slots, TX failures, health reconnects, and automatic Start;
18. final Stop.

The runner emits `rc2-board-evidence.json`. It intentionally leaves independent-capture and Studio-UI fields in a failing state. A headless runner is not allowed to certify those observations.

A short diagnostic run may be invoked with `--soak-seconds N`, but **anything below 3600 seconds is not RC2 acceptance evidence**.

## 5. Independent SV capture

During the accepted retained run, capture at least 10 seconds of SV traffic from an independent observer.

The capture must prove:

- destination MAC matches the frozen profile;
- APPID matches;
- VLAN ID / PCP match;
- `svID` matches;
- `confRev` matches;
- exactly 1 ASDU;
- 64-byte sample payload;
- `smpCnt` continuity with no unexplained gap;
- at least one `3999 -> 0` wrap;
- live edits do not restart `smpCnt`;
- `smpSynch=0` throughout the accepted evidence.

Keep the capture file as a release-evidence artifact/reference. Do not infer these items from Studio configuration alone.

## 6. Studio responsiveness/orphan observation

The automated board runner is headless, so separately run the frozen Studio candidate long enough to verify:

- UI remains responsive during active 4000-fps operation;
- normal controls continue updating;
- closing Studio does not leave an orphan Studio process, worker, or firmware tool process.

These observations must be entered only after they are physically observed.

## 7. Finalize fail-closed RC2 evidence

After the automated board run and independent capture:

```powershell
.\finalize-rc2-evidence.ps1 `
  -Evidence .\rc2-board-evidence.json `
  -Evaluator .\arstack_rc2_acceptance.exe `
  -Output .\rc2-final-evidence.json
```

The finalizer asks only for facts that cannot be proven by the headless board runner: independent capture facts and Studio UI/orphan observations. It then invokes:

```text
arstack_rc2_acceptance --evidence rc2-final-evidence.json --expect-sha ed46f3bed73d398d71be2eacf34c4995325c3cad
```

Exit code `0` is the policy verdict required for RC2 physical acceptance.

## 8. Evidence to attach to issue #83

Record at minimum:

- frozen candidate SHA;
- RC1 cycle/matrix results;
- board `device_id` used for accepted run;
- `rc2-final-evidence.json`;
- evaluator PASS output;
- retained-run start/end time and duration;
- independent capture filename/reference;
- any screenshots/log excerpts useful for traceability.

If any physical case fails, stop release acceptance. Treat the failure as a release blocker under `docs/SMV_STUDIO_RELEASE_FREEZE.md`; do not broaden scope or silently move the freeze baseline.
