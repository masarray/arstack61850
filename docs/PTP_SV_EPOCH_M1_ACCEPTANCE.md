# ESP32-P4 SV/PTP synchronization — M1 engineering evidence

**Status: software integration candidate. NOT relay-accepted.** Base: `main@9e70a4937874`.

## Scope / why alarms can still occur

IEC 61850-9-2 Amendment 1:2020 specifies the meaning of SV synchronization declarations, but the counter reset event and acceptable sample-time error depend on the applicable application/product profile (e.g. IEC 61869-9). IEC/IEEE 61850-9-3:2016 is an L2/P2P PTP profile with 1-second Announce / Sync / Pdelay intervals and 3 Announce timeout intervals. A free-running local oscillator is **not** GNSS/UTC traceable; do not promote a lab source to `clockClass=6` or global `smpSynch=2`.

Authoritative references:
- https://webstore.iec.ch/en/publication/24998
- https://standards.ieee.org/ieee/61850-9-3/6167/
- IEC/IEEE 61850-9-3 power-utility timing profile (normative protocol reference)
- https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/gptimer.html

## M1 implemented

1. Production PTP source defaults to 1 Hz Sync, 1 Hz Announce, and enables Pdelay response. Receiver Sync timeout increased to 3s because a 1s timeout with 1 Hz packets can falsely enter HOLDOVER due to modest scheduling jitter.
2. Requested hardware Pdelay support must be active before PTP source reports ready; otherwise START fails closed. PTP TX failure clears readiness.
3. When a verified LAB_SOURCE is already running **before** SV START, the initial `smpCnt=0` timer alarm is approximately aligned to the next whole second of the ESP32-P4 EMAC PTP clock by a bounded cross-clock observation and safe first-delay planning. **This is coarse initial alignment, not a disciplined/slewed long-term sample timebase**; GPTimer remains an independent oscillator.
4. `smpSynch=AUTO` cannot promote a PTP clock lock into an SV synchronization claim without separately qualified SV timeline phase. This M1 does not yet produce that qualification. Forced 1/2 values are explicit laboratory injections and never evidence of measured synchronization.
5. New headless tests cover the epoch arm maths, invalid clock reads, and fail-closed sync claim gating.

## M2 required before claiming no synchronization alarm

- Develop a bounded PTP↔GPTimer continuous clock bridge, with phase/frequency correction to keep sample time aligned for long runs, including restart and phase-step behavior.
- Require the intended SV profile's exact `smpCnt` epoch policy, `smpSynch` meaning, payload order/quality/scaling, and PTP profile parameters from SCL plus applicable product standard.
- Capture actual Ethernet TX hardware timing evidence for canonical SV (not diagnostic mirror), relative to EMAC PTP time; measure peak, P95/P99 and sustained drift at 4000 and 4800 fps. If timestamp APIs stall the real-time task, use bounded sampled evidence or an independent hardware analyzer rather than adding per-frame IOCTL overhead.
- Promote AUTO to 1 **only** after local source/clock quality + sample timeline validity + profile-derived timing tolerance are measured and continuously monitored. Only a separately verifiable globally traceable source can authorize 2.
- Source-loss, multiple grandmasters/BMCA, packet drop, wrong transportSpecific/domain/VLAN, Pdelay failure, holdover, link restart and bounded transmit failures must never falsely advertise lock.
- Preserve 1h 4000 fps reference evidence, then independent 4800 fps acceptance; continuous counter and no missed slots/TX failures.
- Use isolated laboratory IED and full record: IED model/firmware, SCL, PTP settings, firmware commit, `.pcapng`, IED time-lock state, SV acceptance and exact alarm text. Do not infer no-alarm from a successful CI build.

## Operator sequencing for M1

1. Use only isolated, non-production Ethernet and approved test IED.
2. Match IED PTP mode (IEC/IEEE 61850-9-3 Layer 2, P2P), domain, transportSpecific, VLAN and multicast path. For current lab source, clockClass 248 is truthful (not externally traceable); some IEDs correctly refuse to treat this as a qualified protection-grade GM.
3. In native ARStack Studio, start PTP before SV START. Wait for Announce + Sync + Follow_Up and Pdelay readiness. Then arm SV.
4. Inspect independent canonical capture and IED diagnostics. If the IED still alarms, report the alarm rather than changing `smpSynch` or PTP clock quality to hide it.

M1 improves interoperability defaults and synchronization safety; it does **not** certify IEC/IEEE 61850-9-3 performance or guarantee a locked IED. M2 + physical bench evidence are mandatory.


## M1+ timeProperties / TAI hardening (software-only)

- Portable PtpTimeProperties now encodes IEEE 1588v2 flagField bits explicitly in Announce, Sync, Follow_Up and Pdelay; packet visibility alone does not assert traceability.
- ESP32-P4 LAB_SOURCE treats time(nullptr) as an untrusted candidate UTC clock, validates a plausible date and a bounded configured TAI-UTC offset, converts to TAI seconds, and checks the hardware seconds-register range. The EMAC PTP timescale flag is set only if the TAI epoch is actually seeded.
- currentUtcOffsetValid is FALSE by default; CONFIG_AR_PTP_MANUAL_UTC_OFFSET_VALID=y requires explicit operator validation and a successful TAI seed. timeTraceable/frequencyTraceable remain FALSE. Local source remains clockClass=248, accuracy UNKNOWN, timeSource=INTERNAL_OSCILLATOR.
- LAB_SOURCE rejects configuration that claims unverified higher grandmaster quality. Test vectors inspect wire octets independently of the existing PTP packet decoder.
- Uninitialized RTC: run as a plainly non-timescale-validated laboratory source; **do not** market as a fully conformant IEC/IEEE 61850-9-3 grandmaster.

Still missing: independent reference proving actual UTC time and leap-second offset, continuous phase/frequency coupling between EMAC PTP and SV GPTimer, mandatory clock-state/BMCA behaviors, and physical accuracy/IED acceptance.

## M3C.4 — First alarm latency hardening (17 October 2026)

The experimental epoch-arm code originally computed time to the next EMAC
PTP second **before** configuring GPTimer, then armed the later timer using
the unchanged interval. This converts driver/interrupt setup time into a
systematic SV sample-zero offset. The M3C.4 planner now retains the monotonic
end of the PTP read, subtracts measured pre-arm setup age, and rechecks the
same role/source-scoped clock lease. If too close to the intended second,
the first alarm defers by one whole second. Stale or backwards observations
do not arm a PTP-aligned timer: the publisher falls back to unqualified free
running. ISR cadence, stream counter and AUTO smpSynch fail-closed policy
are unchanged.

Regression covers a 200-us startup delay, near-epoch rollover, stale clock
evidence, reversed monotonic time and invalid alarm fields. The remaining
gap between this last pre-arm monotonic sample and actual GPTimer clock start
still needs measurement. Host Npcap receive timestamps do NOT establish MAC
egress timing; firmware and CI success do NOT authorize smpSynch=1/2 or a
public PTP-locked release. Test on the isolated ESP32-P4 bench with an
independent GM/PPS before claiming physical lock.

Bench checkpoint: PR #229 guarded SOURCE/RECEIVER artifacts are experimental images only and require board P0 revision validation, independently measured SV TX phase, and a separate stable release review.
