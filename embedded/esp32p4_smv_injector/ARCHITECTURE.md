# ESP32-P4 Sampled Values Injector — standards-first architecture

This document defines the architecture after the ESP32-P4 injector progressed from fixed-rate bring-up to a physically exercised SCL-driven runtime profile.

The core injector workflow is now functionally complete for the current supported 4I+4V embedded profile. Remaining work is deliberately separated into validation depth, profile breadth, synchronization, analyzer and product-hardening phases.

The design separates:

- **wire facts** — what is actually observed or timestamped;
- **configured expectations** — what SCL/profile configuration requires;
- **runtime state** — what immutable profile and mutable signal state the ESP32-P4 has actually armed;
- **profile claims** — conclusions such as generic IEC 61850-9-2, IEC 61869-9, or legacy profile compatibility;
- **synchronization evidence** — what is actually known about PTP clock and `smpSynch` state.

A decoded SV packet or successful bench run is not by itself a conformance claim.

## 1. Current product split

```text
Host engineering application
    |
    | SCD/CID/ICD/IID + operator values
    v
C++ SCL/profile compiler
    |- stream address and identifiers
    |- SmvOpts field-presence policy
    |- DataSet order and element types
    |- sample-rate / nofASDU / counter-policy candidates
    |- compatibility Class A/B/C diagnostics
    v
Validated immutable SvPublisherProfile
    |
    | bounded control plane
    v
ESP32-P4 realtime runtime
    |- validate / commit immutable profile while STOPPED
    |- prebuild packet templates
    |- fixed-point waveform engine
    |- deterministic SV publisher
    |- live magnitude / phase / frequency / quality state
    |- truthful unsynchronized state until PTP evidence exists
    |- timing/evidence telemetry
```

XML/SCL parsing belongs on the host. It must never occur in the ESP32 realtime TX hot path.

## 2. Proven end-to-end injector flow

The current physically exercised flow is:

```text
engineering file
    -> C++ SCL parser
    -> resolved SV stream
    -> Class A after required counter-policy confirmation
    -> Deploy while STOPPED
    -> ESP32-P4 profile commit
    -> START
    -> 4800 fps runtime publication
    -> live 50 Hz three-phase values
```

A retained real engineering-file-derived test reached approximately 4800 samples/s with zero canonical TX failures and zero missed slots in device telemetry.

This establishes functional completion for the current embedded profile boundary, not universal SCL support or formal conformance.

## 3. Reuse ARStack core instead of creating parallel models

ARStack contains the shared SCL, Ethernet, SV and evidence primitives required by this architecture. The host compiler should remain the semantic authority rather than introducing a second browser or embedded SCL model.

The browser is therefore an operator/control surface, not an IEC 61850 semantic parser.

## 4. Compiled SV profile

The host reduces SCL plus explicit engineering resolution to an immutable profile containing the fields needed by the embedded runtime, including:

```text
SvPublisherProfile
  schemaVersion
  controlBlockReference
  svId
  dataSetReference
  destinationMac
  appId
  vlanPresent / vlanId / vlanPriority
  confRev
  sampleRate / sampleMode
  publisherRateHz
  nofAsdu
  sampleCounterPolicy / modulus
  ASDU field-presence policy
  ordered channels
  payloadSizeBytes
```

The first embedded bridge intentionally supports a bounded subset:

- one ASDU;
- absolute `SmpPerSec` publisher rate;
- explicit validated sample-counter modulus;
- eight logical channels `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`;
- 16 ordered leaves as eight `INT32 value + Quality` pairs;
- 64-byte sample payload.

Activation fails closed when required bindings are unresolved or the profile cannot be mapped safely to this layout.

## 5. Runtime mutability law

While RUNNING, profile identity/layout is immutable:

```text
immutable while RUNNING
  destination MAC
  APPID
  VLAN / PCP
  svID
  DataSet identity/layout
  publisher rate
  sample-counter policy
  ASDU field-presence policy
```

Live signal state remains mutable:

```text
mutable while RUNNING
  current / voltage magnitude
  phase
  signal frequency
  quality
  channel enable state
```

Structural change therefore requires:

```text
STOP -> validate -> deploy -> arm -> START
```

## 6. Canonical versus diagnostic traffic

The standards-facing canonical stream and the capture workaround have separate identities.

### Canonical stream

The canonical stream uses the active SCL-derived runtime profile: multicast destination, APPID, VLAN/PCP, `svID`, `confRev`, publisher rate and sample-counter policy.

### Diagnostic mirror

The current lab mirror is intentionally different:

- broadcast destination;
- APPID `0x4F01`;
- `svID=AR_DIAG_SV1`;
- untagged;
- same live waveform source;
- active publisher rate and configuration revision;
- separate diagnostic sample counter.

The current Windows USB-Ethernet capture path reliably exposes the mirror but does not reliably expose the canonical multicast/priority-tagged stream. Therefore observing `AR_DIAG_SV1` is evidence of the diagnostic path only; it is not evidence that the SCL profile was ignored.

