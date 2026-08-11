# SMV Injector status and completion boundary

This document records the current engineering status of the ESP32-P4 Sampled Values injector and defines what is considered complete versus what remains as validation, timing, analyzer and product-hardening work.

## Status summary

**SMV Injector functional phase: COMPLETE for the current supported runtime profile.**

The completed functional path is:

```text
Import SCL / CID / SCD / IID
    -> C++ IEC 61850 SCL parser
    -> normalized SvPublisherProfile
    -> stream selection and Class A/B/C diagnostics
    -> explicit sample-counter-policy validation when required
    -> deploy immutable profile while STOPPED
    -> ESP32-P4 arms the runtime profile
    -> START
    -> deterministic Sampled Values publication
    -> live I/U/phase/frequency/quality updates while RUNNING
    -> STOP
```

The current completion statement is intentionally narrower than a standards-conformance or production-readiness claim.

## Physical bench evidence achieved

The current branch has been exercised on an ESP32-P4 Ethernet board with a real engineering-file-derived 4800 fps profile.

Observed results include:

- engineering file compiled by the C++ SCL engine;
- one SV stream resolved and promoted to Class A after explicit counter-policy confirmation;
- immutable runtime profile deployed to the ESP32-P4;
- SCL-derived `svID`, APPID, VLAN/PCP intent, configuration revision, publisher rate and counter modulus displayed as the armed profile;
- runtime transitioned to 4800 fps;
- device telemetry reported approximately 4800 samples/s;
- device telemetry reported zero missed slots in the retained run;
- device telemetry reported zero canonical TX failures in the retained run;
- live 50 Hz three-phase waveform remained observable through the diagnostic capture path;
- diagnostic stream inherited the active profile rate and configuration revision, confirming that it was produced by the same active runtime profile;
- live engineering-value changes remain independent from immutable stream identity/layout.

This is sufficient to call the **injector workflow itself functionally complete for the supported 4I+4V profile boundary**.

## Current supported device-profile boundary

The first deployable embedded profile is deliberately bounded:

- one ASDU;
- `SmpPerSec` with an absolute publisher rate;
- explicit validated `smpCnt` modulus;
- eight logical channels ordered as `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`;
- 16 ordered leaves as eight `INT32 value + Quality` pairs;
- 64-byte sample payload;
- configurable destination MAC, APPID, VLAN presence/VID/PCP, `svID`, `confRev` and supported ASDU field-presence options;
- rational GPTimer scheduling for rates that are not an integer number of microseconds, including 4800 fps;
- truthful `smpSynch=0` until measured synchronization exists.

Structurally valid profiles outside this boundary may still be parsed and explained by the host compiler, but deployment must remain blocked rather than guessing an embedded payload layout.

## Canonical stream versus diagnostic mirror

The firmware currently transmits two distinct streams during bench validation:

```text
canonical stream
    SCL-derived multicast destination
    SCL-derived APPID
    SCL-derived svID
    configured VLAN/PCP
    standards-facing identity

 diagnostic mirror
    broadcast destination
    APPID 0x4F01
    svID AR_DIAG_SV1
    untagged
    capture/debug identity only
```

The diagnostic mirror exists because the current host USB-Ethernet capture path does not reliably expose the canonical multicast/priority-tagged stream. The diagnostic identity is intentionally different so a tool cannot confuse the mirror with the configured process-bus stream.

Seeing `AR_DIAG_SV1` in the current analyzer therefore does **not** mean the SCL profile failed to deploy. It means the analyzer is observing the lab mirror while the canonical SCL stream is transmitted in parallel.

## What is not yet claimed

The following remain outside the functional-complete statement:

- trusted on-wire proof of canonical multicast destination, APPID, `svID` and VLAN/PCP on an independent raw capture path;
- formal IEC 61850-9-2, IEC 61869-9 or legacy profile conformance;
- hardware-TX timestamp statistics;
- IEC/IEEE 61850-9-3 PTP discipline and synchronized `smpSynch` behavior;
- universal engineering scaling inferred from arbitrary SCL files;
- arbitrary DataSet layouts and arbitrary basic-type combinations on the embedded target;
- multi-stream publication;
- fault/scenario scripting and COMTRADE playback;
- production packaging, signed desktop distribution and long-duration qualification;
- independent subscriber/relay interoperability matrix.

These are follow-on engineering phases, not evidence that the current injector workflow is unfinished.

## Next hardware phase: independent analyzer node

A second ESP32-P4 Ethernet board is a suitable next validation node.

The intended analyzer architecture is:

```text
process-bus Ethernet
        |
        v
ESP32-P4 raw EMAC RX
        |
        +-- promiscuous / all-multicast receive
        +-- preserve and parse IEEE 802.1Q tags
        +-- classify EtherType 0x88BA
        +-- decode SV APPID / ASDU / svID / smpCnt / confRev / quality
        +-- calculate continuity and timing evidence
        |
        v
Observed-wire profile
        |
        v
GUI configured-vs-observed comparison
```

The analyzer can only inspect frames that physically reach its Ethernet port. In a switched process-bus lab, use a trusted TAP or managed-switch mirror/SPAN port that preserves VLAN tags.

The goal is to remove the current Windows USB-NIC capture ambiguity and make configured-vs-observed evidence a first-class ARStack61850 capability.

## Parallel synchronization phase: PTP Grandmaster Clock

PTP Grandmaster work is now a separate stacked track so synchronization can advance without reopening the completed injector core.

The dedicated roadmap is `docs/PTP_GMC_ROADMAP.md` and implementation is tracked by issue #56 under parent PTP/SV roadmap #24.

The sequence is intentionally evidence-first:

```text
ESP32-P4 EMAC hardware clock/timestamp adapter
        -> portable PTP wire codec
        -> passive PTP monitor
        -> LAB GMC Announce/Sync/Follow_Up
        -> independent receiver evidence
        -> explicit clock-source/traceability state
        -> anchor SV sample timeline to the same PTP hardware clock
        -> only then derive smpSynch from measured state + sourced profile rules
```

The PTP implementation must start from `feature/smv-scl-profile-bridge` and remain a stacked branch. It must preserve the established 4000/4800 fps injector behavior, keep PTP state out of the SV hot path, and retain `smpSynch=0` until synchronization is actually evidenced.

A free-running laboratory grandmaster, a host-seeded clock and a traceable external-reference grandmaster are different states and must never be collapsed into one `locked` claim.

## Product interpretation

The injector has crossed the boundary from a fixed bench waveform generator to an **SCL-driven IEC 61850 Sampled Values injection instrument** for the current supported profile.

The next work should focus on verification depth and profile breadth rather than re-opening the already proven core workflow:

```text
Configured engineering intent
        -> deterministic injector
        -> independent raw analyzer
        -> configured-vs-observed evidence

and in parallel:

EMAC hardware time
        -> PTP GMC
        -> PTP-anchored SV timeline
        -> truthful synchronization evidence
```

No decoded packet, successful bench run, or visible PTP traffic by itself is treated as a formal conformance certificate.