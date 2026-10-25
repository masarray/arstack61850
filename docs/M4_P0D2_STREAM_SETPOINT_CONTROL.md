# M4-P0D2 — Three independent live SV setpoint banks on ESP32-P4

**Status:** feature branch / firmware CI. Continues merged P0C2B triple canonical SV TX preview and P0D1 independent four-buffer banks. Existing M3.H.3 stable release remains unchanged.

## Real implementation

In opted-in `MULTISV SELECT` TX preview mode, MU1, MU2 and MU3 now read their own bounded `M4ThreeSvSignalBanks` slot when rendering the sample for a common 250us tick. One GPTimer and one EMAC publisher task remain. The publisher reads snapshots without mutex and performs no heap allocations.

- `SVSTREAM <1|2|3> SHOW`: read back all eight channel values, phase, quality, frequency, and generation of the **selected** MU.
- `SVSTREAM <1|2|3> SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms_wire_counts> <phase_mdeg> <quality_u32>`: atomically publish a full state snapshot for only this MU.
- `SVSTREAM <1|2|3> FREQ <millihertz>`: change fundamental frequency for only this MU. Uses the existing fixed-point sine engine.
- Commands require a committed `MULTISV` roster and explicit `MULTISV SELECT`; editing a legacy single-SV board does not activate a multi stream. Invalid values and slots are rejected without changing active signal state.
- A signal bank's generation persists across STOP/START within this powered session, with per-MU phase reset as in the existing START policy. Packet identities remain unchanged by waveform edits.
- Source of truth remains the firmware; command readback is `SVSTREAM SHOW`, not the Studio draft.

### Engineer workflow

```text
MULTISV STATUS
MULTISV SHOW
MULTISV SELECT                    # only while stopped, from a committed 3-MU SCD roster
SVSTREAM 1 SET IA 100000 0 0
SVSTREAM 2 SET IA 200000 -120000 0
SVSTREAM 3 SET IA 300000 120000 0
SVSTREAM 2 SHOW
# Existing verified START handshake remains required. SVSTREAM edits work live.
```

### Explicit incomplete work

This P0D2 interface uses **atomic single-command full-bank publication**, but it is NOT a full multi-command scenario prepare/commit handshake. That transactional UX/readback integration is the next requirement before polished 3-page Studio. Per-stream START/STOP is also still missing; `MULTISV` opts into a group START/STOP. Multi-SV physical packet capture at the final 12,000 canonical SV frames/s and PTP phase/jitter measurements are not yet available.

**Next:** P0D3 multi-field setpoint transaction, P0E Qt `StreamScenarioModel` + Table/Waveform/Phasor pagination, P0F paired package with on-wire test. No extra PTP timers or per-MU TX tasks.
