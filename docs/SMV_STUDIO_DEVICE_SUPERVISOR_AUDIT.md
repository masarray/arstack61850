# ARStack Studio P0.5 Device Supervisor Audit

Status: implementation audit and migration plan after the first physical ESP32-P4 firmware/reconnect tests.

This document complements `SMV_STUDIO_DEVICE_SUPERVISOR_ARCHITECTURE.md`. The architecture document defines the target. This audit records what the current implementation actually does, where it violates the target invariants, and the safest order to migrate without creating a second source of truth.

## Executive verdict

The current application can build, package, discover a serial device, flash a verified ESP32-P4 image, reconnect, and in at least one physical test verify the newly flashed ARStack firmware. That proves the firmware packaging and basic serial/flash paths are useful.

The control plane is not yet production-ready because several independent asynchronous mechanisms still share implicit state:

- `DeviceController` owns `QSerialPort` directly on the GUI thread;
- `SmartSessionController` owns discovery/update/profile orchestration but also infers state from `DeviceController` booleans and human logs;
- `StudioDeviceController` independently scrapes the same log to decide whether firmware is current;
- `WorkflowBar.qml` derives additional product state and automatically opens recovery dialogs;
- `FirmwareManager` owns an external `espflash` process without an explicit port-ownership handshake with the serial controller;
- profile deployment is a fire-and-wait sequence with no bounded transaction timeout.

These weaknesses explain the physical symptoms already observed: false firmware-recovery prompts after a successful flash, `Access is denied` around re-enumeration, and `PREPARING` that can remain forever.

The fix must be a staged control-plane migration, not another collection of timers or popup conditions.

## Prime migration rule

**Do not create a new `DeviceSupervisor` beside `SmartSessionController`.**

`SmartSessionController` is already the closest thing to the product-level session authority. Creating a second supervisor while the old one remains active would violate `AGENTS.md` and make the race surface larger.

Migration rule:

1. evolve `SmartSessionController` in-place into the typed supervisor;
2. keep its QML type/name during P0.5 so QML does not need a simultaneous big-bang rewrite;
3. extract serial ownership from `DeviceController` into `DeviceIoWorker` behind the existing controller facade;
4. retire redundant state derivation from `StudioDeviceController` and QML as each typed supervisor field becomes authoritative;
5. only consider a class rename after physical acceptance, when it is a mechanical/API cleanup rather than an architectural change.

## Current ownership map

### `DeviceController`

Current responsibilities are too broad:

- enumerates ports with `QSerialPortInfo`;
- chooses a recommended candidate;
- owns and opens `QSerialPort`;
- performs the semantic handshake;
- parses identity, profile, state, timing and PTP lines;
- stores device identity fields;
- sends START/STOP/profile/live-setpoint commands;
- owns profile armed/deploying booleans;
- stores diagnostics text;
- interprets transport errors.

Because the QML `DeviceController` object is instantiated on the GUI thread, `QSerialPort`, parsing callbacks, writes, handshake timers, live flushes and heartbeat activity currently run on the GUI thread.

### `StudioDeviceController`

Adds:

- 20 ms coalesced live-setpoint flush;
- 700 ms session heartbeat;
- firmware-current gating for START and profile deploy;
- a second firmware identity parser that searches `logText`.

This duplicates firmware truth already derived by `SmartSessionController` and must be removed once typed identity exists.

### `SmartSessionController`

Currently owns:

- 2.5 s discovery watchdog;
- automatic blank-board ROM probing;
- firmware update/reconnect workflow;
- version comparison;
- default-profile synchronization;
- the presentation state string.

It is therefore the correct object to evolve into the single supervisor, but its current model is a collection of booleans (`updateRequested_`, `blankProbeInFlight_`, `blankBoardDetected_`, `setupError_`, `needsProfileSync_`, `profileSyncInFlight_`) rather than typed orthogonal state.

### `FirmwareManager`

Good existing properties that must be preserved:

- manifest/schema validation;
- ESP32-P4 target verification;
- pre-v3 revision policy;
- SHA-256 verification;
- flash offset validation;
- bounded launch/probe/flash/reset deadlines;
- terminal outcome per operation;
- explicit shutdown/reap.

