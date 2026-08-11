# PTP Lab Timing Companion

This document defines the PTP role in ARStack61850 and the evidence boundary for using it with the ESP32-P4 Sampled Values Injector and future analyzer products.

## Product role

ARStack PTP is a **laboratory and field-troubleshooting timing companion**.

A common process-bus commissioning problem is:

```text
relay / subscriber requires PTP before it will accept or use Sampled Values
                              |
                              v
                    station GPS/PTP server
                     not installed yet,
                  unavailable, or suspect
                              |
                              v
               SMV injection cannot proceed
```

The ARStack ESP32-P4 path is intended to close that temporary test gap:

```text
                    ARStack ESP32-P4
                    /             \
                   /               \
          Sampled Values          PTPv2 L2
             Injector         timing companion
                   \               /
                    \             /
                     protection relay
```

This makes the instrument useful during troubleshooting, internal testing and interoperability work without pretending to be the station timing system.

It is **not** a replacement for a GPS-backed time server and it is not a certified grandmaster.

## Shared Injector / Analyzer architecture

The PTP wire model belongs in the portable C++ core rather than in the ESP32 firmware or a desktop GUI.

```text
                    portable ARStack C++

              PTP codec / parser / model
                         |
             +-----------+-----------+
             |                       |
             v                       v
       passive monitor          message builder
       health / evidence       Announce / Sync /
       PCAP analyzer           Follow_Up / Pdelay
             |                       |
             v                       v
      future PTP/SMV         ESP32-P4 PTP lab
         Analyzer              timing companion
```

The same parser and semantic model therefore serve both product directions:

- **Injector** — transmit PTP traffic beside deterministic SMV traffic;
- **Analyzer** — observe PTP source/domain/message behavior without transmitting anything.

## PTP-P1 implementation boundary

PTP-P1 provides the laboratory transport/timestamp foundation.

### Portable C++ core

Implemented:

- IEEE 1588/PTPv2 Layer-2 EtherType `0x88F7`;
- PTP header encode/decode;
- Announce;
- Sync;
- Follow_Up;
- Pdelay_Req encoding;
- Pdelay_Resp;
- Pdelay_Resp_Follow_Up;
- VLAN parsing;
- QinQ parsing;
- general and peer-delay multicast recognition;
- passive source/domain/message monitor;
- sequence-anomaly tracking;
- conservative health and `smpSynch` policy primitives;
- read-only PCAP analyzer CLI.

### ESP32-P4 hardware timing path

When `CONFIG_AR_PTP_LAB_TX=y` on the current ESP-IDF 5.x adapter:

1. the ESP32-P4 IEEE1588 hardware clock is enabled;
2. Announce is emitted at the configured interval;
3. two-step Sync is transmitted;
4. the actual EMAC TX descriptor timestamp of Sync becomes the Follow_Up precise-origin timestamp;
5. incoming Pdelay_Req frames are received through the timestamp-aware Ethernet input callback;
6. the actual EMAC RX descriptor timestamp becomes the Pdelay_Resp request-receipt timestamp (`t2`);
7. the actual EMAC TX descriptor timestamp of Pdelay_Resp becomes the Pdelay_Resp_Follow_Up response-origin timestamp (`t3`).

The PTP task runs separately from the Sampled Values realtime task. It does not move PTP parsing, queue handling or response construction into the SMV hot loop.

## Configuration

PTP transmission is **off by default**.

Relevant ESP32-P4 configuration options:

```text
CONFIG_AR_PTP_LAB_TX
CONFIG_AR_PTP_DOMAIN
CONFIG_AR_PTP_ANNOUNCE_INTERVAL_MS
CONFIG_AR_PTP_SYNC_INTERVAL_MS
```

Current defaults when the feature is enabled:

```text
Domain              0
Announce interval   1000 ms
Sync interval       250 ms
Two-step clock      yes
Clock class         248
Clock accuracy      unknown
Variance            0xFFFF
Time source         internal oscillator
Priority1           128
Priority2           128
```

These clock-quality values are intentionally conservative. The lab helper must not advertise GPS traceability or calibrated accuracy it does not possess.

## `smpSynch` safety rule

PTP traffic visibility and Sampled Values synchronization claims are different facts.

```text
PTP packets transmitted
        !=
measured synchronized Sampled Values clock
```

PTP-P1 therefore does **not** automatically promote canonical SMV `smpSynch` merely because the local PTP broadcaster is enabled.

The portable ASDU default is fail-safe:

```text
smpSynch = 0
```

A future automatic `smpSynch` transition requires measured clock state and an explicit profile rule.

Compatibility overrides may later be exposed for controlled laboratory use, but they must remain visibly distinct from measured synchronization.

