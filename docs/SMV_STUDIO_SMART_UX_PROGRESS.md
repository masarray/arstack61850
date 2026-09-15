# ARStack Studio — P0 Smart UX / Zero-Configuration Progress

Target operator experience:

`Plug ESP32-P4 -> Studio detects/connects -> firmware checked/installed when needed -> default 4I+4V prepared -> READY -> edit values -> START/STOP`

The normal operator must not need to understand SCL Class A, profile deployment, APPID/MAC/VLAN, firmware files, SHA-256, espflash, COM-port internals, or PTP unless an exceptional condition requires it.

## Status legend

- ✅ implemented in branch
- 🟡 implemented but CI/hardware/UX acceptance still required
- ⬜ not implemented yet

## Production engineering contract

- ✅ `AGENTS.md` production engineering contract from `main` is carried into the P0 branch.
- ✅ Firmware onboarding follows the existing ownership boundaries: `DeviceController` owns serial discovery, application-wide `FirmwareService` owns package/probe/flash state, and `SmartSessionController` owns the operator state machine.
- ✅ No second firmware parser, second flash service, or QML-owned device state machine was added.
- ✅ Firmware process execution is asynchronous; the UI thread no longer waits synchronously for espflash process start/termination.
- ✅ Process startup is bounded to 2.5 s and firmware operation output is bounded/truncated rather than growing without limit.

## Smart workflow

- ✅ Built-in 4I+4V / 4000 fps / 9-2LE reference profile loads automatically.
- ✅ Startup device discovery is automatic.
- ✅ Hot-plug/replug watchdog retries discovery while the device is genuinely offline.
- ✅ After a compatible injector is verified, the default 4I+4V profile is synchronized/deployed automatically while output is stopped.
- ✅ Session-owned profile sync is required before READY; a stale `profileArmed` state from an earlier session is never trusted.
- ✅ `Deploy`, `Check`, `Quick Start`, and normal Engineering tabs are removed from the primary operator workflow.
- ✅ START remains an explicit operator action; the application never auto-starts SV output.
- ✅ Normal workflow exposes one large START or STOP action, not both competing at once.
- ✅ Phasor and waveform views start collapsed; operator can opt in when useful.
- ✅ Normal session orchestration lives in native `SmartSessionController` instead of ribbon timing logic.
- 🟡 Zero-configuration preparation must still be hardware-tested across app launch, USB replug, STOP/START, and ESP power-cycle.

## Blank / unconfigured ESP32-P4 onboarding

- ✅ After normal ARStack identity discovery fails, Studio may perform a ROM-level check only on one unique high-confidence Espressif candidate; arbitrary COM ports are not auto-flashed.
- ✅ A ROM-verified supported ESP32-P4 that has no working ARStack identity becomes an explicit `FIRMWARE REQUIRED` operator state.
- ✅ Primary UI automatically offers a novice `Set up ESP32-P4` dialog; the user does not select `.bin` files or need ESP-IDF/Python/CLI tools.
- ✅ User approval remains explicit before any flash starts.
- ✅ `Install firmware` re-verifies the ESP32-P4 target before writing; earlier detection is not blindly trusted.
- ✅ Flash is blocked for another ESP chip, unsupported revision, invalid/missing manifest, invalid offset, missing image, hash mismatch, or missing bundled flasher.
- ✅ The target policy is explicit `ESP32-P4 pre-v3`; deterministic regression coverage accepts v0/v1/v2 and rejects v3+ or invalid revision data.
- ✅ CI negative contract also rejects a firmware manifest whose target chip is changed away from `esp32p4`.
- ✅ Firmware installation uses a modal progress popup. Real espflash percentages render 0..100; probe/reset/reconnect phases remain safely indeterminate.
- ✅ Download-mode recovery is shown in the same popup only when exceptional BOOT/RESET help is needed.
- ✅ A large final result popup explicitly reports `Firmware installed successfully` only after post-flash reconnect and semantic ARStack firmware identity verification.
- ✅ Flash tool success by itself is not treated as final installation success.
- ✅ Failure/cancellation paths exit deterministically instead of leaving the Smart Session in an indefinite flashing state.
- 🟡 Physical blank-board acceptance is still required: factory/blank ESP32-P4 -> prompt -> install -> reset -> reconnect -> semantic identity -> READY.
- 🟡 Physical Download-mode fallback and retry still need acceptance on the target board/USB driver combination.