Missing architectural boundary:

- it can start `espflash` immediately after the serial controller calls `QSerialPort::close()`, but there is no explicit `portReleased` acknowledgement/ownership token across the Windows driver boundary.

### QML

`WorkflowBar.qml` correctly funnels F5/Start through Smart Session for firmware gating, but QML still derives some independent truth:

- `Main.qml` computes `canDeploy` / `canStart` from raw device/profile properties;
- edits send directly to `device` whenever `deviceVerified` is true;
- header click can invoke `device.autoDetectAndConnect()` directly;
- recovery dialogs are opened automatically from state changes.

QML must end as a pure projection/action surface over supervisor-owned product state.

## P0 blockers found by audit

### P0-A — human diagnostic log is part of product state

`DeviceController` parses only product/target/protocol/device ID into typed fields. Firmware version is not part of its typed identity. `SmartSessionController::refreshFirmwareIdentity()` searches the latest `ARSTACK identity` line in `logText`; `StudioDeviceController::currentFirmwareIdentitySeen()` implements the same pattern again.

Impact:

- two firmware-version parsers;
- diagnostics truncation/format changes can alter control behavior;
- impossible to attach a boot/session identity to an event;
- stale lines can be confused with current connection state.

Required fix: one typed `DeviceIdentity` parsed once at the transport boundary.

### P0-B — handshake is single-shot and timing-sensitive

Opening a port immediately sends `IDENTIFY`, `SHOW`, and `PROFILE SHOW`. A single one-shot verification timer (about 1.1 s during generic probing, 2.6 s otherwise) closes the port if identity has not appeared. There is no bounded IDENTIFY retry and the firmware does not proactively announce identity when the control task becomes ready.

Impact:

- USB CDC re-enumeration / firmware startup timing can create false identity failure;
- installed/current firmware can be classified as unknown;
- the recovery path may then be entered incorrectly.

Required fix: firmware boot identity announcement plus bounded request/retry handshake.

### P0-C — normal identity failure can become automatic ROM recovery

`SmartSessionController` schedules a read-only ROM probe when the application identity is not verified and a candidate port is available. A successful ROM probe proves only that a supported ESP32-P4 boot ROM can be reached; it does not prove application firmware is absent.

Impact:

- transient semantic-handshake failure can produce `Firmware required`;
- user can be asked to reinstall firmware that was already flashed correctly.

Required fix: remove automatic ROM classification from the normal startup path. ROM probing begins only after an explicit operator Recover/Install action from an `unidentified` state.

### P0-D — serial ownership and firmware-tool ownership are not transactional

Firmware update currently performs:

`device.disconnectPort()` -> `firmware.probeTarget()`

in the same orchestration path. There is no explicit ownership state, release acknowledgement, re-enumeration generation, or driver-settle proof.

Impact:

- Windows COM handle races;
- `Access is denied` / permission failures;
- reconnect can target a transient/stale port name;
- success/failure can depend on USB driver timing.

Required fix: `PortOwner { none, device_session, firmware_tool }` and an acknowledged release/acquire protocol.

### P0-E — profile synchronization is not a bounded transaction

`deployProfile()` marks `profileDeploying=true`, sends a command sequence, and waits for a textual committed/armed response. Smart Session clears `profileSyncInFlight_` only on the success condition `profileArmed && !profileDeploying`.

If the firmware rejects the transaction, the transport drops a response, the COM handle becomes degraded, or no response arrives, `profileSyncInFlight_` can remain true indefinitely. `PREPARING 4I+4V` therefore has no guaranteed terminal transition.

Required fix: generation-aware profile transaction with deadline and terminal success/failure.

### P0-F — transport errors are under-classified

Only `ResourceError` and `DeviceNotFoundError` force disconnect. Other errors on an open serial handle are logged while the current verified/connected state may remain alive.

Impact:

- Windows `PermissionError` / access-loss conditions can leave stale verified state;
- profile and live commands can continue to target a broken handle.

Required fix: structured transport error mapping. Permission/access/read/write failures that invalidate control reliability must retire the session generation and close the link.

### P0-G — no session generation

