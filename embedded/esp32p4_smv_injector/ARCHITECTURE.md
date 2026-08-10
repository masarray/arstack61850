# ESP32-P4 SV Injector — standards-first architecture

This document defines the direction after P0/P1 hardware bring-up and P2 50 Hz realtime publishing proved that the current ESP32-P4 target can sustain a 4000-sample/s SV stream on the bench.

The design separates:

- **wire facts** — what is actually observed or timestamped;
- **configured expectations** — what SCL/profile configuration requires;
- **profile claims** — conclusions such as generic IEC 61850-9-2, IEC 61869-9, or legacy 9-2LE compatibility;
- **synchronization evidence** — what is actually known about PTP clock and `smpSynch` state.

A decoded SV packet is not by itself a conformance claim.

## 1. Product split

```text
Host engineering application
    |
    | SCD/CID/ICD/IID + waveform/fault scenario
    v
SCL/profile compiler
    |- stream address and identifiers
    |- SmvOpts field-presence policy
    |- dataset order and element types
    |- sample-rate / nofASDU / counter policy
    |- scaling and quality policy
    |- synchronization policy
    v
Versioned compiled SvPublisherProfile
    |
    | control plane
    v
ESP32-P4 realtime runtime
    |- validate immutable profile
    |- prebuild packet templates
    |- waveform/fault engine
    |- deterministic SV publisher
    |- PTP-disciplined hardware clock
    |- truthful smpSynch
    |- timing/evidence telemetry
```

XML/SCL parsing belongs on the host. It must never occur in the ESP32 realtime TX hot path.

## 2. Reuse ARStack core instead of creating parallel models

ARStack already contains:

- `ariec61850::scl::SclDocument` / `SclSampledValuesStream`;
- dataset entries with CDC/basic-type metadata;
- Ethernet/VLAN types;
- Sampled Values ASDU/PDU/frame codecs;
- sample-counter and stream-supervision utilities.

The future publisher profile should be compiled from those models rather than duplicating SCL parsing inside the embedded application.

## 3. Compiled SV profile

The host should reduce SCL plus an explicitly selected interoperability profile to a compact immutable structure similar to:

```text
SvPublisherProfile
  version
  profileFamily
  destinationMac
  appId
  vlanId / vlanPriority
  svId
  confRev
  nofAsdu
  fieldPresence
    dataSet
    refrTm
    smpRate
    smpMod
  dataSetReference
  samplingBasis
  sampleRate
  samplesPerCycle
  sampleCounterPolicy
  synchronizationPolicy
  channels[]
    semantic
    basicType
    byteOffset
    scale
    qualityPolicy
```

Activation must fail closed when required bindings are unresolved or the binary profile fails version/length/checksum validation.

## 4. Profile claims

Keep three product-facing families separate:

1. **Generic IEC 61850-9-2 / SCL driven** — exact configuration follows the selected SCL stream.
2. **IEC 61869-9** — only after required constraints are sourced and validated.
3. **Legacy 9-2LE compatibility** — explicit compatibility mode, never silently inferred as the generic standard.

Unknown or partially evidenced profile fields stay unknown. The software must not convert a heuristic into a compliance label.

## 5. Canonical vs diagnostic traffic

The standards-facing canonical stream and any capture workaround must have separate identities.

Current bench example:

- canonical: SCL-derived multicast/VLAN stream;
- diagnostic mirror: untagged broadcast with its own APPID/svID and explicit sampling metadata for raw analyzer reconstruction.

A diagnostic mirror is never evidence that the multicast/VLAN process-bus stream has passed interoperability testing.

## 6. Timing hierarchy

Timing evidence has distinct layers and they must not be mixed:

```text
sample deadline
    -> publisher task wake time
    -> EMAC hardware TX timestamp
    -> analyzer hardware RX timestamp
    -> external reference capture timestamp
    -> host capture timestamp
```

The current GPTimer/FreeRTOS telemetry proves scheduler continuity, but host USB-Ethernet arrival timestamps can be batched or perturbed and are not the source of truth for device TX jitter.

P2 should therefore add ESP32-P4 EMAC IEEE1588v2 hardware timestamp evidence before making on-wire timing claims.

## 7. PTP / IEC 61850-9-3 direction

Use an evidence-oriented layered design and move standards-facing synchronization onto hardware time:

1. PTPv2 Layer-2 codec (`0x88F7`).
2. Passive monitor: observed sources, domain, message types, sequence integrity.
3. IEC/IEEE 61850-9-3 Power Profile validator.
4. ESP32-P4 EMAC IEEE1588v2 hardware clock.
5. Peer-delay / offset estimation and clock servo.
6. Lock / holdover / unlocked state machine.
7. SV `smpSynch` policy derived from measured state.
8. Hardware TX timestamp evidence correlated with `smpCnt`.

Software-generated lab PTP traffic must never be treated as proof of hardware synchronization. Traffic visibility, clock discipline and synchronization claims remain separate.

## 8. `smpSynch` rule

Standards mode:

```text
no valid synchronization evidence -> unsynchronized
measured local synchronization     -> local according to selected profile
measured global/PTP synchronization-> global according to selected profile
```

Compatibility overrides, if later offered for relay readability tests, must be visibly labelled as compatibility behavior and never treated as timing evidence.

## 9. Current bench interpretation

The current 50 Hz build has demonstrated:

- approximately 4000 SV samples/s;
- continuous sequence on the diagnostic stream;
- balanced reconstructed 50 Hz three-phase waveforms;
- zero device-side TX failures and missed GPTimer notifications in observed stable runs;
- a host USB-Ethernet capture path that reports about the correct average throughput but large individual arrival-time excursions.

Therefore the next timing work is **hardware timestamp evidence**, not compensating the publisher against host capture jitter.

## 10. Acceptance gates

A production-grade result requires independent gates for:

- packet/framing correctness;
- SCL/configuration match;
- dataset semantics and engineering scaling;
- canonical multicast/VLAN interoperability;
- sustained 4000/4800 sample/s integrity;
- hardware TX timing statistics;
- PTP synchronization quality and truthful `smpSynch`;
- relay/subscriber interoperability;
- evidence artifacts (PCAP + machine-readable timing/profile report).

Tracked work:

- P2 deterministic timing evidence: issue #21
- standards-first SCL/profile/PTP architecture: issue #24