## Live values / responsiveness

- ✅ Current and voltage edits apply to connected firmware live.
- ✅ Magnitude and phase text edits are coalesced with a 90 ms UI debounce to avoid serial command storms while preserving immediate-feeling operation.
- ✅ Frequency and signal writes pass through a bounded native last-value-wins queue in `StudioDeviceController`.
- ✅ Native queue capacity is bounded by design to one pending frequency plus the eight fixed 4I+4V channel IDs; it cannot grow with repeated typing.
- ✅ Native live writes flush on a short timer, and START forces the newest queued values to the device before enabling output.
- ✅ Disconnect/unverify clears pending live writes; ZERO cancels pending channel writes so an old queued value cannot reappear after zeroing.
- ✅ Numeric controls use a stronger visual hierarchy because injection values are the primary operator task.
- ✅ Current/voltage matrices use larger channel labels, roomier rows, simple `Channel / Value / Phase` headings, and clearer validation messages.
- 🟡 Native live-write coalescing still needs hardware feel/latency acceptance during rapid edits.

## Firmware intelligence

- ✅ Firmware image, manifest, SHA-256 contract, and `espflash` are bundled with the application package.
- ✅ Recovery can probe and flash ESP32-P4 without ESP-IDF/Python on the operator PC.
- ✅ ROM/download-mode recovery has guided BOOT/RESET fallback.
- ✅ ESP-IDF project version is pinned to `0.1.0` for deterministic semantic firmware identity.
- ✅ New firmware `IDENTIFY` appends `firmware=<version>` while preserving the older identity prefix for backwards compatibility.
- ✅ Firmware capabilities advertise `SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE` when the independent lease timer is available.
- ✅ Smart session distinguishes current firmware from legacy/outdated firmware. Missing semantic version is treated as legacy rather than silently accepted.
- ✅ Friendly firmware-update prompt is implemented with concise operator wording.
- 🟡 One-click update coordinator is implemented: safe STOP if required -> release serial -> ROM probe -> verified bundled flash -> reset -> reconnect -> verify semantic identity -> prepare 4I+4V.
- 🟡 If automatic ROM entry fails, normal UI falls back to BOOT + RESET guidance and `Retry`.
- ✅ Smart Session and Advanced share one application-wide `FirmwareService`; package/probe/progress state has a single authority.
- ✅ Firmware Manager parses real espflash percentage tokens when available and exposes `flashProgress` 0..100; unknown formats safely remain indeterminate.
- ✅ Packaged firmware contract regression-checks target parsing, pre-v3 revision policy, progress parsing, SHA-256 rejection, and wrong-chip manifest rejection.
- 🟡 Hardware acceptance of legacy-firmware -> one-click update -> reconnect -> READY is still required.

## Operator UI simplification

- ✅ Primary ribbon reduced to smart state + Balanced + Zero + optional views + Advanced + START/STOP.
- ✅ Smart-session enum names are translated to operator-facing states such as `Connect device`, `Checking device`, `Firmware required`, `Installing firmware`, `Preparing`, `Ready`, and `Running`.
- ✅ Firmware update/install dialogs hide bootloader/SHA/protocol details unless recovery fails.
- ✅ Low-level profile deployment is hidden from normal operation.
- ✅ Advanced/recovery functionality remains available rather than being deleted.
- ✅ Phasor/waveform visual noise is opt-in at startup rather than occupying the default workspace.
- ✅ Firmware update/install failure surfaces a concise operator result instead of silently dropping back to an idle state.
- ✅ Advanced configuration header/typography is simplified and enlarged; tabs are `Injection / Firmware / Waveform / Timing / Device` with one `Device ready / Setup mode` status pill.
- ✅ Telemetry is collapsed by default and reduced to one quiet status line; expanded mode contains only `Recent activity` and `Transmission` instead of duplicating channel state.
- ✅ Main action ribbon uses human-readable state labels instead of raw state-machine names and keeps Ready/Running copy intentionally short.
- ✅ Injection value fields and current/voltage matrices are visually promoted above chrome/status elements.
- ⬜ Main workspace title and remaining legacy top-level header/footer language still need simplification in `Main.qml`.
- ⬜ Remove the remaining duplicate top-level device indicator and keyboard-help noise from `Main.qml` normal view.
- ⬜ Review default window density at 100%, 125%, 150%, and laptop resolutions after a packaged build is available.