Callbacks and text responses do not carry a connection generation. After reset/reconnect there is no formal barrier preventing late callbacks or cached state from the old serial session from affecting the new one.

Required fix: monotonic `sessionGeneration`, attached to worker events and transactions.

### P0-H — USB presence observation is indirect

Port discovery is refreshed during connection attempts. While a verified port is open, removal detection relies mostly on serial errors. There is no independent device-presence observer and no identity-based reacquisition after COM renumbering.

Required fix: worker-side bounded presence observation. Use portable `QSerialPortInfo` polling first; optionally add a Windows native device-change adapter later if measurement shows the polling latency is insufficient.

### P0-I — direct QML/device commands bypass the future supervisor contract

Setpoint edits, frequency changes, quality updates and some connection actions can call `device` directly. `deviceVerified` alone is not the complete permission to send control traffic.

Required fix: supervisor provides the actionable command gate/capability context. The device facade may still expose methods for compatibility during migration, but QML must stop deriving control permission itself.

### P0-J — single-instance ownership is absent

A second Studio process can contend for the same COM port and produce access errors indistinguishable from driver/re-enumeration failures.

Required fix: acquire a process-level single-instance lock before starting device workers. The second process should exit cleanly after notifying/foregrounding the existing instance where practical.

## Invariants that must not change during migration

1. Public P0 wire scope remains exactly 4I + 4V, 4000 fps reference flow, 1 ASDU, 64-byte sample payload and 16 leaves.
2. `smpSynch` remains honest/unsynchronized (`0`) until measured disciplined-clock evidence exists.
3. Firmware flashing remains user-approved and fail-closed.
4. Wrong chip/revision/hash/manifest/offset remains blocked before write.
5. Output never auto-Starts after boot, reconnect, firmware update or profile restore.
6. Hard loss of the Studio control session while RUNNING remains bounded by the firmware lease (~2.5 s).
7. The realtime publisher hot path receives no desktop worker/logging complexity.
8. Live setpoints retain bounded last-value-wins/coalesced behavior.
9. No unbounded retry, reconnect, worker queue or diagnostics queue is introduced.
10. Normal Studio startup must never write firmware automatically.

## Migration strategy — seam first, worker second

A full worker/supervisor rewrite in one patch would combine protocol grammar, threading, USB ownership, firmware recovery, profile transactions and QML changes. That is too broad for the current hardware-learning phase.

Use the following checkpoints.

### Checkpoint S1 — typed semantic identity and firmware boot announcement

Embedded:

- add per-boot nonce (`boot_id`);
- emit one machine-readable identity announcement only after the control task is ready;
- keep `IDENTIFY` request/response authoritative;
- keep the existing stable EFUSE `device_id`.

Desktop:

- extend the one identity parser to capture firmware version, boot ID and capabilities;
- store typed identity on `DeviceController`/transport boundary;
- remove `SmartSessionController::refreshFirmwareIdentity()` log scraping;
- remove `StudioDeviceController::currentFirmwareIdentitySeen()` log scraping;
- compare current firmware from typed identity only.

Regression:

- valid current identity;
- legacy identity;
- malformed identity;
- missing firmware field;
- wrong target/protocol;
- stale boot ID/session identity rejection once S2 adds generation.

Hardware gate:

- reboot board repeatedly and verify identity appears without requiring exact IDENTIFY timing.

### Checkpoint S2 — bounded semantic handshake; disable automatic ROM classification

- retry IDENTIFY at a bounded cadence (target: about 4 attempts across 2-3 s; exact values measured on the board/Windows path);
- identity timeout produces `DEVICE NOT IDENTIFIED`, never `FIRMWARE REQUIRED`;
- remove `maybeScheduleBlankBoardProbe()` from ordinary startup;
- expose explicit `Recover firmware` action only after semantic failure;
- ROM probe remains read-only until explicit recovery action.

Regression:

- first IDENTIFY response lost, second succeeds;
- all identity attempts time out -> unidentified, no ROM probe;
- current firmware never opens Install dialog;
- legacy identity opens Update, not Recover.

### Checkpoint S3 — bounded profile transaction

