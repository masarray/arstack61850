# M4-P0E2C1 — Single-owner Studio MULTISV serial deployment

**Scope:** operator-initiated **configuration deployment and roster readback**; **not** three-stream live scenario application or START. Maintains M3.H.3 Official Stable firmware and V1 single-SV path unchanged.

## Operational sequence

1. Open a three-MU SCD in Studio; explicitly choose 9-2LE and the verified 4000 counter policy. `SclProfileModel` caches a single canonical ARM3 V2 envelope. Edit MU draft pages separately.
2. Connect a **verified, stopped** ESP32-P4 with both `MULTISV-CONFIG-STAGING-V2` and `MULTISV-TX-LAB-PREVIEW` capabilities. Staging-only boards cannot pass this gate.
3. Select **Deploy 3 MU**. Supervisor suppresses automatic legacy V1 profile synchronization and grants the existing `DeviceIoWorker` single-owner queue to the M4 transaction. The GUI's legacy START remains disabled.
4. For each control-plane step, `M4MultiSvUploadTransaction` emits at most one command. The controller accepts its ACK only **after the worker confirms transmission**. It enforces monotonic transaction ID, contiguous <=48-byte chunks, exact firmware ACK counters, validation before COMMIT, whole-envelope hash+size+generation, then all three exact `MULTISV SHOW` stream rows before `MULTISV SELECT` confirmation.
5. A per-step 5-second watchdog bounds stalled writes or ACK/readback, and a 120-second overall budget prevents a never-ending sequence of individually valid slow ACKs. No auto retry, speculative send, extra Qt worker, duplicated clock, MCU XML parser or TX hot-path allocation.
6. Explicit operator STOP interrupts upload immediately, then queues the worker's priority STOP. Session generation/boot changes, USB disconnect, error/rejection and GUI shutdown revoke the local proof. The firmware may retain an already committed roster; a later session must re-read or redeploy instead of assuming success.
7. A fully matched SELECT ACK marks **roster verified** and records its content hash. This is **NOT** evidence of per-MU signal state being applied or Ethernet 12kfps. Live three-MU START is deliberately blocked until P0E3/P0F acceptance.

## CI cost and regression

This change adds dedicated regression cases to the **existing** headless deterministic session harness; it does not create any extra workflow, CI job, runner matrix, packaging task or firmware build variant. The pull request receives only one consolidated commit after offline source/contract checks. Existing Qt/Windows native preview checks compile the production controller and unchanged worker. No physical hardware claim follows from host CI.

## Next gates

- **P0E2C2 / P0E3:** exact three independently staged `SVSTREAM <slot> CFG` transactions, SET/FREQ/SHAPE readback, host reconnect adoption and explicit safe group START.
- **P0F:** capture independently via NIC/Wireshark 12,000 canonical frames/s aggregate and validate three MAC/svID identities, common smpCnt/PTP phase, measured egress skew/overruns and autonomy across unplug/reconnect.
