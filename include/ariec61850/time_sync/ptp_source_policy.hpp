// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ar::iec61850::time_sync {

// An operator policy is NOT a PTP worker role. These states are pure and
// deterministic; the ESP32 EMAC owner must acknowledge every emitted action.
enum class PtpSourceIntent : std::uint8_t {
    internal_gm, external_follower, auto_failover
};
enum class PtpSourceState : std::uint8_t {
    idle, starting_internal, internal_ready, acquiring_external,
    external_locked, external_loss, revoking_sv, draining_ptp,
    waiting_exclusive_emac, waiting_second_boundary, fault
};
enum class PtpSourceAction : std::uint8_t {
    none, start_internal, start_external, revoke_sv_and_pause,
    stop_current_ptp, rearm_sv_at_second, refuse_competing_gm
};
struct PtpSourcePolicyOptions final {
    PtpSourceIntent intent{PtpSourceIntent::internal_gm};
    bool external_target_selected{};
    bool fallback_authorized{};
    bool auto_restore_external{};
    std::uint32_t acquisition_timeout_ms{5000U};
    std::uint32_t loss_timeout_ms{3000U};
    std::uint32_t restore_hysteresis_ms{5000U};
};
struct PtpSourceEvidence final {
    bool link_up{};
    bool source_worker_active{};
    bool receiver_worker_active{};
    bool receiver_cleanup_pending{};
    bool internal_gm_tx_ready{};
    // Measured servo LOCKED + exact selected GM identity/domain/port match.
    bool selected_external_servo_locked{};
    // Independently verified external source; never infer this from a local
    // SOURCE Announce or from an SV smpSynch flag.
    bool independent_external_candidate_valid{};
    bool other_grandmaster_present{};
    bool sv_transmission_paused{};
    bool at_ptp_second_boundary{};
    bool sv_gm_identity_matches_active_ptp{};
};
struct PtpSourceDecision final {
    PtpSourceState state{PtpSourceState::idle};
    PtpSourceAction action{PtpSourceAction::none};
    std::uint32_t authority_generation{};
};

// Fail-closed timing-source changeover policy. Never starts two PTP workers,
// never releases SV at a fractional second, and never substitutes internal GM
// for a selected EXTERNAL_FOLLOWER unless explicit AUTO+fallback was chosen.
// This class does not actuate hardware; the exclusive EMAC role owner must
// implement and acknowledge every action before calling tick again.
class PtpSourcePolicyMachine final {
public:
    [[nodiscard]] bool configure(const PtpSourcePolicyOptions options) noexcept {
        if (state_ != PtpSourceState::idle ||
            options.acquisition_timeout_ms < 1000U ||
            options.acquisition_timeout_ms > 120000U ||
            options.loss_timeout_ms < 500U ||
            options.loss_timeout_ms > 120000U ||
            options.restore_hysteresis_ms < 1000U ||
            options.restore_hysteresis_ms > 120000U ||
            (options.intent == PtpSourceIntent::external_follower &&
             !options.external_target_selected) ||
            (options.intent != PtpSourceIntent::auto_failover &&
             (options.fallback_authorized || options.auto_restore_external)) ||
            (options.auto_restore_external && !options.fallback_authorized)) {
            return false;
        }
        options_ = options;
        configured_ = true;
        return true;
    }

