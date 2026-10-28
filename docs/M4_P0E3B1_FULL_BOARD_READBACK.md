# M4-P0E3B1 — Complete three-MU board snapshot inspection

## Delivered control-plane contract

M4 now has a **read-only** board-inspection path; it does not use the Studio draft as evidence and never invokes START/STOP, SELECT or waveform SET. The existing single `DeviceIoWorker` owns the serial transport. The engineer can press **Inspect board** after (re)connecting an M4-capable ESP32-P4, including while SV remains autonomously RUNNING.

Firmware `SVSTREAM <1..3> SHOW` emits one local atomic `SvLiveSignalBank::snapshot()` using:

```text
SVSTREAM SNAP BEGIN slot=1 generation=... hash=... hz_millihz=... tx_enabled=... ct_enabled=... ct_dc=... ct_harmonic=... ct_order=... ct_clip=...
SVSTREAM SNAP ROW slot=1 generation=... channel=IA rms=... phase=... quality=... enabled=...
... 8 distinct IA/IB/IC/IN/UA/UB/UC/UN rows ...
SVSTREAM SNAP END slot=1 generation=... hash=...
```

The hash is the **existing** `sv_scenario_hash` over the exact wire state (not a new algorithm), ignoring generation as prescribed by the original canonical contract. A generation of zero is valid for a pristine default signal bank. This path runs on firmware's UART control task, **not** its 250µs publisher loop; it adds no heap allocation or per-packet work.

The production `M4ThreeMuBoardReadback` collects **three sequential** SHOWs and accepts a slot only with matching slot/generation, exactly eight unique typed channels (quality and enabled included), CT shaping, and a matching canonical BEGIN/END hash. A failed/missing/mixed row, illegal field, changed boot epoch, timeout or transport error invalidates all three results, never yielding a false partial "verified". The controller waits for `commandTransmitted` before consuming a response, uses a single 6s deadline per slot, and clears proof on session/boot change, shutdown, STOP, roster or scenario mutation. It does **not** claim physical timing, NIC TX rates, or general M4 START readiness.

Studio shows a concise three-slot **BOARD** strip with frequency, generation, TX enable and a detailed engineering hover view (all eight raw wire-channel values, phase, quality and CT state). The board readback is deliberately separate from M4 draft pages and does **not silently overwrite an editor draft after reconnect**. Engineers can inspect an autonomous running board even when the M4 draft editor is blocked.

## Regression & CI cost

Extended the **existing** `arstack_m4_upload_harness` to check production parser success, all three state snapshots, missing/duplicated row, changed quality/hash and changed boot. No additional workflow/jobs, no new thread/serial owner, and no changes to Official Stable M3.H.3. Existing Qt/Linux/Windows and firmware CI must pass on the exact PR head.

## Remaining gates

- **P0E3B2**: automatic boot-scoped, board-authoritative MULTISV selected-roster discovery and safe RUNNING reconnect adoption; match roster identity to SCD and stream generations without silently deploying GUI drafts.
- **P0E3C**: full verified group START/STOP and per-slot enable readback independent of V1, then physical P0F three-stream capture/egress skew and PTP validation.

Do not call this milestone complete M4 START or physical 12kfps proof.
