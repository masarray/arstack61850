# M4-P0E3A — Three independent MU scenario COMMIT/readback from Studio

**Scope:** Studio now submits exactly three frozen 4I+4V per-MU signal banks using the already-merged P0D3 firmware protocol. **GROUP START REMAINS BLOCKED** until a separate group-START verification phase and physical P0F acceptance; this change does not claim 12kfps.

## Engineer workflow

1. Open validated three-IED SCD in the single Studio GUI. Select each MU page and edit independently (I/U magnitude, phase, quality, enable state, frequency and CT shape).
2. Press **Deploy 3 MU**, then wait for exact **ARM3** firmware SHOW/SELECT roster verification on the *current boot*.
3. Press **Apply 3 MU**. The host first freezes all three drafts and validates each typed wire field. If *any* MU is invalid, **zero** CFG commands are enqueued.
4. Using the same existing `DeviceIoWorker`, transmit a fixed **12-command exclusive batch** for MU1: `SVSTREAM 1 CFG BEGIN/FREQ/8 SET/SHAPE/COMMIT`. Its `sv_scenario_hash` is computed from the exact canonical typed `SvLiveSignalState`, not GUI JSON or a new hashing scheme.
5. Do **not** advance to MU2 until firmware responds with matching `SVSTREAM CFG COMMITTED slot=1 token=... hash=... generation=...` AND matching fresh `SVSTREAM CFG READBACK...`. Repeat for MU2 and MU3 with their independent token/hash/generation. An acknowledged staging step does not count as COMMIT/READBACK.
6. The exact 3/3 confirmation is a **board RAM signal-bank proof**, not START or Ethernet proof. UI and copied diagnostics distinguish roster readiness from signal-bank readiness, and the legacy single-SV START remains gated.
7. Operator **STOP/F6** cancels an in-progress multi-MU apply. The sole serial worker clears unsent exclusive commands and reserves a priority STOP slot. USB loss, new boot/session, timeout, firmware rejection and Studio shutdown revoke host readback authority; Studio exit itself never sends STOP.

## Safety and performance

- No new task, timer or heap use in the firmware 250 µs SV hot path.
- Host-only one-shot 7 s ACK/readback stage timer, plus a 60 s monotonic hard bound. No automatic replays. Firmware still enforces its own 8 s atomic staging deadline.
- Per-MU atomicity is **not** group atomicity: MU1 and MU2 might be committed when MU3 is rejected. Failure status explicitly says partial board state is possible. START remains inhibited.
- Supervisor permits Apply only if current SCD ARM3 hash matches firmware-selected roster, the board is verified and STOPPED, and no competing profile, PTP startup, M4 upload or scenario transaction owns serial.
- Existing production binary and unit harness targets link the exact same `M4ThreeMuScenarioApply` C++ implementation. Added negative tests to the already-existing `arstack_m4_upload_harness` and STOP isolation to S8 deterministic session harness. No workflow, job or matrix added.

**Next:** P0E3B full independent live bank SHOW including CT shape and reconnect adoption; P0E3C verified group START without deselecting M4 and per-slot START/STOP; P0F physical 3×4,000fps/PTP same-epoch capture. M3.H.3 Stable remains immutable.
