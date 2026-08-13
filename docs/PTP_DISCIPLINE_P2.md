# PTP-P2 — Measured Clock Discipline

PTP-P2 adds a hardware-timestamped **time receiver and clock-discipline engine** to the ESP32-P4 timing subsystem. It is designed for isolated IEC 61850 laboratory interoperability work, especially receiver-condition testing around Sampled Values and PTP.

## What P2 is

P2 has three explicit operating roles:

| Role | Behavior | Clock actuation | AUTO `smpSynch` |
| --- | --- | --- | --- |
| `LAB_SOURCE` | Emits Announce / two-step Sync and answers peer-delay | No external discipline | `0` |
| `TIME_RECEIVER` | Selects an external PTP source, measures peer delay and offset, disciplines the ESP32-P4 IEEE1588 clock | Yes | Measured state only |
| `MONITOR` | Passive PTP observation | No | `0` |

A single Ethernet port is never intentionally run as PTP source and time receiver at the same time.

PTP remains disabled by default in firmware.

## What P2 is not

P2 does **not** by itself establish any of the following claims:

- GPS-backed grandmaster behavior
- certified grandmaster behavior
- calibrated UTC
- formal IEC/IEEE 61850-9-3 conformance
- formal IEEE 1588 profile conformance
- relay interoperability acceptance

Those claims require evidence beyond software state, including the appropriate reference source, measurement equipment, profile verification and physical relay testing.

## Hardware timestamp path

`TIME_RECEIVER` uses the ESP32-P4 EMAC IEEE1588 timestamp path.

For peer delay:

- `t1` = local hardware TX timestamp of `Pdelay_Req`
- `t2` = peer request-receive timestamp carried by `Pdelay_Resp`
- `t3` = peer response-transmit timestamp carried by `Pdelay_Resp_Follow_Up`
- `t4` = local hardware RX timestamp of `Pdelay_Resp`

The portable engine calculates:

```text
meanPathDelay = ((t2 - t1) + (t4 - t3) - corrections) / 2
```

For Sync:

- the local receive time comes from the EMAC hardware RX descriptor
- a two-step source supplies the exact origin timestamp in `Follow_Up`
- Sync and Follow_Up `correctionField` values are included

The portable engine calculates:

```text
offsetFromMaster = localRx - masterOrigin - meanPathDelay - corrections
```

## Source selection

P2 deliberately implements a bounded single-port source preference rather than claiming a full BMCA implementation.

Announce candidates are compared using a deterministic clock-quality tuple including priority, clock class, accuracy, variance, grandmaster identity and steps removed. A selected source may be replaced by a better candidate or dropped after timeout.

Source changes clear pending Sync and peer-delay correlation and reset the discipline acquisition state.

## Discipline state machine

States:

```text
UNLOCKED -> ACQUIRING -> LOCKED -> HOLDOVER -> UNLOCKED
                         |             |
                         +----FAULT----+
```

Default laboratory thresholds are intentionally conservative and configurable:

- maximum peer path delay: `5 ms`
- maximum path-delay jitter: `100 us`
- LOCKED offset threshold: `2 us`
- unlock threshold: `20 us`
- acquisition phase-step threshold: `1 ms`
- qualified samples required for LOCKED: `8`
- Sync timeout: `1 s`
- bounded HOLDOVER: `5 s`
- maximum frequency command: `±100,000 ppb`

A large acquisition offset may cause a bounded phase step. Smaller residual offsets generate bounded frequency commands. Hardware actuation failure transitions the discipline state to `FAULT`.

## Frequency actuation on ESP-IDF 5.x

The ESP-IDF EMAC frequency-adjust command scales the **current** IEEE1588 increment rather than accepting an absolute ppb value. P2 therefore converts its absolute servo request into a relative ratio against the correction already applied:

```text
relativeScale = (1 + desiredPpb / 1e9) / (1 + appliedPpb / 1e9)
```

This prevents successive corrections from accidentally compounding.

On stop or source reset, P2 requests a return to nominal frequency before clearing measured synchronization state.

## `smpSynch` policy

P1.75 laboratory overrides remain unchanged:

- forced `0` = simulated not synchronized
- forced `1` = simulated local synchronized
- forced `2` = simulated global synchronized

These values are intentional test stimuli and do not claim measured clock state.

`AUTO` is different. P2 supplies AUTO with measured evidence only:

| Discipline evidence | AUTO `smpSynch` |
| --- | --- |
| `UNLOCKED`, `ACQUIRING`, `FAULT` | `0` |
| `LOCKED`, source not proven globally traceable | `1` |
| `LOCKED`, strict global traceability evidence | `2` |
| bounded `HOLDOVER` | `1` |

Global promotion requires more than receiving Announce packets. The selected Announce must carry strict time-traceability evidence, a non-local time source, a suitable clock class and a nonzero grandmaster identity, while the local measured discipline must be `LOCKED`.

## Bench commands

Existing commands remain valid. P2 adds stopped-only role selection through the bounded profile command surface:

```text
PROFILE PTPROLE SOURCE
PROFILE PTPROLE RECEIVER
PROFILE PTPROLE MONITOR
PROFILE SHOW
PTP START
PTP SHOW
PTP STOP
```

`PROFILE SMPSYNCH <AUTO|0|1|2>` remains live while SV is running.

`PTP SHOW` emits the existing PTP status plus a stable `PTP2` diagnostic line containing role, discipline state, selected source, offset, path delay, jitter, frequency correction, measured `smpSynch`, RX counters and accepted/rejected discipline samples.

## Task isolation

The PTP receiver/discipline task remains on CPU0 and outside the deterministic SMV hot path. PTP timestamp correlation and servo work never run in the SMV publisher's sample-timing loop.

## Current topology boundary

The P2 receiver is intentionally scoped to a bounded lab topology where the selected Announce source also participates in the peer-delay exchange. Full multi-hop profile behavior, transparent/boundary-clock topology qualification and full BMCA are not claimed in this tranche.

A future combined PTP + SMV analyzer should introduce a shared Ethernet RX dispatcher so several protocol consumers do not compete for raw RX callback ownership.

## Validation requirements before relay claims

Software completion is not relay acceptance. Before describing P2 as proven with a particular relay/profile, capture and retain evidence for at least:

1. external reference source identity and Announce profile
2. Pdelay `t1/t2/t3/t4` exchange
3. Sync / Follow_Up sequence correlation
4. offset and path-delay convergence
5. LOCKED / HOLDOVER / UNLOCKED transitions
6. actual transmitted SV `smpSynch`
7. relay PTP state and SV acceptance behavior
8. failure cases: source loss, wrong domain, wrong transportSpecific and forced `smpSynch` transitions
