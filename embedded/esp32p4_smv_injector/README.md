# ESP32-P4 Ethernet Sampled Values Injector

This target runs the ARStack61850 Sampled Values injector on the current ESP32-P4 Ethernet development board using the internal EMAC and onboard RMII PHY.

The firmware has progressed beyond the original fixed 4000 fps bring-up. The current runtime accepts a bounded immutable SCL-compiled publisher profile from the host GUI, arms that profile while STOPPED, and publishes deterministic Sampled Values while live signal values remain mutable.

See:

- [`ARCHITECTURE.md`](ARCHITECTURE.md) for the standards-first system boundaries;
- [`../../docs/SMV_INJECTOR_STATUS.md`](../../docs/SMV_INJECTOR_STATUS.md) for the current completion statement and physical bench evidence.

## Current hardware result

The current ESP32-P4 path has been proven on real hardware:

- ESP32-P4 rev 1.3 boots and drives the onboard RMII PHY;
- raw Layer-2 TX reaches an independent host capture path;
- IEC 61850 Sampled Values are decoded from the generated `0x88BA` traffic;
- the development 4000 fps profile has sustained stable device telemetry with zero TX failures and zero missed timer notifications in retained runs;
- the SCL-driven runtime has physically transitioned to a real engineering-file-derived 4800 fps profile;
- the 4800 fps retained run reported approximately 4800 samples/s, zero canonical TX failures and zero missed slots;
- the active profile exposed SCL-derived `svID`, APPID, VLAN/PCP intent, configuration revision, publisher rate and counter modulus in the GUI;
- three-phase current/voltage waveforms remained observable through the diagnostic capture path;
- profile identity/layout is immutable while RUNNING, while magnitude, phase, frequency, quality and channel enable state remain live controls.

This proves the injector workflow for the supported profile boundary. It does not by itself prove formal IEC conformance or trusted on-wire visibility of every canonical multicast/VLAN field.

## Runtime profile model

The embedded runtime currently accepts a bounded `RuntimePublisherProfile` containing:

- destination MAC;
- APPID;
- VLAN present/VID/PCP;
- configuration revision;
- publisher rate;
- sample-counter modulus;
- `nofASDU`;
- supported ASDU field-presence flags;
- `svID`;
- DataSet reference when present;
- profile generation.

The supported embedded DataSet layout is currently:

```text
Ia, Ib, Ic, In, Ua, Ub, Uc, Un
```

with eight `INT32 sample + UINT32 quality` pairs for a 64-byte sample payload.

Profiles that cannot be mapped safely to this bounded layout must be rejected rather than guessed.

## Runtime state rule

```text
STOPPED
   |
   +-- receive / validate immutable publisher profile
   |
   +-- COMMIT / ARM
   |
   v
RUNNING
   |
   +-- magnitude may change live
   +-- phase may change live
   +-- frequency may change live
   +-- quality may change live
   +-- channel enable may change live
   |
   +-- MAC / APPID / VLAN / svID / DataSet / rate / layout remain immutable
   |
   v
STOPPED
```

A structural profile change therefore requires `STOP -> validate -> deploy -> arm`.

## Rational sample scheduler

The publisher uses a 1 MHz GPTimer and a rational one-shot scheduling cursor instead of permanently rounding fractional-microsecond periods.

Examples:

```text
4000 fps -> 250 us, 250 us, 250 us, ...

4800 fps -> 208 us, 208 us, 209 us, ...
```

This preserves the requested long-term event rate in the timer domain.

The realtime TX path prebuilds the SV frame template. Per sample it patches only bounded fixed-width fields such as `smpCnt`, sample values and quality; it does not perform XML parsing or rebuild the full BER structure in the hot loop.

## Canonical stream and diagnostic mirror

During bench validation the firmware transmits two distinct streams.

### Canonical stream

The canonical stream uses the active runtime profile:

- configured multicast destination MAC;
- configured APPID;
- configured VLAN/PCP when enabled;
- configured `svID`;
- configured `confRev`;
- configured publisher rate;
- configured `smpCnt` modulus;
- supported configured ASDU field-presence policy.

This is the standards-facing stream identity.

### Diagnostic mirror

The separate lab mirror currently uses:

- broadcast destination `FF:FF:FF:FF:FF:FF`;
- APPID `0x4F01`;
- `svID=AR_DIAG_SV1`;
- untagged Ethernet;
- the same live waveform source;
- the active runtime publisher rate;
- the active configuration revision;
- an independent diagnostic sample counter.

The tested Windows USB-Ethernet capture path reliably shows the broadcast mirror but does not reliably expose the canonical multicast/priority-tagged stream. The mirror therefore exists only as a visibility/debug aid.

An analyzer showing `AR_DIAG_SV1` is observing the diagnostic mirror, not the configured canonical SCL identity.

## Synchronization truth

The current firmware keeps `smpSynch=0` because no measured PTP-disciplined synchronization has yet been established.

A publisher rate, stable waveform or software task timestamp must never be converted into a synchronization claim.

## Board wiring used

| Signal | GPIO |
|---|---:|
| PHY RESET | 51 |
| MDC | 31 |
| MDIO | 52 |
| RMII REF_CLK input | 50 |
| TX_EN | 49 |
| TXD0 | 34 |
| TXD1 | 35 |
| CRS_DV | 28 |
| RXD0 | 29 |
| RXD1 | 30 |
| PHY address | 1 |

## Toolchain

Use ESP-IDF 5.5.x.

The currently tested board is ESP32-P4 rev 1.3, so its local `sdkconfig` must retain the pre-v3 chip-revision setting required by that silicon revision.

Do not use `fullclean`, delete a working `sdkconfig`, or change target/revision settings merely to repeat a normal build.

## Build and flash

```bash
cd embedded/esp32p4_smv_injector
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

For the current bench the serial port has been `COM3`.

A current SCL-profile-bridge boot includes output similar to:

```text
Live signal engine ready: profile=... rate=... fps ...; use GUI START
GUI profile deployment uses bounded PROFILE subcommands while STOPPED.
```

After the GUI deploys a profile and START is requested, the publisher reports an armed profile and begins rate-specific telemetry.

## Host control path

The normal operator path is the local GUI:

```text
Import SCL / CID
    -> host C++ profile compiler
    -> Class A/B/C diagnostics
    -> Deploy while STOPPED
    -> START
    -> live value changes
    -> STOP
```

The serial command parser remains a low-level bench/fallback transport and is not the primary operator UX.

## Remaining validation and hardening work

The injector functional phase is complete for the current supported 4I+4V profile. Remaining work is validation depth and profile breadth:

- trusted raw capture of the canonical multicast/VLAN stream;
- independent configured-vs-observed profile comparison;
- ESP32-P4 EMAC hardware TX timestamp evidence;
- hardware timing statistics over retained long-duration runs;
- IEC/IEEE 61850-9-3 PTP clock discipline before changing `smpSynch`;
- broader SCL DataSet/basic-type deployment support;
- engineering scaling derived from validated source information rather than generic assumptions;
- multi-stream publication;
- fault/scenario/COMTRADE workflows;
- relay/subscriber interoperability evidence;
- formal profile/conformance work using sourced normative rules and independent test evidence.

## Next hardware validation node

A second ESP32-P4 Ethernet board is a suitable candidate for an independent raw analyzer:

```text
Ethernet RX
   -> promiscuous / all-multicast receive
   -> preserve and parse VLAN tags
   -> classify 0x88BA
   -> decode SV identity and ASDU
   -> verify smpCnt continuity / rate / quality
   -> compare observed wire facts to configured SCL facts
```

In a switched lab, the analyzer still requires the relevant frames to reach its physical port. Use a TAP or managed-switch mirror/SPAN configuration that preserves the traffic and VLAN tags.

Use an isolated bench network. Do not connect this development image to an operational process bus.