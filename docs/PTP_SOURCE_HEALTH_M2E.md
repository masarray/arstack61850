# M2E — IEC/IEEE 61850-9-3 lab SOURCE readiness and cadence hardening

Engineering scope: default-off experimental phase tracking remains unchanged; public release remains untouched. M2E fixes two concrete *PTP SOURCE* faults upstream of the SV phase servo, and enforces standards-oriented nominal SOURCE cadence by default.

## The failures

1. Before M2E, `g_ptp_ready` could become true on ANY successful TX (including Pdelay_Resp) once lifetime totals for Announce/Sync/Follow_Up were each nonzero. After failed Sync or missing Follow_Up, the SOURCE could requalify without new Sync evidence. This was not an accurate time-authority readiness contract.
2. `PtpPublisherRuntime::poll_due` set `next_due = poll_now + interval`. A task running e.g. 7ms late each second accumulates nominal-source cadence error; long stalls shift the phase permanently. This is not a PTP epoch alignment mechanism, but unnecessarily drifts the protocol cadence.

## Implemented controls

- New portable **`PtpSourceReadinessGate`** in the existing PTP power-profile header: SOURCE qualifies only after an Announce and a *complete* hardware TX timestamped Sync + Follow_Up pair that have both succeeded since the last failed Ethernet/PTP send. Failure of a Pdelay exchange also invalidates the current evidence; later successful Pdelay alone **never** requalifies READY.
- `g_ptp_source_generation` increments **only on a fresh gate transition to READY**, allowing the existing phase and MAC egress evidence paths to reject a SOURCE restart. No stale lifetime counters are consulted for readiness.
- The portable SOURCE scheduler now advances by integer multiples of the **original monotonic deadline**. A 1.007s poll does not shift the next deadline to 2.007s; the next deadline remains 2.000s. A long stall sends **one** due message pair, not catch-up bursts.
- New default-on **strict SOURCE admission**: Announce 1s, Sync 1s, hardware peer-delay responder enabled (`inspect_ptp_power_profile_cadence` reused). Does not hardcode PTP domain/transport/VLAN; settings still must match the receiving IED. The explicit `CONFIG_AR_PTP_ALLOW_NONSTANDARD_LAB_CADENCE=y` permits isolated nonstandard diagnosis without silently labelling it a power-profile default.
- One opt-in Draft CI job (no new workflow) compiles and runs portable runtime, power-profile, phase and TX-evidence regressions with `-Werror` and builds ESP32-P4 SOURCE using ESP-IDF 5.5.4.

## What this does *not* certify

- The task scheduler cadence is NOT hard-phase-locked to the PTP second boundary and software wake latency still exists. The phase-authority/egress work is experimental and requires hardware measurement.
- SOURCE `clockClass=248`, `clockAccuracy=unknown`, `timeSource=internalOscillator` and not traceable remain truthful; the IED is entitled to reject a non-traceable master when it needs a traceable one. Do not lower alarm thresholds or spoof an external reference.
- A zero alarm claim, IEEE/IEC conformance certificate, PTP BMCA conformance or physical 1us-grade synchronisation requires external clock/IED evidence.
- Assessed against IEC/IEEE 61850-9-3:2016 cadence requirements and independent vendor descriptions:
  - https://standards.ieee.org/ieee/61850-9-3/6167/
  - https://infocenter.nokia.com/public/7705SAR234R1A/topic/com.nokia.basic-system-guide/iec-ieee-61850-9-3-c37-238-2017.html
  - https://microchip-ung.github.io/bsp-doc/bsp/2026.09/ptp.html