## Robustness / crash resistance

- ✅ Native START policy fails closed unless the device is verified, protocol is supported, semantic firmware identity matches the Studio build, and a profile is armed.
- ✅ Hidden/legacy Start and Deploy shortcuts cannot bypass a required firmware update merely because an older firmware still reports protocol v1.
- ✅ Profile deployment remains stopped-only and fail-closed.
- ✅ Firmware package/target/hash checks remain fail-closed.
- ✅ Startup discovery race was removed; initial probe and hot-plug watchdog are serialized by state.
- ✅ Smart-session orchestration is centralized in native C++.
- ✅ Firmware update completion is not trusted until the newly reconnected firmware reports the expected semantic version.
- ✅ Firmware process startup, update reconnect, and discovery retry behavior are bounded; no unbounded firmware wait/retry loop was added.
- ✅ Post-flash reconnect is bounded to six retry windows and waits for active discovery/verification instead of launching overlapping probes.
- ✅ Live-edit write backlog is bounded and last-value-wins instead of growing with operator keystrokes.
- ✅ Normal application shutdown performs a best-effort STOP before the control process exits while output is RUNNING.
- 🟡 Studio maintains a silent 700 ms control heartbeat only for current semantic firmware; legacy firmware is not spammed with an unknown command.
- 🟡 Studio sends a fresh heartbeat synchronously before START; a Studio-owned START fails closed if that control-session write cannot be established.
- 🟡 Firmware implements a 2.5 s session lease using an independent one-shot `esp_timer`, so lease expiry does not depend on serial input continuing after a GUI crash.
- 🟡 Lease timeout handles millisecond-boundary rounding by rearming the remaining interval rather than silently losing the one-shot.
- 🟡 If the firmware lease timer cannot be created/rearmed, a fresh Studio-owned START is rejected or an already-running Studio session is fail-stopped.
- ✅ Manual/bench START remains backwards compatible: without a fresh Studio heartbeat, the firmware does not force a session lease.
- 🟡 General hot-plug discovery remains periodic and lightweight; hardware acceptance is still needed for repeated unplug/replug cycles.
- ⬜ Hardware kill test: terminate Studio while RUNNING and verify external SMV capture stops within approximately 2.5–3 s.
- ⬜ Hardware USB-removal tests during READY and RUNNING.
- ⬜ Regression tests for malformed/slow serial responses and command timeouts.
- ⬜ Soak test for continuous 4000 fps operation plus repeated live edits.

## Current checkpoint

Checkpoint H — **zero-configuration workflow + novice blank-board firmware onboarding + verified popup progress/result + asynchronous bounded FirmwareManager + semantic firmware update + bounded live writes/reconnect + operator-first visual hierarchy + Studio/firmware control-session lease** is implemented in the branch.

Current validation state: **latest-head CI and physical hardware acceptance are pending.** Do not describe blank-board installation, session lease, or this checkpoint as hardware-proven until the Qt/firmware/release builds pass and the physical blank-board/update/hard-kill/USB tests are completed.

Next checkpoint after CI: fix any concrete build/QML regression first; then finish the remaining `Main.qml` top-level chrome cleanup, build a new Windows RC, and run blank-board install, current-firmware, legacy-update, hard-kill, USB-replug, and sustained 4000 fps hardware acceptance.
