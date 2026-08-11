# PTP Grandmaster roadmap for the SMV Injector

This document defines the next synchronization track after the SCL-driven ESP32-P4 Sampled Values injector functional phase.

The goal is to add a **PTP Grandmaster Clock (GMC) capability** to the injector without destabilizing the already-proven SV runtime and without turning traffic visibility into a synchronization or conformance claim.

## Baseline that must be preserved

The GMC work starts from `feature/smv-scl-profile-bridge`, where the injector already provides:

- SCL/CID/SCD/IID -> C++ `SvPublisherProfile` compilation;
- bounded profile deployment to ESP32-P4 while STOPPED;
- runtime MAC / APPID / VLAN / PCP / svID / confRev / rate / counter policy;
- 4I+4V, one-ASDU, 64-byte `INT32 + Quality` payload support;
- deterministic 4000 and 4800 fps publication;
- live magnitude / phase / frequency / quality control while RUNNING;
- truthful `smpSynch=0` because no measured synchronization source is yet active.

PTP changes must not regress these properties.

## Standards and claim boundary

Use authoritative standards as the source of truth:

- IEC/IEEE 61850-9-3 for the power-utility PTP profile;
- IEEE/IEC 61588 / IEEE 1588 for the underlying PTP protocol;
- ESP32-P4 hardware documentation for EMAC timestamp behavior.

The public summaries of these standards are not sufficient to invent profile constants. Exact profile parameters, default intervals, clock-quality values, BMCA behavior, TLVs, peer-delay rules, transport requirements, and synchronization-class claims must be taken from licensed/authoritative normative text or retained interoperability evidence.

Until that gate is met, label the implementation as a **laboratory GMC / synchronization foundation**, not as IEC/IEEE 61850-9-3 conformant.

## Hardware fact to exploit

ESP32-P4 EMAC supports IEEE 1588-2008 hardware timestamping and exposes a 64-bit timestamp for transmitted and received frames. The PTP implementation must keep hardware time behind a narrow ESP-IDF adapter so version-sensitive register/driver details do not leak into portable ARStack code.

Do not use `esp_timer_get_time()`, RTOS wake time, or host packet-arrival timestamps as the PTP clock source of truth.

## Architecture

```text
                    Host GUI / engineering control plane
                              |
                              v
                      PTP GMC configuration
                              |
                              v
+----------------------------------------------------------------+
| ESP32-P4                                                       |
|                                                                |
|  EMAC IEEE1588 hardware clock                                  |
|       |                                                        |
|       +--> timestamp adapter --> PTP clock service             |
|       |                          |                              |
|       |                          +--> GMC state / quality       |
|       |                          +--> Sync/Follow_Up            |
|       |                          +--> Announce                  |
|       |                          +--> peer-delay responder      |
|       |                                                        |
|       +--> SV timebase bridge ---------------------------------+
|                                  |                             |
|                                  v                             |
|                         deterministic SV publisher             |
|                                  |                             |
|                                  v                             |
|                         hardware TX evidence                   |
+----------------------------------------------------------------+
```

Portable code owns PTP message representation, validation, state and policy. ESP-IDF-specific code owns EMAC clock/timestamp access and raw Layer-2 transmission/reception.

## Tranche order

### GMC-0 — hardware-clock and wire-codec foundation

Deliverables:

- narrow `PtpHardwareClock` / timestamp adapter for the ESP32-P4 EMAC;
- read/set/adjust operations only when actually supported by the pinned toolchain/hardware path;
- 64-bit hardware timestamp representation with explicit validity;
- portable IEEE 1588 message header codec and bounded message structures needed by the selected profile;
- raw Layer-2 EtherType `0x88F7` classification;
- host unit/golden tests for encode/decode and malformed-frame rejection;
- no PTP traffic required yet;
- no change to SV `smpSynch`.

Acceptance: hardware clock can be read monotonically on the physical board and the portable codecs are deterministic under GCC/Clang/MSVC/embedded CI.

### GMC-1 — passive monitor before active grandmaster

Deliverables:

- receive PTP frames without claiming synchronization;
- expose observed domain, sourcePortIdentity, sequence IDs, message types and timestamp availability;
- bounded foreign-clock table;
- duplicate/gap/malformed counters;
- optional GUI status panel: `PTP monitor only`.

Acceptance: retained packet evidence proves that observed PTP traffic is decoded correctly while the existing SV injector remains unchanged.

### GMC-2 — laboratory grandmaster transmit path

Start with a deliberately explicit **LAB_GMC** operating mode.

Recommended implementation strategy:

- prefer a two-step Sync model first unless authoritative hardware/API evidence proves a reliable one-step correction-field path;
- transmit Sync using the EMAC hardware timestamp path;
- obtain the actual Sync egress timestamp;
- build Follow_Up from that measured timestamp;
- transmit Announce according to the selected, sourced profile rules;
- implement peer-delay responder behavior required by the selected profile only after its exact normative contract is sourced;
- use a fixed bounded state machine; no heap allocation in timestamp-critical paths;
- retain sequence, missing timestamp, late Follow_Up and TX-failure counters.

A free-running or host-seeded laboratory GMC must advertise only truthful clock quality/traceability. Do not invent `clockClass`, UTC traceability, frequency traceability, time source or profile flags from memory.

Acceptance: an independent PTP receiver/analyzer recognizes a stable GMC stream and correlates Follow_Up to hardware-timestamped Sync frames. This still does not promote SV `smpSynch`.

### GMC-3 — clock-source and discipline model

Support clock-source states explicitly:

