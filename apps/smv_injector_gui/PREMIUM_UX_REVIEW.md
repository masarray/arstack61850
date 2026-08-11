# Premium UX review — ESP32-P4 SMV Injector

This branch is intentionally an evaluation surface. Do not merge it into the production GUI until the injection workflow and visual direction are approved.

## V2 direction after first visual review

The first cockpit prototype still behaved too much like a web form: one long 8-channel table, per-row Apply buttons, wire-value clutter, a large frequency slider, and responsive stacking that moved the phasor/waveform below the injector on normal laptop widths.

V2 changes the working model rather than only polishing colors:

- Current and Voltage are separate 4-row matrices shown side-by-side.
- Magnitude and Phase are the only primary numeric columns.
- Wire counts/mdeg remain available in the selected-value detail instead of occupying a permanent column.
- Per-row Apply buttons are removed; Live Apply is the normal fast path and `Enter` commits when Live Apply is paused.
- Phasor and waveform remain beside the injector down to approximately 1010 px viewport width.
- Frequency is a compact direct-entry value with 50/60 Hz presets; the large slider is removed.
- A 3-phase link mode can couple A/B/C magnitude and preserve 120-degree phase relationships while editing.

## Keyboard-first matrix navigation

Numeric cells behave as an engineering matrix rather than independent HTML fields:

- `Arrow Up / Down` — previous / next channel in the same numeric column.
- `Arrow Left / Right` — move between Magnitude and Phase, wrapping to the adjacent row.
- `Enter` — commit when Live Apply is off, then move to the next channel.
- `Ctrl + Arrow Up / Down` — increment / decrement by the field step.
- `Ctrl + Shift + Arrow Up / Down` — coarse step (10×).
- `Tab / Shift+Tab` — normal sequential field navigation.
- Focusing a numeric cell selects its whole value so typing immediately replaces the old value.

The navigation wraps through `Ia → Ib → Ic → In → Ua → Ub → Uc → Un`, so repeated arrow-key operation never requires reaching for the mouse.

## Primary workflow to evaluate

1. Open the GUI.
2. Prepare values while the device is offline if desired.
3. Use the arrow keys to move through the injection matrix and type new values directly.
4. Optionally enable 3-phase link for balanced magnitude/angle editing.
5. Confirm the generated phasor and waveform without leaving the workspace.
6. Connect the ESP32-P4.
7. Use Live Apply for immediate setpoint updates, or pause Live Apply and use Enter / Apply All.
8. Start live injection from the persistent bottom action bar.

## Visual intent

The target is a precision engineering instrument, not a dashboard and not a gaming UI:

- compact hierarchy
- graphite surfaces
- restrained accent color
- small tabular numeric typography
- thin separators
- minimal radius and shadow
- no glass blur or decorative glow
- selected signal color is used for data identity, not decoration
- phasor and waveform are part of the working surface, not afterthought cards

## Visualization truthfulness

Phasor and waveform are generated setpoint previews. They visualize what the configured signal generator is intended to produce. They are not Ethernet on-wire proof. Future observed/captured data must be clearly separated from generated data.

## Review checklist

Please judge the branch on these questions:

- Can an engineer change many Ia/Ib/Ic/Ua/Ub/Uc values without touching the mouse?
- Can the operator understand the active injection in one glance?
- Are Current and Voltage easy to compare independently?
- Do phasor and waveform remain visible at the laptop viewport actually used on the bench?
- Is the UI calm and premium rather than card-heavy or browser-like?
- Is Start/Stop always obvious without dominating the entire screen?
- Is secondary protocol information available without slowing the primary workflow?

## Scope

GUI only. No firmware timing, SV packet layout, or serial command protocol is changed by this evaluation branch.
