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
