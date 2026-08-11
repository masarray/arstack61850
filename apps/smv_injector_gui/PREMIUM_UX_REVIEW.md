# Premium UX review build

This branch is intentionally an evaluation build for the ESP32-P4 SMV Injector GUI. It does not change the firmware command protocol and it is not intended to be merged until the operator workflow and visual direction are approved.

## Design target

- premium, clean engineering-instrument appearance rather than a dashboard
- no-scroll primary workflow on normal engineering laptops
- fast 4I + 4V value entry in a compact precision grid
- persistent generated phasor and waveform visualization
- clear separation between generated setpoint preview and future on-wire observation
- device/profile/runtime status always visible
- Start/Stop always in one fixed location
- minimal visual effects and no heavy chart dependencies

## Primary workflow

1. Connect the ESP32-P4.
2. Optionally load SCL/CID engineering data.
3. Edit magnitude, phase, and frequency directly in the injection grid.
4. Confirm the generated phasor and 40 ms waveform preview.
5. Keep Live apply enabled for immediate updates or disable it and use Apply / Apply all.
6. Press Start live.
7. Observe runtime rate, missed slots, TX failures, and generation counter.
8. Press Stop from the fixed action bar.

Balanced and Zero can be used while disconnected so a test state can be prepared and visually reviewed before the device is armed.

## Visualization truthfulness

The phasor and waveform are labelled **Generated / SETPOINT**. They are calculated from the configured magnitude, phase, enable state, and frequency. They show what the injector is configured to generate; they are not presented as proof of frames observed on the Ethernet wire.

A later monitor path can add an **Observed** trace and generated-vs-observed comparison without changing the main cockpit layout.

## Keyboard / fast-entry behavior

- `Tab` moves through value fields naturally.
- Double-click selects the complete numeric value.
- `Enter` applies the active channel when a device is connected.
- Number inputs retain native arrow-key fine adjustment.
- Clicking/focusing a row makes that channel visually dominant in the phasor and waveform previews.

## Performance approach

The prototype remains dependency-free HTML/CSS/JavaScript. Visualization uses two canvas surfaces and redraws only on state changes or resize through `requestAnimationFrame`. Device serial traffic and the firmware realtime publisher remain unchanged.

## Review focus

Please judge this PR primarily on:

- visual quality / premium feel
- density and readability
- speed of the injection workflow
- usefulness of persistent phasor + waveform
- whether profile/status information is visible without competing with the injection task
- whether the layout is a good visual foundation for the later Qt/QML desktop implementation

## Run

Use the existing launcher from the repository root:

```powershell
.\apps\smv_injector_gui\run.cmd
```

The same Web Serial requirements and firmware compatibility rules as the existing GUI apply.