    [[nodiscard]] PtpSourceDecision tick(const std::uint64_t ms,
                                         const PtpSourceEvidence& e) noexcept {
        if (!configured_ || ms < last_time_ms_) {
            state_ = PtpSourceState::fault;
            return reply();
        }
        last_time_ms_ = ms;
        switch (state_) {
        case PtpSourceState::idle:
            if (!e.link_up) return reply();
            if (options_.intent == PtpSourceIntent::internal_gm ||
                (options_.intent == PtpSourceIntent::auto_failover &&
                 !options_.external_target_selected)) {
                if (!exclusive(e)) return reply();
                if (e.other_grandmaster_present)
                    return reply(PtpSourceAction::refuse_competing_gm);
                generation_++;
                state_ = PtpSourceState::starting_internal;
                since_ = ms;
                return reply(PtpSourceAction::start_internal);
            }
            if (!exclusive(e)) return reply();
            generation_++;
            state_ = PtpSourceState::acquiring_external;
            since_ = ms;
            return reply(PtpSourceAction::start_external);
        case PtpSourceState::starting_internal:
            if (e.source_worker_active && e.internal_gm_tx_ready) {
                state_ = rearm_required_ ? PtpSourceState::waiting_second_boundary
                                         : PtpSourceState::internal_ready;
            } else if (ms - since_ > options_.acquisition_timeout_ms) {
                state_ = PtpSourceState::fault;
                return reply(PtpSourceAction::revoke_sv_and_pause);
            }
            return reply();
        case PtpSourceState::internal_ready:
            if (!e.source_worker_active || !e.internal_gm_tx_ready) {
                switch_to_external_ = false;
                return begin_revoke(ms);
            }
            if (options_.intent == PtpSourceIntent::auto_failover &&
                options_.auto_restore_external && e.independent_external_candidate_valid) {
                if (!external_candidate_since_) external_candidate_since_ = ms + 1U;
                if (ms + 1U - external_candidate_since_ >=
                    options_.restore_hysteresis_ms) {
                    switch_to_external_ = true;
                    return begin_revoke(ms);
                }
            } else external_candidate_since_ = 0U;
            return reply();
        case PtpSourceState::acquiring_external:
            if (e.receiver_worker_active && e.selected_external_servo_locked) {
                state_ = rearm_required_ ? PtpSourceState::waiting_second_boundary
                                         : PtpSourceState::external_locked;
                return reply();
            }
            if (ms - since_ >= options_.acquisition_timeout_ms &&
                options_.intent == PtpSourceIntent::auto_failover &&
                options_.fallback_authorized) {
                switch_to_external_ = false;
                return begin_revoke(ms);
            }
            // EXTERNAL_FOLLOWER never secretly becomes an internal GM.
            return reply();
        case PtpSourceState::external_locked:
            if (!e.receiver_worker_active || !e.selected_external_servo_locked) {
                state_ = PtpSourceState::external_loss;
                since_ = ms;
                return reply(PtpSourceAction::revoke_sv_and_pause);
            }
            return reply();
        case PtpSourceState::external_loss:
            if (e.receiver_worker_active && e.selected_external_servo_locked) {
                rearm_required_ = true;
                state_ = PtpSourceState::waiting_second_boundary;
                return reply();
            }
            if (ms - since_ >= options_.loss_timeout_ms &&
                options_.intent == PtpSourceIntent::auto_failover &&
                options_.fallback_authorized) {
                switch_to_external_ = false;
                return begin_revoke(ms);
            }
            return reply();
        case PtpSourceState::revoking_sv:
            if (!e.sv_transmission_paused) return reply();
            state_ = PtpSourceState::draining_ptp;
            if (e.source_worker_active || e.receiver_worker_active)
                return reply(PtpSourceAction::stop_current_ptp);
            return reply();
        case PtpSourceState::draining_ptp:
            if (e.source_worker_active || e.receiver_worker_active ||
                e.receiver_cleanup_pending || !e.sv_transmission_paused) return reply();
            state_ = PtpSourceState::waiting_exclusive_emac;
            return reply();
        case PtpSourceState::waiting_exclusive_emac:
            if (!e.sv_transmission_paused || !e.link_up || !exclusive(e)) return reply();
            if (!switch_to_external_ && e.other_grandmaster_present)
                return reply(PtpSourceAction::refuse_competing_gm);
            generation_++;
            since_ = ms;
            if (switch_to_external_) {
                state_ = PtpSourceState::acquiring_external;
                return reply(PtpSourceAction::start_external);
            }
            state_ = PtpSourceState::starting_internal;
            return reply(PtpSourceAction::start_internal);
        case PtpSourceState::waiting_second_boundary: {
            const bool active = switch_to_external_
                ? e.receiver_worker_active && e.selected_external_servo_locked
                : e.source_worker_active && e.internal_gm_tx_ready;
            if (active && e.sv_transmission_paused &&
                e.at_ptp_second_boundary &&
                e.sv_gm_identity_matches_active_ptp) {
                state_ = switch_to_external_ ? PtpSourceState::external_locked :
                                                PtpSourceState::internal_ready;
                rearm_required_ = false;
                return reply(PtpSourceAction::rearm_sv_at_second);
            }
            return reply();
        }
        case PtpSourceState::fault: return reply();
        }
        state_ = PtpSourceState::fault;
        return reply();
    }

    [[nodiscard]] PtpSourceState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }

private:
    [[nodiscard]] static bool exclusive(const PtpSourceEvidence& e) noexcept {
        return !e.source_worker_active && !e.receiver_worker_active &&
               !e.receiver_cleanup_pending;
    }
    [[nodiscard]] PtpSourceDecision reply(
        const PtpSourceAction action = PtpSourceAction::none) const noexcept {
        return {state_, action, generation_};
    }
    [[nodiscard]] PtpSourceDecision begin_revoke(const std::uint64_t ms) noexcept {
        since_ = ms;
        rearm_required_ = true;
        external_candidate_since_ = 0U;
        state_ = PtpSourceState::revoking_sv;
        return reply(PtpSourceAction::revoke_sv_and_pause);
    }

    PtpSourcePolicyOptions options_{};
    PtpSourceState state_{PtpSourceState::idle};
    bool configured_{false};
    bool switch_to_external_{false};
    bool rearm_required_{false};
    std::uint64_t since_{0U};
    std::uint64_t last_time_ms_{0U};
    std::uint64_t external_candidate_since_{0U};
    std::uint32_t generation_{0U};
};

} // namespace ar::iec61850::time_sync
