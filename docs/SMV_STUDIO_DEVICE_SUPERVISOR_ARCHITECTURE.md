# ARStack Studio Device Supervisor Architecture

Status: P0.5 design contract before the next physical-hardware RC.

## Goal

ARStack Studio must behave like a professional test instrument rather than a serial terminal with UI state layered on top. The operator should not need to understand COM ports, ROM mode, profile deployment, or firmware handoff during the normal path.

The required normal experience is:

1. plug in the ESP32-P4;
2. Studio discovers it;
3. Studio proves ARStack semantic identity and firmware compatibility;
4. Studio synchronizes the selected 4I+4V profile;
5. Studio reports READY;
6. the operator presses Start.

Recovery is a separate, explicit path. A transient serial-handshake failure must never be interpreted as proof that firmware is absent.

This design follows `AGENTS.md`: one state authority, explicit bounded state machines, deterministic ownership, no arbitrary retry loops, bounded queues, structured status, clean shutdown, and physical validation before release.

## Current root causes this architecture removes

The current P0 path has four architectural weaknesses found during physical testing:

- semantic firmware identity is scraped from human-readable `logText`, so product state depends on diagnostics text;
- `QSerialPort` and the external firmware tool do not have an explicit exclusive-port ownership barrier across reset/re-enumeration;
- blank-board recovery can be inferred after an ordinary identity timeout, which can misclassify installed firmware as absent;
- profile synchronization has no terminal timeout/transaction generation, so a lost/rejected response can leave Studio in PREPARING forever.

These are control-plane issues; they must be fixed before further UI polish.

## Architecture

```text
QML / presentation
        |
        v
DeviceSupervisor (single state authority, UI thread)
        |
        +----------------------+----------------------+
        |                      |                      |
        v                      v                      v
DeviceIoWorker          FirmwareWorker          Profile service
QThread                 QThread / async QProcess  transaction state
owns QSerialPort        owns espflash only        uses DeviceIoWorker
        |
        v
USB / serial adapter
        |
        v
ESP32-P4 ARStack firmware
```

Only `DeviceSupervisor` decides the operator-visible state. Workers report structured events; they do not directly decide whether the product is READY, needs firmware, or may Start.

## 1. DeviceSupervisor: one source of truth

Do not keep growing a single flat string state. Maintain a typed context with orthogonal sub-states:

```text
PhysicalState : absent | present | reenumerating
TransportState: closed | opening | handshaking | online | degraded | fault
IdentityState : unknown | arstack_current | arstack_legacy | foreign
FirmwareState : idle | update_available | recovering | flashing | verifying | failed
ProfileState  : none | syncing | armed | failed
OutputState   : stopped | starting | running | stopping
```

The QML presentation is derived from this context by a single priority function. QML must never derive independent device truth from COM availability, log strings, or individual worker booleans.

Every asynchronous connection attempt receives a monotonically increasing `sessionGeneration`. Worker events include this generation. Events from an old connection/reset are ignored after a new generation begins. This prevents late callbacks from a previous COM instance changing current state.

## 2. DeviceIoWorker: exclusive owner of QSerialPort

Use one long-lived worker thread for serial/device I/O, not one thread per request.

Responsibilities:

- enumerate/observe serial candidates;
- open and close the selected port;
- own all `QSerialPort` reads/writes/timers;
- perform bounded semantic handshake;
- maintain control-session liveness;
- serialize configuration transactions;
- report structured transport errors;
- immediately report port disappearance;
- release the port on supervisor request and confirm `portReleased` before firmware tools may start.

The GUI thread never directly opens the COM port.

### Bounded identity handshake

After opening a candidate port:

1. wait for a short USB/CDC settle event/timer;
2. send `IDENTIFY`;
3. retry at a bounded interval, for example 4-5 attempts over about 2-3 seconds;
4. accept only a machine-readable ARStack identity frame;
5. otherwise close the port and report `IdentityTimeout`.

An identity timeout means **unknown device state**, not `firmware missing`.

## 3. Typed firmware identity: stop parsing diagnostics logs

`DeviceController`/worker must expose identity as typed data, not by searching `logText`:

```text
DeviceIdentity {
    product
    target
    protocolVersion
    firmwareVersion
    deviceId
    bootId
    capabilities
}
```

Recommended firmware response/announcement:

```text
ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 \
  device_id=<efuse-id> firmware=<semver> boot_id=<boot-nonce> \
  capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO
```

