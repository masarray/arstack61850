# M3C.1 — Continuous integer PTP-second binding for SV zero epochs

Date: 2026-10-15. Base: M2F PR #157 at `90062f391a9c97cb1ff20d072b7f15ba4a777fac`. This is a stacked Draft checkpoint, not a merge/release or proof of PTP compliance.

## Bug / evidence gap
The M2A software phase observer previously normalized phase modulo 1 second. It checked GPTimer elapsed intervals and source/session generations, but it did not retain the identity of the integer PTP second corresponding to each ISR-published sample-zero deadline. Phase modulo 1 second is not by itself an epoch-continuity proof.

## Correction
`SvPtpPhaseAuthority` now derives and stores the nearest **integer PTP second** for the last SV sample-zero GPTimer deadline, using the bracketed EMAC PTP read and signed low-32 GPTimer delta. The first valid observation establishes a baseline only. A following observation binds only if its sample-zero generation AND integer PTP-second identity both advance **exactly one**. Missed epochs, clock steps or impossible labels invalidate and clear the PI integrator and any pending correction.

The existing ESP32-P4 SOURCE control-plane observer now publishes ISR phase-steering validity only after this continuous binding; on first observation it deliberately publishes zero correction. No new runtime timer, heap allocation, per-frame PTP IOCTL, software mutex on the ISR, PTP servo or SmpSynch upgrade was added.

## Focused regression
`sv_ptp_phase_authority_tests`: baseline vs continuous qualification, +1 s / -1 s PTP clock step, missing SV epoch, reacquisition, observation spanning the PTP-second rollover, and GPTimer 32-bit wrap. Existing ±80 ppm 4000/4800 fps tracking and fault simulations remain in the same suite.

## Scope and next gating
This is M3C **foundation**, NOT completion of the M3A canonical dual-mode runtime authority. It currently guards the opt-in SOURCE servo; RECEIVER phase correlation, clock-authority selection/switching, absolute PPS/smpCnt physical accuracy, MAC egress, non-default SV profile epoch semantics, full M3 recovery and legitimate AUTO smpSynch remain separate engineering gates. The firmware experiment stays default OFF and native ARStack Studio is unchanged. A successful build/test establishes software verification only; hardware/IED acceptance remains unverified.

## M3C.3 — Bounded confidence on software phase tracking (2026-10-17)

The previous software-tracking decision checked only the nominal GPTimer/PTP
phase residual against the configured threshold. It could report
`software_tracking=true` after consecutive 400-µs clock-read brackets even
though the corresponding measurement uncertainty was at least 201 µs,
larger than the default 60-µs tracking threshold.

The qualification counter now advances only when
`abs(measured_phase_error_us) + observation_uncertainty_us` is within the
configured software-tracking threshold. One low-confidence observation revokes
tracking. Integer-second continuity, the separate phase-observation acceptance
limit, bounded PI steering, ISR work and firmware feature gates are unchanged.
This is a software confidence bound, not a hardware egress measurement or
authorization to assert `smpSynch=1/2`.

Portable regression includes six accurate-phase/uncertain-clock observations,
four fresh narrowly bracketed reacquisition observations, and confidence
demotion after one degraded bracket. Hardware bench acceptance is still absent.

## M3C.5 — Vendor-neutral AUTO local-sync qualification (2026-10-17)

No new operator prompts; source PTP On → SMV Start still operates directly.
The IEC 61850-9-2 local sync declaration (`smpSynch=1`) is not the same
as merely sending PTP frames. IEC/IEEE 61850-9-3 defines the applicable
power-utility PTP profile. The SIPROTEC 5 process-bus manual confirms
`Accepted SmpSynch=1` is an intentional local-source interoperability mode
when merging unit and relay share a time reference; ARStack contains no
Siemens-specific wire quirk.

A **SOURCE-only, explicit hardware-evidence build** can now promote AUTO to
local `1` only when ALL gates pass: stable PTP SOURCE generation, coherent
second-bound GPTimer sample-zero observations, software tracking with the
measured absolute phase plus read uncertainty <=4us, two adjacent coherent
16-second comparisons of genuine canonical SV MAC TX descriptor timestamps
both within +/-4us, no publish faults/missed slots/descriptor stalls, and
current hardware evidence age <=17 sample epochs. These 4us values are a
conservative project admission policy, not a universal IEC profile claim.
When disabled or insufficient, AUTO stays `0`; global `2` is never implied.

The gate revokes on STOP, role change, missed sample, failed transmission,
wrong generation, discontinuous PTP epoch, stale evidence or unqualified
software phase. No heap, blocking IO, or additional work in the GPTimer ISR.
The sparse actual-frame descriptor poll remains a possible publisher timing
disturbance; evaluate actual board and relay traces before release.

SOURCE and RECEIVER remain distinct: this milestone qualifies SOURCE local
evidence only; receiver `LOCKED` without equivalent measured SV egress
does not promote AUTO. Successful firmware CI does not establish physical
phase accuracy or guarantee zero relay sync alarms.

External specifications:
- https://standards.ieee.org/ieee/61850-9-3/6167/
- https://cache.industry.siemens.com/dl/files/460/109742460/att_1135746/v1/SIP5_Processbus_V09.50_Manual_C054-D_en.pdf?download=true
