# ARStack Studio v0.1.1 — PTP production integration

Status: **release candidate integration**.

## Purpose

v0.1.1 promotes the already-implemented ARIEC61850/ARSVIN-derived PTP stack into the normal ESP32-P4 production firmware instead of shipping it compiled out. No second PTP engine is introduced.

## Production default

The normal firmware image compiles `CONFIG_AR_PTP_LAB_TX=y` and starts in `LAB_SOURCE` mode with conservative ARSVIN-compatible defaults:

- Layer-2 PTPv2 (`0x88F7`)
- domain 0
- `transportSpecific=0`
- untagged PTP by default
- Announce every 1000 ms
- two-step Sync every 250 ms
- Pdelay response enabled
- clockClass 248
- clockAccuracy Unknown
- internal-oscillator time source

The ESP32-P4 adapter uses its IEEE1588 hardware clock and hardware TX/RX descriptor timestamps for the existing two-step Sync and peer-delay paths.

## Roles retained

- `LAB_SOURCE` — broadcasts Announce / Sync / Follow_Up and answers peer delay.
- `TIME_RECEIVER` — external-source selection, hardware timestamp correlation, path/offset measurement and bounded clock discipline.
- `MONITOR` — passive timing observation.

Role changes remain controlled by the existing stopped-only profile path.

## smpSynch truth boundary

`LAB_SOURCE` does **not** make the locally generated SMV clock externally disciplined. `AUTO` therefore remains `smpSynch=0` in source/monitor mode. Only measured `TIME_RECEIVER` discipline may promote AUTO to 1/2 according to the existing P2 evidence policy. Forced 0/1/2 values remain explicit laboratory stimuli.

## Product capability contract

The v0.1.1 semantic identity and firmware manifest advertise `PTP-P2` and `SMPSYNCH-AUTO`. Studio v0.1.1 requires these capabilities in addition to the existing SMV/live/session contract, so an older PTP-less production image cannot silently present as current.

## Release gate

Software CI must prove:

1. the normal production firmware build has PTP-P2 enabled;
2. the dedicated PTP matrix still builds SOURCE and TIME_RECEIVER;
3. the Windows Studio/package contract accepts the v0.1.1 firmware identity and manifest;
4. no IED Simulator source is changed by this integration.

Physical closeout is intentionally short and product-focused: verify visible Announce/Sync/Follow_Up, matching Pdelay response when requested, and simultaneous SMV 4000 fps with zero missed/TX failures over a practical bench interval. This is an interoperability/product claim, not GPS traceability or formal IEEE/IEC conformance certification.