The existing EFUSE-derived `device_id` remains the stable physical identity. Add a per-boot `boot_id` so Studio can distinguish a reboot/reflash from a delayed line belonging to the previous session.

The firmware should emit its identity once when the control task is actually ready, and must still answer `IDENTIFY` on demand. Startup announcement is assistance; the explicit request/response remains authoritative.

Human logs remain diagnostics only.

## 4. USB insertion/removal and re-enumeration

Two signals are used together:

- serial transport errors (`ResourceError`, device removed, invalid handle/access loss);
- platform USB/COM presence observation.

On Windows, prefer a native device-change adapter where practical, with bounded `QSerialPortInfo` polling as a portable fallback. The supervisor must not key a device only by `COM3`, because the COM number can change after reset. Before semantic identity is known, use USB metadata as a candidate hint; after identity, use ARStack `device_id` as the authoritative device identity.

### Unplug behavior

If USB disappears at any time:

- cancel pending profile/config transactions;
- mark the serial session generation dead;
- close/release handles;
- transition immediately to `DEVICE DISCONNECTED`;
- never show READY from cached state;
- if SV was running, rely on the firmware session lease to stop transmission within the bounded lease window.

### Replug behavior

When the same board returns:

- reacquire it automatically;
- re-run semantic identity;
- if firmware is current, re-synchronize the volatile runtime profile;
- return to READY;
- never auto-Start output after a reconnect.

## 5. Explicit serial-port ownership arbitration

Introduce a single ownership enum:

```text
PortOwner: none | device_session | firmware_tool
```

Firmware flashing may begin only after this sequence:

```text
DeviceSupervisor
  -> DeviceIoWorker.closeAndRelease()
  <- portReleased(sessionGeneration)
  -> FirmwareWorker.probe/flash()
```

After flashing:

```text
FirmwareWorker flash complete
  -> reset
  -> PortOwner = none
  -> wait for USB disappearance/reappearance or bounded settle
  -> DeviceIoWorker reacquires candidate
  -> semantic ARStack identity verification
  -> current firmware + matching target/device proof
  -> only then report firmware installation SUCCESS
```

This prevents `Access is denied` races caused by Studio and `espflash` competing for the same Windows COM handle.

A second Studio process must also be blocked by a single-instance lock so two applications cannot own the same injector concurrently.

## 6. Firmware decision policy

Normal startup must never automatically invoke ROM probing merely because application identity was late.

Decision table:

| Semantic result | Operator state | Action |
| --- | --- | --- |
| Current ARStack identity | Preparing / Ready | Never offer install |
| ARStack identity, old firmware/protocol | Firmware update available | Offer Update |
| No ARStack identity after bounded retries | Device not identified | Retry / Recover firmware action |
| User explicitly selects Recover/Install | Firmware recovery | Release serial port, then ROM-probe |
| ROM proves supported ESP32-P4 | Firmware recovery ready | Allow flash |
| ROM reports different chip | Unsupported device | Block flash |
| No ROM response | Download-mode help | Explicit Retry only |

`espflash board-info` proves chip/ROM identity. It does **not** prove that application firmware is absent.

## 7. FirmwareWorker

The existing integrity rules remain mandatory: manifest schema, target chip, pre-v3 revision policy, image SHA-256, flash offset, and bundled flasher are verified before write.

Move process ownership behind an asynchronous worker/service boundary so UI responsiveness does not depend on process startup, Defender scanning, output parsing, or reset delays.

Firmware operations remain bounded:

- launch timeout;
- ROM probe timeout;
- flash timeout;
- reset timeout;
- cancellation/shutdown;
- one terminal result per attempt.

No automatic infinite retry. Recoverable bootloader failures expose one explicit Retry action.

## 8. Profile synchronization must be a transaction

`PREPARING` may never be an unbounded state.

For every profile deployment:

- snapshot the previous firmware profile generation;
- begin one transaction;
- send the bounded command sequence;
- accept success only when `PROFILE committed generation=N` / armed confirmation has a generation newer than the snapshot;
- enforce a bounded completion timeout (for example 3-5 seconds on the local serial path);
- on rejection/timeout, terminate the transaction;
- perform at most one controlled reconnect/retry when policy allows;
- otherwise enter `PROFILE SYNC ERROR` with an explicit Retry action.

`profileSyncInFlight` must always have a matching completion/failure/timeout transition. No boolean may stay true forever.