Do this before worker extraction because it removes the current `PREPARING forever` release blocker with a small, testable seam.

- add profile transaction state (`idle/sending/waiting/armed/failed`);
- record previous profile generation;
- start a 3-5 s local-transport deadline (final value chosen from measurement);
- success requires newer committed/armed generation for the current serial session;
- rejection, write failure, timeout or disconnect ends the transaction;
- allow at most one controlled reconnect/resync retry by policy;
- otherwise expose `PROFILE SYNC ERROR` + explicit Retry;
- no boolean may remain in-flight without an owner timer/result.

Regression:

- commit succeeds;
- commit rejected;
- response omitted;
- disconnect during transaction;
- late success from retired transaction ignored.

After S1-S3, produce a short hardware RC. Acceptance: current flashed board must open repeatedly without firmware prompt and must reach bounded READY or a bounded actionable error. Do not wait for worker extraction to validate these control semantics.

### Checkpoint S4 — extract `DeviceIoWorker` behind the existing facade

Preserve the `DeviceController` QML-facing shape temporarily, but move all serial I/O into one long-lived worker thread.

Important construction rule: create the actual `QSerialPort` and worker timers in the worker thread after `moveToThread`; do not create a UI-thread `QSerialPort` and then accidentally leave its affinity behind.

Worker responsibilities:

- port enumeration/presence polling;
- open/close/read/write;
- handshake timers;
- machine-line parsing or structured events;
- bounded command queue;
- quiet heartbeat/health requests;
- release acknowledgement.

UI/supervisor responsibilities:

- no direct `QSerialPort` calls;
- no waits/blocking joins on normal UI operations;
- typed event consumption only.

Queue policy:

- control/config commands: small ordered bounded queue;
- live setpoints: existing channel-wise last-value-wins coalescing;
- telemetry: coalesce stale values;
- exclusive profile/firmware operations block unrelated configuration mutation.

### Checkpoint S5 — session generation + explicit port owner + FirmwareWorker boundary

- add monotonic `sessionGeneration` in Smart Session/supervisor;
- every worker event carries generation;
- retired-generation events are ignored;
- implement `PortOwner`;
- firmware process may start only after worker emits `portReleased(generation)`;
- after reset, do not trust COM number alone; reacquire candidate then prove semantic device ID/boot ID;
- success popup remains after semantic firmware verification, never after flash exit code alone.

Move `FirmwareManager` process execution behind a worker/service boundary at this checkpoint. Preserve its existing manifest/hash/revision/deadline code rather than rewriting it.

Regression:

- firmware tool refused while device owns port;
- explicit release -> firmware tool allowed;
- stale reconnect event ignored;
- COM number changes across reset;
- PermissionError retires session;
- one terminal firmware result per attempt.

### Checkpoint S6 — USB/hotplug + health + single instance

- bounded worker-side `QSerialPortInfo` presence scan (initial target 500-750 ms, measured CPU impact);
- immediate link retirement on fatal serial error;
- device ID becomes physical identity after handshake, COM name only transport locator;
- unplug READY -> DISCONNECTED;
- unplug RUNNING -> DISCONNECTED, firmware lease owns fail-safe STOP;
- replug -> identity -> profile sync -> READY, never auto-Start;
- add single-instance process lock;
- add lower-rate positive health request separate from 700 ms safety heartbeat.

Health policy must be bounded, e.g. a small number of missed replies before `online -> degraded -> reconnecting`; exact numbers come from physical latency measurement, not arbitrary sleeps.

### Checkpoint S7 — make QML presentation-only

Once typed supervisor state is complete:

- QML does not call `autoDetectAndConnect()` directly;
- QML does not derive firmware truth from `deviceVerified` or logs;
- QML does not derive `canStart` / `canDeploy` independently;
- all Start/Stop/Recover/Retry actions enter supervisor commands;
- dialogs are opened from actionable supervisor state, with no repeated automatic popup after a terminal failure;
- Advanced/Diagnostics display worker/supervisor diagnostics but cannot mutate hidden state directly.

## Typed state target inside existing SmartSessionController

During P0.5 keep the public class but replace the boolean web with a context resembling:

