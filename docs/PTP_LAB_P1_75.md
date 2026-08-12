# PTP-P1.75 — Lab control, observability, and SV synchronization simulation

PTP-P1.75 completes the laboratory control surface between the portable PTP runtime, the ESP32-P4 hardware-timestamp adapter, and ARStack Studio.

## Why smpSynch must be independently controllable

A laboratory injector must be able to reproduce receiver conditions, including deliberately inconsistent or transitional states. Therefore the SV `smpSynch` field is not tied blindly to whether PTP packets are visible or whether the PTP lab transmitter is running.

The product exposes the same four synchronization-policy meanings used by ARIEC61850/ARSVIN:

| Lab policy | SV wire value | Meaning | Evidence source |
|---|---:|---|---|
| `AUTO` | 0 until measured lock exists | External/measured timing policy | `SAFE_DEFAULT` today; `MEASURED` after PTP-P2 supplies lock evidence |
| `FORCE_0` | 0 | Not synchronized | `LAB_OVERRIDE` |
| `FORCE_1_LOCAL` | 1 | Local synchronized | `LAB_OVERRIDE` |
| `FORCE_2_GLOBAL` | 2 | Global synchronized | `LAB_OVERRIDE` |

The three forced modes are explicitly marked **SIMULATED** in the UI/evidence path. They are test stimuli and are not claims about the ESP32-P4 clock.

## AUTO safety rule

`AUTO` never promotes `smpSynch` merely because valid Announce, Sync, Follow_Up, or Pdelay traffic is visible. Until PTP-P2 provides measured clock-discipline/lock evidence, AUTO advertises `smpSynch=0`.

The portable policy API already accepts an optional measured synchronization value. This is the future handoff point for PTP-P2 without changing the lab-mode semantics introduced here.

## Live transition testing

The forced value is patched into the already encoded deterministic SV frame immediately before ESP-IDF Ethernet transmit. Stream identity, waveform generation, `smpCnt`, BER layout, and PTP scheduling are not rebuilt.

This allows a bench sequence such as:

```text
PROFILE SMPSYNCH 0
START
PROFILE SMPSYNCH 1
PROFILE SMPSYNCH 2
PROFILE SMPSYNCH AUTO
```

while the SV publisher continues running. This is useful for observing how a relay reacts to synchronization-state transitions without conflating the stimulus with actual clock discipline.

Accepted aliases are:

```text
PROFILE SMPSYNCH AUTO
PROFILE SMPSYNCH 0
PROFILE SMPSYNCH 1
PROFILE SMPSYNCH 2
```

Firmware status/evidence is emitted as:

```text
SMPSYNCH mode=FORCE_1_LOCAL advertised=1 source=LAB_OVERRIDE simulated=1 measured=0
```

`PROFILE SHOW` reports the active stream profile and current synchronization policy.

## ARStack Studio

The PTP Expert panel exposes:

- PTP domain and `transportSpecific`
- VLAN and PCP
- Announce and Sync intervals
- peer-delay response enable
- PTP start/stop/profile control
- Announce, Sync, FollowUp, Pdelay, and TX-failure counters
- all four SV synchronization policies
- explicit advertised `smpSynch` wire value
- `SIMULATED`, `SAFE_DEFAULT`, or future `MEASURED` provenance

Switching 0/1/2 is allowed while SV is running. This is intentional laboratory behavior.

## Realtime isolation

PTP remains outside the CPU1 deterministic SV scheduling task. P1.75 does not add servo mathematics or clock adjustment to that hot path.

The SV transmit shim only inspects outgoing IEC 61850 SV (`0x88BA`) frames from `app_main.cpp` and patches the one-byte `smpSynch` TLV. Non-SV traffic and the PTP task continue to use ESP-IDF Ethernet transmission directly.

## Non-claims

P1.75 does not claim:

- GPS traceability
- grandmaster certification
- calibrated UTC accuracy
- IEEE 1588 / IEC/IEEE 61850-9-3 conformance
- a disciplined local clock
- relay PTP lock
- automatic `smpSynch=1` or `2` qualification

Those require PTP-P2 clock discipline and physical bench evidence.