On every boot/reconnect, the firmware runtime profile is considered volatile and Studio may re-deploy it automatically after semantic identity is verified. Output remains STOPPED until the operator presses Start.

## 9. Liveness/health monitoring

Keep the high-rate lease heartbeat quiet and bounded, but separate lease maintenance from positive device-health proof.

Recommended control-plane liveness:

- `HEARTBEAT` continues to maintain the 2.5 s firmware fail-safe lease while Studio owns a running session;
- a lower-rate `PING`/`PONG` or equivalent structured status request verifies that the control path is still responsive;
- after a bounded number of missed health replies, transition `online -> degraded -> reconnecting`;
- do not preserve READY/RUNNING presentation from stale values.

The realtime SV publisher must remain independent from desktop UI work and keep the current fail-safe lease behavior.

## 10. Command queue and backpressure

One bounded command path goes through `DeviceIoWorker`.

Suggested semantics:

- control/config commands: ordered, lossless within a small bounded queue; reject explicitly on overflow;
- live setpoints: last-value-wins/coalesced by channel, as already intended;
- telemetry/presentation: coalesce stale intermediate events;
- firmware/profile transactions: exclusive, no unrelated config commands interleaved.

No unbounded signal/event queues and no worker per packet/event.

## 11. Single-instance desktop ownership

Before creating the device worker, acquire a single-instance application lock. A second ARStack Studio instance must not silently open another handle to the same COM device. Preferred UX: notify the already-running instance or show `ARStack Studio is already running` and exit.

This is part of device ownership, not cosmetic UX.

## 12. Operator-facing state model

The operator should see only actionable states:

- Waiting for device
- Identifying device
- Firmware update available
- Firmware recovery required
- Installing firmware
- Reconnecting device
- Preparing 4I+4V
- Ready to inject
- Running
- Device disconnected
- Setup issue / Profile sync issue

Every non-transient state must expose a clear next action. There must be no permanent `Connecting...` or `Preparing...` state.

## 13. Tests required before the next hardware RC

### Deterministic desktop tests

- current semantic identity -> never opens firmware recovery;
- legacy identity -> offers Update;
- identity timeout -> does not claim blank firmware;
- stale event from old session generation -> ignored;
- port disappears while READY -> DISCONNECTED;
- port disappears while RUNNING -> DISCONNECTED and no auto-restart;
- firmware worker cannot start until serial worker confirms port release;
- profile commit timeout -> PROFILE SYNC ERROR, not infinite PREPARING;
- profile rejection -> terminal profile failure;
- second application instance -> blocked;
- shutdown -> workers cancel/join and process exits.

### Windows integration tests

- simulated COM open/access error maps to transport loss;
- release/reacquire ownership around firmware tool is ordered;
- re-enumerated COM number can change without losing physical-device workflow;
- packaged installer and portable builds keep the same lifecycle behavior.

### Physical ESP32-P4 acceptance

1. current firmware: open Studio 10 times -> 10/10 identifies without offering Install;
2. unplug/replug while READY -> auto-recover to READY, never auto-Start;
3. unplug while RUNNING -> firmware stops by lease; Studio shows DISCONNECTED;
4. legacy firmware -> one Update -> current identity -> Ready;
5. blank board -> explicit Recover -> flash -> current identity -> Ready;
6. flash then restart Studio -> current identity, no repeated install prompt;
7. profile synchronization -> bounded Preparing -> Ready;
8. Start/Stop -> sustained 4000 fps, setpoint edits, no missed control transition;
9. hard-kill Studio while RUNNING -> SV stops after lease expiry;
10. repeated reset/re-enumeration soak test.

## 14. Implementation order

Do not attempt all UI and control changes in one patch. Implement in this order:

1. typed `DeviceIdentity` and firmware boot identity announcement;
2. `DeviceIoWorker` + session generation + bounded handshake;
3. serial/firmware `PortOwner` handoff and single-instance lock;
4. remove automatic ROM-probe classification from normal startup;
5. bounded profile transaction with timeout/failure state;
6. USB removal/re-enumeration recovery;
7. lower-rate health monitoring;
8. QML presentation mapped only from `DeviceSupervisor`;
9. deterministic CI regressions;
10. physical acceptance and only then release/merge.

## Product behavior reference

a commercial device-control application is used here only as a product-behavior reference: integrated device/software control, coherent status, live observation, and actionable device diagnostics. ARStack must not imitate proprietary protocols or vendor-specific traces. The implementation remains based on ARStack's explicit IEC 61850 and device-control contracts.
