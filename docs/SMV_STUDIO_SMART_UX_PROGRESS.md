# ARStack Studio — P0 Smart UX / Zero-Configuration Progress

Target operator experience:

`Plug ESP32-P4 -> Studio detects/connects -> firmware checked -> default 4I+4V prepared -> READY -> edit values -> START/STOP`

The normal operator must not need to understand SCL Class A, profile deployment, APPID/MAC/VLAN, firmware files, SHA-256, espflash, COM-port internals, or PTP unless an exceptional condition requires it.

## Status legend

- ✅ implemented in branch
- 🟡 implemented but CI/hardware/UX acceptance still required
- ⬜ not implemented yet

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

## Live values / responsiveness

- ✅ Current and voltage edits apply to connected firmware live.
- ✅ Magnitude and phase text edits are coalesced with a 90 ms UI debounce to avoid serial command storms while preserving immediate-feeling operation.
- ✅ Frequency and signal writes now pass through a bounded native last-value-wins queue in `StudioDeviceController`.
- ✅ Native queue capacity is bounded by design to one pending frequency plus the eight fixed 4I+4V channel IDs; it cannot grow with repeated typing.
- ✅ Native live writes flush on a short timer, and START forces the newest queued values to the device before enabling output.
- ✅ Disconnect/unverify clears pending live writes; ZERO cancels pending channel writes so an old queued value cannot reappear after zeroing.
- 🟡 Native live-write coalescing still needs hardware feel/latency acceptance during rapid edits.

## Firmware intelligence

- ✅ Firmware image, manifest, SHA-256 contract, and `espflash` are bundled with the application package.
- ✅ Recovery can probe and flash ESP32-P4 without ESP-IDF/Python on the operator PC.
- ✅ ROM/download-mode recovery has guided BOOT/RESET fallback.
- ✅ ESP-IDF project version is pinned to `0.1.0` for deterministic semantic firmware identity.
- ✅ New firmware `IDENTIFY` appends `firmware=<version>` and `capabilities=SMV-4I4V,LIVE-SETPOINTS` while preserving the older identity prefix for backwards compatibility.
- ✅ Smart session distinguishes current firmware from legacy/outdated firmware. Missing semantic version is treated as legacy rather than silently accepted.
- ✅ Friendly `Firmware update -> Update now?` operator prompt is implemented.
- 🟡 One-click update coordinator is implemented: safe STOP if required -> release serial -> ROM probe -> verified bundled flash -> reset -> reconnect -> verify semantic identity -> prepare 4I+4V.
- 🟡 If automatic ROM entry fails, normal UI falls back to BOOT + RESET guidance and `Retry update`.
- 🟡 Update UI currently uses an indeterminate progress bar. Actual numeric flash percentage is not parsed yet.
- ⬜ Hardware acceptance of legacy-firmware -> one-click update -> reconnect -> READY is still required.
- ⬜ Centralize the Firmware Manager instance used by Smart Session and Advanced configuration to avoid duplicate package validation objects.

## Operator UI simplification

- ✅ Primary ribbon reduced to smart state + Balanced + Zero + optional views + Advanced + START/STOP.
- ✅ Low-level profile deployment is hidden from normal operation.
- ✅ Advanced/recovery functionality remains available rather than being deleted.
- ✅ Phasor/waveform visual noise is opt-in at startup rather than occupying the default workspace.
- ✅ Firmware internals remain hidden during the normal update path; only exceptional BOOT/RESET recovery is surfaced.
- ⬜ Main workspace title and legacy header/footer language still need simplification.
- ⬜ Remove duplicate device/status indicators and legacy keyboard-help noise from normal view.
- ⬜ Advanced window needs final progressive-disclosure cleanup and larger minimum typography.
- ⬜ Review default window density at 100%, 125%, 150%, and laptop resolutions.

## Robustness / crash resistance

- ✅ Native START policy fails closed unless the device is verified, protocol is supported, semantic firmware identity matches the Studio build, and a profile is armed.
- ✅ Hidden/legacy Start and Deploy shortcuts cannot bypass a required firmware update merely because an older firmware still reports protocol v1.
- ✅ Profile deployment remains stopped-only and fail-closed.
- ✅ Firmware package/target/hash checks remain fail-closed.
- ✅ Startup discovery race was removed; initial probe and hot-plug watchdog are serialized by state.
- ✅ Smart-session orchestration moved into one native C++ state machine (`WAITING FOR DEVICE / DEVICE FOUND / CONNECTING / FIRMWARE UPDATE / UPDATING FIRMWARE / UPDATE NEEDS BOOT / PREPARING 4I+4V / READY / RUNNING / ERROR`).
- ✅ Reconnect preparation is delayed briefly after identity verification so existing SHOW / PROFILE SHOW responses can settle before automatic profile synchronization.
- ✅ Firmware update completion is not trusted until the newly reconnected firmware reports the expected semantic version.
- ✅ Firmware update state exits deterministically on probe rejection/failure instead of hanging indefinitely.
- ✅ Live-edit write backlog is bounded and last-value-wins instead of growing with operator keystrokes.
- ⬜ Add bounded retry/backoff for disconnect/reconnect and serial errors.
- ⬜ Add regression tests for USB removal during READY and RUNNING.
- ⬜ Add regression tests for malformed/slow serial responses and command timeouts.
- ⬜ Add soak test for continuous 4000 fps operation plus repeated live edits.

## Current checkpoint

Checkpoint C — **zero-configuration workflow + semantic firmware update + bounded native live-write coalescing** is implemented in the branch.

Current validation state: **CI and hardware acceptance pending for the newest Smart UX head.** Do not treat this checkpoint as release-ready until those gates pass.

Next implementation checkpoint after CI: final main-window visual cleanup, centralize firmware-manager ownership, bounded reconnect/backoff, then hardware acceptance of both current-firmware and legacy-firmware paths.
