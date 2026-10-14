# ESP32-P4 SV/PTP synchronization — M1 engineering evidence

**Status: software integration candidate. NOT relay-accepted.** Base: `main@9e70a4937874`.

## Scope / why alarms can still occur

IEC 61850-9-2 Amendment 1:2020 specifies the meaning of SV synchronization declarations, but the counter reset event and acceptable sample-time error depend on the applicable application/product profile (e.g. IEC 61869-9). IEC/IEEE 61850-9-3:2016 is an L2/P2P PTP profile with 1-second Announce / Sync / Pdelay intervals and 3 Announce timeout intervals. A free-running local oscillator is **not** GNSS/UTC traceable; do not promote a lab source to `clockClass=6` or global `smpSynch=2`.

Authoritative references:
- https://webstore.iec.ch/en/publication/24998
- https://standards.ieee.org/ieee/61850-9-3/6167/
- https://www.omicron-lab.com/fileadmin/assets/Precision-Timing/Knowledge_Applications/Article_IEC_61850-9-3/IEC_61850-9-3_-_Will_simplicity_supersede_complexity_.pdf
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