```text
UNINITIALIZED
LAB_FREE_RUNNING
HOST_SEEDED_NOT_TRACEABLE
EXTERNAL_REFERENCE_ACQUIRING
EXTERNAL_REFERENCE_LOCKED
HOLDOVER
FAULT
```

A future external reference may be GNSS/PPS or another measured timing source. Time-of-day availability, frequency discipline and traceability are separate properties.

Do not collapse `has a current UTC-looking value` into `traceable`.

Acceptance: state transitions are deterministic, measured, and represented in telemetry; loss of reference degrades state instead of silently retaining a synchronization claim.

### GMC-4 — bind the SV sample timeline to hardware PTP time

This is the critical convergence step.

The current rational GPTimer scheduler is deterministic but free-running. A PTP-enabled injector must make the **SV sample timeline a function of the hardware PTP clock**, not merely run PTP messages beside an unrelated sample timer.

Required behavior:

- define sample epoch/boundary policy from the selected profile;
- align sample boundaries to hardware PTP time;
- derive the next SV sample deadline from the active profile rate and PTP epoch;
- retain rational scheduling for rates such as 4800 fps, but anchor phase to the hardware clock;
- correlate `smpCnt`, scheduled PTP deadline, RTOS wake, TX submit and EMAC hardware TX timestamp;
- prevent PTP task activity from blocking the high-priority SV publisher;
- no catch-up bursts after a missed slot.

Acceptance: 4000 and 4800 fps remain stable with zero unexplained counter gaps while sample-phase error is measured against the hardware PTP clock.

### GMC-5 — truthful `smpSynch` policy

Only after GMC-4 and sourced profile rules exist may `smpSynch` change from zero.

The decision must be a pure policy result derived from:

```text
clock source state
+ hardware-clock health
+ selected PTP profile
+ sample timeline actually anchored to that clock
+ applicable synchronization/profile rule
```

Any loss of the required evidence must demote `smpSynch` immediately according to the sourced rule.

No GUI switch may force a standards-mode synchronization claim.

## Grandmaster operating modes

Keep these modes distinct:

1. **PTP OFF** — current injector behavior; `smpSynch=0`.
2. **PTP MONITOR** — decode/observe only; `smpSynch=0`.
3. **LAB GMC** — active PTP source using the EMAC hardware clock, but without external traceability unless separately proven; `smpSynch` remains conservative until GMC-5.
4. **TRACEABLE GMC** — future mode requiring measured external-reference discipline and sourced profile-quality policy.

Do not let the presence of Announce/Sync traffic alone move the product into TRACEABLE GMC.

## Realtime task isolation

Suggested task ownership:

```text
CPU1 high priority
  SV publisher + sample deadline execution

CPU0 medium/high
  PTP event RX/TX state machine
  Follow_Up / peer-delay processing

CPU0 lower priority
  telemetry / GUI control
```

PTP and SV may share the Ethernet driver, but they must use separate bounded packet buffers/state. PTP message generation must not rebuild or mutate the active SV profile.

## GUI integration

Add a compact `Time & synchronization` section only after backend evidence exists. Minimum fields:

- role: OFF / MONITOR / LAB GMC / TRACEABLE GMC;
- domain;
- grandmaster identity;
- hardware clock valid;
- source state;
- traceability flags as observed/configured facts;
- Sync/Announce counters;
- hardware timestamp failures;
- SV timebase: FREE-RUNNING / PTP-ANCHORED;
- `smpSynch` current value plus the reason for that value.

Do not show `LOCKED` unless a concrete measured lock state exists.

## Relationship to existing tracks

- Issue #24 remains the parent standards-first PTP/SV synchronization roadmap.
- Issue #21 remains the deterministic timing-evidence gate.
- PR #26 contains an independent hardware-TX timestamp evidence track; reuse/converge its narrow EMAC timestamp adapter where technically sound rather than duplicating timestamp mechanisms.
- The current injector profile bridge remains the runtime baseline; PTP must be stacked on it rather than rebuilding the injector from an older branch.

## Branch and PR discipline for the separate implementation thread

The separate GMC implementation thread must:

1. start from the latest `origin/feature/smv-scl-profile-bridge`;
2. create `feature/ptp-gmc-foundation`;
3. open a **draft stacked PR** with base `feature/smv-scl-profile-bridge`;
4. keep each GMC tranche in reviewable commits;
5. not merge PR #54 or the GMC PR unless explicitly requested;
6. regularly rebase/refresh only from the injector-profile branch, not from unrelated MMS/control branches;
7. preserve the public vendor-neutral naming policy.

## First physical acceptance sequence

Do not start with `smpSynch`.

```text
GMC-0 hardware clock read
  -> prove non-zero/monotonic hardware time
  -> GMC-1 passive PTP decode
  -> GMC-2 LAB GMC Announce/Sync/Follow_Up
  -> independent receiver recognizes the GMC
  -> correlate Sync hardware TX timestamp with Follow_Up
  -> confirm SV still runs 4000/4800 fps, missed=0, TX fail=0
  -> GMC-4 anchor SV sample timeline to PTP clock
  -> only then evaluate sourced smpSynch policy
```

## Exit definition

The GMC track is functionally complete when:

- PTP traffic is produced from a hardware-timestamped clock path;
- the grandmaster state/quality is truthful;
- an independent receiver observes and accepts the intended laboratory profile behavior;
- the SV timeline is actually anchored to the same hardware PTP clock;
- 4000/4800 fps reliability remains at the established injector baseline;
- hardware TX timing evidence is retained;
- `smpSynch` is derived from measured state and sourced profile rules rather than a manual flag.

Formal IEC/IEEE 61850-9-3 conformance remains a separate evidence/certification claim.