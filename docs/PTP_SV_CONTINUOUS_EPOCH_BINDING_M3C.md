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
