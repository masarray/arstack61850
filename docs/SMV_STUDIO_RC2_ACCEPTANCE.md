# ARStack Studio RC2 physical acceptance

RC2 is the final functional/performance acceptance gate before merge/release. It does **not** replace RC1 USB/session checks and it cannot be completed by synthetic CI alone.

## Candidate freeze

Run RC2 against one exact PR head. Record the full 40-character SHA in the evidence JSON and validate with the same SHA. Any production-code change invalidates that physical result and requires a new candidate.

## Required sequence

1. Launch the exact Windows candidate and verify the current ESP32-P4 injector reaches READY without a firmware prompt.
2. Load the bundled 4I+4V Quick Start profile, Deploy, reach READY, then Start.
3. Complete at least 10 clean Start/Stop cycles and one Zero operation.
4. While RUNNING, exercise live magnitude, phase, frequency, Quality, CT saturation, and profile re-sync. Confirm signal generation advances and `smpCnt` does not restart because of a live edit.
5. Start a retained run of at least 3600 seconds. The whole run must remain within the 4000-fps contract, with zero missed sample slots, zero canonical TX failures, zero health-triggered reconnects, and zero automatic Starts.
6. During the retained run, keep Studio usable and responsive; no worker/process may remain orphaned after shutdown.
7. Independently capture at least 10 seconds of SV traffic. The capture must prove destination MAC, APPID, VLAN/PCP, svID, confRev, exactly 1 ASDU, 64-byte sample payload, continuous `smpCnt`, and at least one `3999 -> 0` wrap.
8. `smpSynch` must remain advertised as `0` for this RC2 gate. Any future synchronized claim requires a separate measured-clock evidence package and must not be inferred from configuration alone.

## Telemetry acceptance

The evidence evaluator requires:

- `durationSeconds >= 3600`
- profile rate exactly `4000`
- one-second observed fps windows within `3999..4001`
- `missed = 0`
- `txFailures = 0`
- `healthReconnects = 0`
- `automaticStarts = 0`

The narrow 3999..4001 observation envelope only accounts for one-second reporting-window boundary effects. It does not relax the configured 4000-fps publisher contract; zero missed slots and independent counter continuity remain mandatory.

## Machine-readable evidence

Copy:

`apps/arstack_studio/rc2/rc2-evidence-template.json`

Fill it only from the physical run and independent capture. Do not mark an item true because CI simulated it.

Validate on the exact candidate build:

```text
arstack_rc2_acceptance --evidence rc2-evidence.json --expect-sha <40-char-pr-head-sha>
```

Exit code `0` is RC2 policy PASS. Any missing/false/unsafe field is fail-closed and prints the rejected acceptance conditions.

## CI boundary

CI only runs:

```text
arstack_rc2_acceptance --self-test
```

The self-test proves that a complete positive fixture passes and that unsafe mutations such as shortened soak, missed slots, TX failure, `smpCnt` restart, broken capture continuity, wrong payload evidence, non-zero `smpSynch`, or an unresponsive UI are rejected. CI success is therefore necessary but never sufficient for RC2 physical acceptance.

## Merge boundary

PR #79 remains unmerged until both RC1 and RC2 physical evidence are complete. The final acceptance issue must contain the candidate SHA, evidence JSON result, retained-run telemetry summary, and independent capture reference.
