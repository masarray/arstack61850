# Premium UX review — ESP32-P4 SMV Injector

This branch is intentionally an evaluation surface. Do not merge it into the production GUI until the injection workflow and visual direction are approved.

## V3 visual direction

The second review confirmed the keyboard workflow is moving in the right direction, but the screen still felt visually noisy and the generated phasor/waveform did not read strongly enough during normal bench use.

V3 keeps the V2 matrix interaction intact and focuses on calm premium instrument art direction:

- one UI type stack: Inter -> Plus Jakarta Sans -> native system fallback, with monospace reserved for engineering numbers;
- larger and more consistent breathing space between working groups;
- borders reduced to secondary separators instead of outlining every object;
- idle numeric fields use low-contrast tonal surfaces and only become strongly framed on focus;
- source/runtime information is intentionally quieter than the injection workspace;
- Current and Voltage remain separate side-by-side matrices;
- the command strip becomes a tonal toolbar instead of another bordered card;
- the preview stays beside the injection matrix down to a narrower viewport before stacking;
- phasor vectors and waveform traces use stronger strokes, round line caps, and higher non-active contrast;
- no blur, glassmorphism, decorative glow, chart dependency, or high-rate animation is introduced.

The repository does not currently contain a local Inter or Plus Jakarta font asset, so this review branch does not add a network font dependency. The stack prefers either font when available and remains fully offline. A vendored font binary can be added separately only after the project decides its font packaging/license policy.

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

## Visualization truthfulness

Phasor and waveform are generated setpoint previews. They visualize what the configured signal generator is intended to produce. They are not Ethernet on-wire proof. Future observed/captured data must be clearly separated from generated data.

## Review checklist

Please judge this V3 branch on these questions:

- Can an engineer change many Ia/Ib/Ic/Ua/Ub/Uc values without touching the mouse?
- Does the screen feel calmer, more expensive, and less like a browser form?
- Do Current and Voltage read as the central task instead of being surrounded by competing metadata?
- Are the phasor vectors and waveform traces immediately visible without squinting?
- Does Signal Preview remain beside the injector at the actual browser zoom used on the bench?
- Is Start/Stop always obvious without dominating the entire screen?
- Is secondary protocol information available without slowing the primary workflow?

## Scope

GUI only. No firmware timing, SV packet layout, or serial command protocol is changed by this evaluation branch. The PR remains draft and unmerged until the visual direction is approved.