The diagnostic mirror must never be relabelled with the canonical `svID`, because that would make a debug frame indistinguishable from the standards-facing stream.

## 7. Rational scheduling

The runtime uses a 1 MHz GPTimer with a rational one-shot schedule cursor.

```text
4000 fps -> 250 / 250 / 250 ... us
4800 fps -> 208 / 208 / 209 ... us
```

This avoids permanently rounding a fractional-microsecond target into the wrong long-term publisher rate.

The packet template is prebuilt when the profile is armed. The hot loop only patches bounded fixed-width fields and transmits the frame.

## 8. Timing hierarchy

Timing evidence has distinct layers and they must not be mixed:

```text
sample deadline
    -> publisher task wake time
    -> EMAC hardware TX timestamp
    -> analyzer hardware RX timestamp
    -> external reference capture timestamp
    -> host capture timestamp
```

Current GPTimer/FreeRTOS telemetry proves scheduler continuity and device-side submission behavior. It does not establish final on-wire jitter.

The next timing gate remains ESP32-P4 EMAC hardware TX timestamp evidence and/or an independent hardware analyzer.

## 9. PTP / IEC 61850-9-3 direction

Use an evidence-oriented layered design and move synchronization claims onto hardware time:

1. PTPv2 Layer-2 codec (`0x88F7`).
2. Passive monitor: observed sources, domain, message types, sequence integrity.
3. Applicable power-profile validator.
4. ESP32-P4 EMAC IEEE1588v2 hardware clock.
5. Peer-delay / offset estimation and clock servo.
6. Lock / holdover / unlocked state machine.
7. SV `smpSynch` policy derived from measured state.
8. Hardware TX timestamp evidence correlated with `smpCnt`.

Software-generated traffic or stable waveform output must never be treated as proof of synchronization.

## 10. `smpSynch` rule

```text
no valid synchronization evidence -> unsynchronized
measured synchronization           -> encode only what the selected profile and evidence justify
```

The current injector therefore keeps `smpSynch=0`.

## 11. Independent analyzer node

The preferred next hardware phase is a second raw Ethernet analyzer node, potentially using another ESP32-P4 board:

```text
Process-bus Ethernet
        |
        v
ESP32-P4 Analyzer
    |- promiscuous / all-multicast raw RX
    |- preserve / parse IEEE 802.1Q tags
    |- classify SV EtherType 0x88BA
    |- decode APPID / svID / smpCnt / confRev / quality / sample values
    |- stream table and continuity statistics
    |- timing evidence
        |
        v
Observed-wire profile
        |
        v
Configured-vs-observed comparison
```

Promiscuous mode cannot recover frames that a switch never sends to the analyzer port. A switched lab therefore requires a trusted TAP or mirror/SPAN port that preserves the relevant traffic and VLAN tags.

This analyzer removes ambiguity introduced by host USB-NIC filtering or VLAN handling and enables direct comparison of configured SCL truth with observed wire truth.

## 12. Profile claims

Keep product-facing claims separate:

1. **Generic SCL-driven Sampled Values** — exact configuration follows the selected engineering stream within the implemented runtime boundary.
2. **IEC 61869-9 profile support** — only after the applicable constraints are sourced and independently validated.
3. **Legacy profile compatibility** — explicit compatibility behavior only, never silently inferred as the generic standard.

Unknown or partially evidenced profile fields stay unknown. A heuristic must not become a compliance label.

## 13. Current completion boundary

### Functionally complete

- host SCL/CID import;
- C++ semantic compilation;
- stream discovery;
- Class A/B/C diagnostics;
- explicit counter-policy confirmation;
- bounded immutable profile deployment;
- ESP32-P4 runtime profile arm;
- 4000 and 4800 fps runtime scheduling;
- live current/voltage magnitude, phase, frequency, quality and enable control;
- START/STOP state model;
- device telemetry for rate, TX failure, missed slots and generation;
- separate diagnostic mirror for capture-path troubleshooting.

### Follow-on phases

- trusted canonical multicast/VLAN on-wire proof;
- configured-vs-observed analyzer;
- hardware TX timing statistics;
- PTP-disciplined clock and synchronization evidence;
- broader embedded DataSet/type support;
- validated engineering scaling;
- multi-stream publication;
- fault/scenario and COMTRADE workflows;
- independent relay/subscriber interoperability;
- production packaging and qualification;
- formal conformance work.

These follow-on gates do not reopen the already proven core injector workflow.

## 14. Acceptance philosophy

A production-grade result ultimately requires independent evidence for:

- packet/framing correctness;
- SCL/configuration match;
- DataSet semantics and engineering scaling;
- canonical multicast/VLAN interoperability;
- sustained publisher integrity;
- hardware TX timing statistics;
- PTP synchronization quality and truthful `smpSynch`;
- subscriber interoperability;
- retained evidence artifacts.

The project should continue to distinguish **functional completion**, **interoperability evidence**, and **formal conformance** rather than treating them as the same milestone.