## Read-only PTP Analyzer CLI

The portable stack includes:

```text
ariec61850_ptp_analyze <capture.pcap> [--domain N] [--json]
```

It reports, among other evidence:

- PTP versus non-PTP frame counts;
- observed domain;
- source port identity;
- Announce count;
- Sync count;
- Follow_Up count;
- Pdelay activity;
- VLAN/QinQ visibility;
- sequence anomalies;
- basic liveness/health checks.

Host PCAP timestamps are observation timestamps, not proof of PTP clock accuracy. The analyzer deliberately does not convert a normal host capture into a precision-timing claim.

## Current RX ownership note

The ESP32-P4 injector is currently a standalone raw Layer-2 application with no competing TCP/IP input consumer. PTP-P1 registers the timestamp-aware Ethernet RX callback and owns/frees the received buffers it inspects.

A future combined PTP/SMV analyzer or application with multiple RX consumers should introduce one shared raw-Ethernet dispatcher rather than registering competing Ethernet input paths.

## Bench acceptance — PTP-P1

Software/CI success is not physical relay acceptance. The first hardware interoperability run should retain packet and device evidence for the following.

### A. Broadcaster visibility

On an independent capture path verify:

- EtherType `0x88F7`;
- expected source MAC / clock identity;
- configured PTP domain;
- periodic Announce;
- periodic Sync;
- matching Follow_Up sequence IDs;
- two-step flag on Sync;
- non-zero, progressing Follow_Up hardware timestamps after the hardware clock is initialized.

### B. Peer-delay exchange

When the relay emits Pdelay_Req verify:

```text
Relay              ARStack ESP32-P4
  |                       |
  |---- Pdelay_Req ------>|  RX descriptor timestamp = t2
  |                       |
  |<--- Pdelay_Resp ------|  body carries t2
  |                       |  TX descriptor timestamp = t3
  |<-- Pdelay_Resp_FU ----|  body carries t3
```

Acceptance evidence should show:

- response sequence ID equals request sequence ID;
- requestingPortIdentity is echoed exactly;
- response and follow-up use the peer-delay multicast destination;
- timestamps are hardware-derived and valid;
- no sustained response loss under the relay's normal Pdelay cadence.

### C. Relay behavior

Record, without overclaiming:

- whether the relay detects the ARStack timing source;
- whether it selects/locks to it in the isolated test setup;
- time until relay timing-ready state;
- whether SMV becomes readable/usable after PTP is available;
- any relay-specific profile mismatch or rejection diagnostic.

A successful result proves interoperability with that tested relay/configuration. It is not a universal IEC/IEEE 61850-9-3 conformance certificate.

### D. SMV non-regression

With PTP and SMV active simultaneously retain the existing SMV telemetry and verify:

- requested publisher rate remains stable;
- no new canonical TX failure trend;
- no sustained missed-slot increase;
- canonical SMV identity and VLAN/APPID remain unchanged;
- PTP processing stays outside the realtime SMV hot path.

## Suggested capture filters

Useful Wireshark display filters for the bench are:

```text
ptp
eth.type == 0x88f7
eth.type == 0x88ba
ptp || eth.type == 0x88ba
```

For evidence, keep the unfiltered raw capture whenever possible and use filters only for inspection.

## PTP-P2 direction

PTP-P1 intentionally stops before claiming disciplined synchronization. The next timing phase should add:

1. explicit lab epoch/time seeding rather than assuming ESP system UTC is valid;
2. hardware timestamp telemetry/correlation;
3. external PTP observation path;
4. applicable power-profile validation;
5. peer-delay / offset calculations where ARStack operates as a synchronized client/source;
6. hardware-clock phase/frequency adjustment;
7. lock / holdover / unlocked state machine;
8. measured-state-driven `smpSynch` policy;
9. retained offset/jitter evidence.

The relevant ESP-IDF 5.x hardware adapter already exposes hardware-clock time and phase/frequency adjustment commands, but those controls should only be used once the servo and evidence model are explicitly designed and tested.

## Claim boundary

PTP-P1 may be described as:

> ESP32-P4 hardware-timestamped PTPv2 Layer-2 laboratory timing companion for Sampled Values troubleshooting and interoperability testing.

PTP-P1 must **not** be described as:

- GPS grandmaster replacement;
- GPS-traceable clock;
- calibrated precision time source;
- certified IEC/IEEE 61850-9-3 implementation;
- universal relay-compatible grandmaster;
- evidence that SMV `smpSynch=2` is automatically justified.

The product value is practical: make process-bus testing possible when normal station timing infrastructure is temporarily unavailable, while keeping every synchronization claim honest and evidence-based.
