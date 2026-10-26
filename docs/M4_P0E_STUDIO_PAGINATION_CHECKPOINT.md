# M4 P0E — One Studio window, three independent SCD-backed MU draft pages

**Scope:** Native Qt/QML GUI milestone. **Draft only — no M4 firmware deployment and no physical output claim.** All existing single-SV and M3.H.3 release artifacts stay unchanged.

## Implemented

- SclProfileModel exposes `m4DraftSupported` and `m4RosterRows()` only when **exactly three** individually resolved SCD SV profiles have class A, exactly 4,000 fps, 4,000 counter modulus and 64-byte payload. The operator still chooses/validates the SCL profile family and counter policy; this is never inferred from the SCD file name.
- One `M4StreamScenarioModel` retains three separate IED-qualified identities and three detached deep-copy value drafts, indexed by MU slot; duplicate identities are rejected before activation.
- The **existing** SignalMatrix, SignalPreview Table/phasor/waveform, current/voltage ListModels and theme remain one instance. The operator switches MU 1/2/3; outgoing draft is captured and incoming draft is restored with the existing non-serial `restoringScenario` guard. Frequency, 8 channels, phases, quality, enable states and waveform shaping are stored individually in each draft.
- The M4 editor is explicitly opt-in, only while output is stopped. Header reads **DRAFT ONLY · NOT DEPLOYED**; `WorkflowBar.requestStart` and deferred start are refused. `applyAllSignals` and `applyLiveScenario` reject M4 drafts, preventing accidental single-SV V1 commands. A different SCL document invalidates/ends the M4 draft view; exiting restores the old single-SV editor values.
- Native `arstack_m4_pages_harness` covers independent slots, stable IDs, duplicate roster rejection and out-of-range selection. Linux and Windows Qt CI and native preview CI now build/run the harness.

## Important incomplete capabilities

1. **Still not a firmware deployment path.** Compile the full `M4ThreeSvRoster` and nested V2 binary, then upload through the existing single `DeviceIoWorker` queue as `MULTISV BEGIN/CHUNK/VALIDATE/COMMIT/SELECT` with transaction readback. No QML direct serial write.
2. Connect M4 page edit commits to `SVSTREAM <slot> CFG` with host `sv_scenario_hash`, accepted generation/hash and per-slot readback, including a no-op on reconnect when state is already verified.
3. Only after the device's actual three-stream capability and readback are verified should the M4 START affordance be enabled. Preserve the legacy V1 START route exactly.
4. P0F needs an exact-head paired installer/firmware and on-wire 3x4000fps physical capture with all 3 MAC/svID/APPID/VLAN and common PTP sample indices; no CI-only performance claim.

## Design rationale

One Qt page composition with three isolated **data snapshots** instead of three GUIs or three always-running QML canvases. The embedded single GPTimer/PTP/EMAC publisher remains separately owned; nothing in this UI milestone modifies the 250-us firmware path.