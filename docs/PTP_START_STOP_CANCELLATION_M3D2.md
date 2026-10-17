# M3D.2 — PTP START/STOP cancellation acknowledgment

The PTP facade's M3D.1 START/config admission gate did not include STOP:
a concurrent STOP could see an unstarted SOURCE or RECEIVER and return
while its START caller later entered READY and reported success.

One fixed-capacity atomic byte now coordinates START, config, and STOP.
- A START/config atomically acquires the idle control slot. No FreeRTOS
  spinlock is held while waiting for the PTP worker's bounded startup.
- STOP publishes cancellation immediately. If control is idle, STOP
  acquires the slot and performs existing bounded SOURCE/RECEIVER rollback.
  If START owns it, STOP returns (request accepted); the START owner drains
  cleanup **before returning** and reports failure, never late success.
- The slot is released by CAS against the same STOP bit. This closes the
  check/unlock lost-cancellation window. Repeated STOPs while rollback is
  underway coalesce; cleanup invokes at most once per ownership interval.
- SOURCE/RECEIVER live status, generation, traceability and clock usability
  are masked immediately when STOP is pending. The high-frequency SV ISR
  and smpSynch AUTO (0) remain unchanged.
- Portable regression checks cancellation while START is active, duplicate
  STOP, idle STOP, request during rollback, and 200 controlled two-thread
  interleavings without arbitrary sleeps. Guarded ESP32-P4 SOURCE and
  RECEIVER builds validate the integration.

A concurrently requested STOP is a nonblocking cancellation request: the
already-running START is responsible for the eventual cleanup/acknowledgment.
No hardware test, real GM, PPS capture, SV wire-phase conformance, or
flashable public release is implied by CI compilation.

## Opt-in hardware bench bundle (M3D.2)

The **one-shot Draft** `phase-guard-checkpoint` compiles experimental
SOURCE and RECEIVER configurations separately and copies the exact binaries
listed by ESP-IDF's own `build/flasher_args.json`. Both role bundles contain
`manifest.json`, `sdkconfig`, `flasher_args.json` and verified
bootloader/partition/application `.bin` files, with SHA-256 per image.
`prHeadCommit` is the PR head rather than GitHub's synthetic pull-request
merge SHA; `checkoutCommit` records that distinct build checkout.
The embedded semantic build ID is `prHeadCommit[:16]`.

Artifacts are retained for **7 days** and named
`esp32p4-m3d-ptp-bench-<PR-head-SHA>`. They are experimental
**software-compiled images**, not an official stable installer/firmware
release and not hardware tested. Match the bundle's `role`, flash offsets,
SDK target and board revision before any physical test; do not flash based
on an assumed fixed partition layout or a stale output folder.
