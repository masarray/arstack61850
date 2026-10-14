# IEC 61850 PTP + SV interoperability — software-only evidence matrix

Status: **in progress, not a conformance certificate.** This matrix is designed for teams without a reference IED or timing analyzer. A passing host/ESP-IDF build demonstrates software consistency, not physical accuracy or relay synchronization.

## Sources and version boundaries

- [IEC/IEEE 61850-9-3:2016](https://webstore.iec.ch/en/publication/24998): IEEE 1588-2008 L2 PTP / peer-to-peer delay profile.
- [IEC 61850-9-2:2011 + AMD1:2020](https://webstore.iec.ch/en/publication/66549): Sampled Values Ethernet mapping.
- [IEC 61869-9:2016](https://webstore.iec.ch/en/publication/24663): digital interfaces for instrument transformers and application-specific synchronization/sample behavior.
- Later drafts or secondary summaries cannot silently override the approved 2016/2020 baselines. Verify profile-specific interpretations against controlled standards copies before making claims.

## Evidence taxonomy

- `STATIC`: inspected source and build configuration.
- `SOFTWARE TEST`: decoded-wire regression driven by the actual portable encoder.
- `EMBEDDED CI`: compilation for pinned ESP-IDF/ESP32-P4 SDK.
- `PHYSICAL`: needs hardware/reference clock/IED; **not available in this phase**.
- `GAP`: known missing/incomplete implementation or evidence; do not market as IEC conformance.

| Feature / condition | Required contract or expectation | Evidence / state |
| --- | --- | --- |
| PTP transport | Ethernet L2 EtherType 0x88F7, multicast, v2 | SOFTWARE TEST: portable PTP frame roundtrip and Ethernet destination |
| PTP delay mechanism | Peer-to-peer delay; reply carries valid RX and TX hardware timestamps | STATIC: Pdelay response path; SOFTWARE TEST frame fields; PHYSICAL pending |
| Announce and Sync | 1 Hz nominal defaults (logMessageInterval=0) | STATIC + SOFTWARE TEST: both 1000 ms |
| Pdelay_Req rate | Receiver requests 1 Hz; SOURCE answers correctly | STATIC for receiver and source; PHY timestamp proof pending |
| PTP domain | Default 0, must match subscriber; 93 is a documented alternative to avoid collisions | STATIC + SOFTWARE TEST, never force one domain universally |
| PTP transportSpecific/VLAN | Must agree with IED and network/engineering plan | Configuration preserved in codecs; no universal nibble or VLAN assumption |
| Clock identity | Stable and nonzero source identity; Follow_Up sequence matches Sync | SOFTWARE TEST |
| Clock class / traceability | No unproven grandmaster quality: lab oscillator is class 248, clockAccuracy unknown | STATIC, checked against encoded Announce |
| PTP timescale and UTC properties | Valid PTP/TAI epoch, UTC offset flags and source provenance must agree | **GAP**: current LAB_SOURCE seeds EMAC from system UTC seconds, does not establish a proven TAI/UTC epoch or truthfully encoded timescale. Never claim 9-3 conformance yet |
| Full PTP ordinary-clock/BMCA behavior | Select/announce/step/holdover/recover according to applicable clock type | **GAP** for a standards-complete Grandmaster; current LAB_SOURCE is a limited laboratory source |
| SV canonical Ethernet stream | 0x88BA, engineered destination, APPID, svID, confRev, VLAN, dataset ordering | Portable codec/profile tests exist; independent canonical-wire acceptance pending |
| 4000/4800 sampling | Correct rational cadence and modulus for the **explicitly chosen** 80-samples/cycle family | SOFTWARE TEST: 120 s mathematical schedule, zero accumulated tick drift |
| Higher IEC 61869-9 profiles | Correct samples/sec vs ASDUs/frame distinction, engineered nofASDU & payload type | **GAP** for embedded runtime generality. 4000 fps/1 ASDU is not a universal IEC 61869-9 profile |
| Counter epoch | `smpCnt=0` at valid profile-specific synchronization event; consistent on loss/restart | M1 only coarse first-arm to EMAC second; **GAP** continuous timebase tracking |
| `smpSynch` declaration | May not claim local/global qualified synchronization solely from PTP packet/clock lock | SOFTWARE TEST: AUTO fail-closed until sample phase is qualified |
| SV hardware TX timing | Measured sample deadline vs actual MAC departure, including P95/P99 and drift | **GAP** without device timestamped SV evidence |
| Actual IED acceptance/no alarm | Vendor IED locks PTP and accepts canonical SV continuously | PHYSICAL pending; cannot be inferred from builds or synthetic PCAP alone |

## Software-only acceptance for the current PR

1. Linux GCC and Clang; Windows MSVC: portable PTP, PTP power-profile wire test, epoch/schedule and sync-gate regressions.
2. ESP32-P4 build matrices: normal SV + PTP enabled, SOURCE/RECEIVER/monitor variants, validated firmware contract.
3. Native Qt ARStack Studio Windows build and installer packaging.
4. Static checks: default Source Sync/Announce/Pdelay are 1 s, source class remains 248, AUTO `smpSynch` is fail-closed.
5. No merged `main` or public title such as "fully 9-3 compliant" until remaining GAPs are fixed and physical evidence exists.

## Next coding tranche, ordered by standards risk

1. **PTP time-properties correctness:** explicitly model PTP/TAI epoch, trusted UTC-offset provenance, Announce timescale/timeTraceable/currentUtcOffsetValid flags, source clock quality; never silently convert an uninitialized system clock to a traceable Grandmaster.
2. **Reusable PTP/SV timeline authority:** continuously compare and correct GPTimer phase against EMAC hardware time at a bounded rate; handle drift, clock steps, PTP restart, source switches, and holdover without pretending alignment.
3. **Profile-targeted SV compiler:** exact profile-specific sample counter reset, ASDU grouping, nofASDU, dataset layout and scaling from SCL + documented source profile. Do not hard-code 4000/4800 for arbitrary SCL.
4. **Passive interoperability fixtures:** anonymous golden frames/PCAP and independent decoder checks for 9-2LE-compatible streams and a separately supported IEC 61869-9 variant. Report mismatches rather than automatic coercion.
5. Hardware acceptance only when equipment becomes available.

**Source-of-truth policy:** a standards-oriented claim is never promoted from STATIC/CI to PHYSICAL. The laboratory fallback `smpSynch=0` is deliberate and must not be overridden to silence an IED alarm.