```text
DeviceSessionContext
  generation
  physicalState
  transportState
  identityState
  firmwareState
  profileState
  outputState
  portOwner
  candidatePort
  verifiedDeviceId
  currentBootId
  lastErrorCode
```

Presentation strings are derived in one function from this context. QML receives both concise presentation and typed properties where needed.

## Error model

Introduce one lightweight machine-readable error enum for the control plane; do not create separate unrelated error frameworks in each class.

Minimum categories:

```text
None
PortMissing
PortBusy
PermissionDenied
OpenFailed
ReadFailed
WriteFailed
IdentityTimeout
IdentityMalformed
WrongTarget
ProtocolUnsupported
FirmwareLegacy
FirmwareProbeFailed
FirmwareFlashFailed
FirmwareVerifyFailed
ProfileRejected
ProfileTimeout
HealthTimeout
Cancelled
```

Human strings are formatted outside the worker/hot path. Diagnostics may include OS error text as context, but state transitions use error codes.

## CI strategy

The current CLI regressions are useful but mostly test parser/package/lifecycle contracts. Add a dedicated deterministic device-session test target rather than stuffing every state case into `main.cpp`.

Recommended structure:

- `arstack_studio_session_tests` using Qt Test or a small deterministic harness;
- scripted/fake transport event source (production + test seam is justified because the real worker is asynchronous and hardware-independent state transitions require deterministic testing);
- no actual COM/ESP32 required for state tests.

CI must lock at least:

1. current identity -> Ready path;
2. legacy identity -> Update path;
3. identity timeout -> Unidentified, never Install;
4. explicit Recover -> ROM probe allowed;
5. PermissionDenied -> session retired;
6. stale generation -> ignored;
7. profile timeout/rejection -> terminal error;
8. unplug READY/RUNNING -> Disconnected;
9. replug -> Ready, never auto-Start;
10. firmware tool cannot own port before release acknowledgement;
11. shutdown retires worker/thread/process;
12. second process lock rejects duplicate instance.

Keep existing reference-template, firmware manifest/hash/revision, lifecycle, headless QML and Windows package gates.

## Performance/resource contract

Measure rather than assume.

Targets to record during P0.5 acceptance:

- worker presence-poll CPU while idle;
- UI event-loop latency during serial telemetry and live edits;
- command queue high-water mark;
- dropped/coalesced live-setpoint count if instrumented;
- identity/reconnect latency distribution across at least repeated board resets;
- profile sync duration;
- firmware release/reacquire latency;
- ESP32 task stack high-water marks after identity/health additions;
- sustained 4000 fps publisher missed-slot/TX-failure telemetry.

No worker design may add allocation/log formatting to the ESP32 realtime transmit path.

## Hardware acceptance for the next RC

The next hardware RC is not accepted merely because firmware flashes.

Required sequence:

1. flash once;
2. close/reopen Studio 10 times -> 10/10 semantic identity, zero install prompts;
3. each open reaches READY or a bounded actionable error, never infinite Connecting/Preparing;
4. profile auto-sync succeeds repeatedly;
5. Start -> analyzer observes expected SV;
6. live magnitude/phase/frequency edit changes the transmitted stream;
7. Stop -> output stops;
8. unplug READY -> immediate Disconnected;
9. unplug RUNNING -> Studio Disconnected and firmware lease stops SV within bound;
10. replug -> auto identity/profile/Ready, no auto-Start;
11. reset board repeatedly including COM renumbering -> recover by device identity;
12. legacy firmware -> exactly one Update workflow;
13. blank board -> explicit Recover -> flash -> verified current identity;
14. hard-kill Studio while running -> firmware lease stops output;
15. close Studio normally -> no orphan process/thread/firmware tool.

## Immediate implementation decision

Do **S1 -> S2 -> S3 first**. These checkpoints directly address the failures seen on the physical board and are small enough to validate independently.

Only after their deterministic tests and short hardware RC pass should serial ownership move to a worker (S4/S5). This avoids debugging protocol grammar, profile transactions, USB thread affinity and Windows port ownership simultaneously.

No further visual polish or full OBS docking work should be mixed into these checkpoints.
