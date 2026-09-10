# ARStack Studio — P0 Smart UX / Zero-Configuration Progress

Target operator experience:

`Plug ESP32-P4 -> Studio detects/connects -> current firmware/default 4I+4V prepared -> READY -> edit values -> START/STOP`

The normal operator must not need to understand SCL Class A, profile deployment, APPID/MAC/VLAN, firmware files, SHA-256, espflash, COM-port internals, or PTP unless an exceptional condition requires it.

## Status legend

- ✅ implemented in branch
- 🟡 implemented but hardware/UX acceptance still required
- ⬜ not implemented yet

## Smart workflow

- ✅ Built-in 4I+4V / 4000 fps / 9-2LE reference profile loads automatically.
- ✅ Existing startup device discovery remains automatic.
- ✅ Hot-plug/replug watchdog retries discovery while the device is genuinely offline.
- ✅ After a compatible injector is verified, the default 4I+4V profile is synchronized/deployed automatically while output is stopped.
- ✅ `Deploy`, `Check`, `Quick Start`, and normal Engineering tabs are removed from the primary operator workflow.
- ✅ START remains an explicit operator action; the application never auto-starts SV output.
- ✅ Normal workflow exposes one large START or STOP action, not both competing at once.
- ✅ Phasor and waveform views start collapsed; operator can opt in when useful.
- 🟡 Automatic profile preparation must be hardware-tested across app launch, USB replug, STOP/START, and ESP power-cycle.

## Live values / responsiveness

- ✅ Current and voltage edits already apply to the connected firmware live.
- ✅ Magnitude and phase text edits are coalesced with a 90 ms debounce to avoid serial command storms while preserving immediate-feeling operation.
- ⬜ Frequency editing still needs the same coalescing policy.
- ⬜ Add bounded native command queue / last-value-wins protection so correctness does not depend on QML timing.

## Firmware intelligence

- ✅ Firmware image, manifest, SHA-256 contract, and `espflash` are bundled with the application package.
- ✅ Recovery can probe and flash ESP32-P4 without ESP-IDF/Python on the operator PC.
- ✅ ROM/download-mode recovery has guided BOOT/RESET fallback.
- 🟡 ESP-IDF project version is now pinned to 0.1.0 as the basis for semantic firmware identity.
- ⬜ Firmware `IDENTIFY` response still needs to expose semantic firmware version/capabilities.
- ⬜ DeviceController still needs to parse/store semantic firmware version.
- ⬜ Current-vs-outdated firmware comparison is not implemented yet.
- ⬜ Friendly `Firmware update available -> Update now?` notification is not implemented yet.
- ⬜ One-click update needs automatic transition to ROM flashing, visible progress, verify/reset/reconnect, and fallback only when automatic bootloader entry fails.

## Operator UI simplification

- ✅ Primary ribbon reduced to smart state + Balanced + Zero + optional views + Advanced + START/STOP.
- ✅ Low-level profile deployment is hidden from normal operation.
- ✅ Advanced/recovery functionality remains available rather than being deleted.
- ⬜ Main workspace title and legacy header/footer language still need simplification.
- ⬜ Remove duplicate device/status indicators and legacy keyboard-help noise from normal view.
- ⬜ Advanced window needs final progressive-disclosure cleanup and larger minimum typography.
- ⬜ Review default window density at 100%, 125%, 150%, and laptop resolutions.

## Robustness / crash resistance

- ✅ Native START policy still fails closed unless verified protocol/profile state is safe.
- ✅ Profile deployment remains stopped-only and fail-closed.
- ✅ Firmware package/target/hash checks remain fail-closed.
- ✅ Startup discovery race was removed; initial probe and hot-plug watchdog are serialized by state.
- ⬜ Move smart-session orchestration from QML into one C++ state machine (`NO_DEVICE / CONNECTING / UPDATE_REQUIRED / PREPARING / READY / RUNNING / ERROR`).
- ⬜ Add bounded retry/backoff for disconnect/reconnect and serial errors.
- ⬜ Add regression tests for USB removal during READY and RUNNING.
- ⬜ Add regression tests for malformed/slow serial responses and command timeouts.
- ⬜ Add soak test for continuous 4000 fps operation plus repeated live edits.

## Current checkpoint

Checkpoint A — **zero-configuration normal path** is implemented in the branch and awaiting CI + hardware acceptance.

Next implementation checkpoint: **semantic firmware identity + automatic update UX**, followed by moving session orchestration into native C++ and final visual cleanup.
