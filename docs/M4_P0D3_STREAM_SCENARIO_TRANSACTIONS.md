# M4 P0D3 — Atomic per-MU scenarios and shared-grid stream gates

**Status:** feature PR / requires ESP-IDF + C++ CI; not physical hardware validation.

Firmware reuses the existing `SvScenarioTransaction` in a bounded array of three slots. No XML, heap, mutex, extra timer, extra publisher task or per-frame log is introduced. The old single-SV CFG/START protocol and M3.H.3 release remain untouched.

## Protocol

Requires `MULTISV` committed roster + explicit `MULTISV SELECT`. Slot is `1`, `2`, or `3`.

```text
SVSTREAM 2 CFG BEGIN 12345
SVSTREAM 2 CFG FREQ 50000
SVSTREAM 2 CFG SET IA 100000 0 0 1
SVSTREAM 2 CFG SET IB 100000 -120000 0 1
SVSTREAM 2 CFG SET IC 100000 120000 0 1
SVSTREAM 2 CFG SET IN 0 0 0 1
SVSTREAM 2 CFG SET UA 5774 0 0 1
SVSTREAM 2 CFG SET UB 5774 -120000 0 1
SVSTREAM 2 CFG SET UC 5774 120000 0 1
SVSTREAM 2 CFG SET UN 0 0 0 1
SVSTREAM 2 CFG SHAPE 0 0 0 2 1000
SVSTREAM 2 CFG COMMIT 12345 <16-character-host-computed-sv_scenario_hash-hex>
SVSTREAM 2 CFG READBACK 12345
SVSTREAM 2 SHOW
```

`FREQ` argument is mHz; `SET` arguments are wire RMS counts, phase millidegrees, uint32 quality, and Boolean channel enabled. `SHAPE` arguments are enabled, DC permille, harmonic permille, order and clip permille. SHA/CRC of the SCD envelope is a **different** check from `sv_scenario_hash`; compute the latter with the existing canonical host codec in `live_signal_transaction.hpp`, not floating-point JSON serialization.

All 18 fields (frequency + shape + 8 SET + 8 enable states) are mandatory. A SET stages value and enabled status together, satisfying two completeness bits. One missing channel, invalid field, changed bank generation, wrong token, stale timeout (>8s) or hash mismatch rejects the whole transaction, preserving old output. `READBACK` checks applied bank generation/hash. A running scenario does not provide a fresh global START authority.

```text
SVSTREAM 2 STOP
SVSTREAM 2 SHOW
SVSTREAM 2 START
```

These operations toggle only the stream's Ethernet TX bit, **not** its signal-bank contents or the shared grid. Each 250us tick reads one 3-bit enable snapshot; all three DDS engines advance regardless of TX gating. MU2 rejoining takes the **current group smpCnt**, not 0. The global START/STOP handshake and physical link/PTP fail-closed policy remain authoritative; a per-MU START while the group is stopped merely arms that MU for the next verified group START. A newly committed SCD roster reinitializes all three TX-enable bits to ON.

## Explicit limitations / next

- Atomic **per-MU scenario**, not one atomic transaction covering all three MUs.
- TX state is RAM-resident and persists through Studio disconnect while the ESP32 remains powered, not across ESP32 reboot.
- Output follows one wire-time grid but no guarantee is claimed about zero Ethernet inter-packet skew.
- Complete serial Studio deploy, 3-MU page editing and reconnect adoption are P0E.
- Firmware CI is **not** proof of real 3×4000fps, 250us deadline, on-wire MAC or PTP phase; P0F must capture physical Ethernet.
