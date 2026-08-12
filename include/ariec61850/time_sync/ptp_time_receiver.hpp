// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp_discipline_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

namespace ar::iec61850::time_sync {

namespace receiver_detail {

[[nodiscard]] inline bool parse_pdelay_body(
    const PtpFrame& frame,
    PtpTimestamp& timestamp,
    PtpPortIdentity& requesting_port_identity) noexcept {
    if (frame.body.size() < 20U ||
        !PtpTimestamp::try_read(
            std::span<const std::uint8_t>{frame.body}.first(10U),
            timestamp)) {
        return false;
    }
    std::copy_n(
        frame.body.begin() + 10,
        8,
        requesting_port_identity.clock_identity.begin());
    requesting_port_identity.port_number = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frame.body[18]) << 8U) |
        static_cast<std::uint16_t>(frame.body[19]));
    return true;
}

} // namespace receiver_detail

struct PtpTimeReceiverOptions final {
    std::uint8_t domain_number{};
    std::uint8_t transport_specific{};
    PtpPortIdentity local_port_identity{};
    std::chrono::milliseconds source_timeout{3000};
    std::chrono::milliseconds exchange_timeout{2000};
};

struct PtpTimeReceiverStatus final {
    std::optional<PtpPortIdentity> selected_source;
    std::optional<PtpClockIdentity> selected_grandmaster;
    std::optional<std::int64_t> mean_path_delay_ns;
    bool selected_source_globally_traceable{};
    std::uint64_t announce_frames{};
    std::uint64_t sync_frames{};
    std::uint64_t follow_up_frames{};
    std::uint64_t pdelay_responses{};
    std::uint64_t completed_pdelay_exchanges{};
    std::uint64_t completed_sync_exchanges{};
    std::uint64_t rejected_exchanges{};
};

/**
 * Bounded single-port time-receiver correlator.
 *
 * Source eligibility is refreshed by Announce only. Sync/Follow_Up/Pdelay
 * activity deliberately cannot keep stale Announce quality or traceability
 * evidence alive after source_timeout.
 */
class PtpTimeReceiver final {
public:
    explicit PtpTimeReceiver(PtpTimeReceiverOptions options = {}) noexcept
        : options_(std::move(options)) {}

    void reset() noexcept {
        status_ = {};
        selected_quality_.reset();
        selected_announce_last_seen_.reset();
        clear_exchanges();
    }

    void reconfigure(PtpTimeReceiverOptions options) noexcept {
        options_ = std::move(options);
        reset();
    }

    [[nodiscard]] bool observe_announce(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::announce ||
            !frame.announce.has_value()) {
            return false;
        }
        ++status_.announce_frames;
        const auto& announce = *frame.announce;
        const AnnounceQuality quality{
            announce.priority1,
            announce.clock_class,
            static_cast<std::uint8_t>(announce.clock_accuracy),
            announce.offset_scaled_log_variance,
            announce.priority2,
            announce.grandmaster_identity,
            announce.steps_removed,
        };

        const bool current_is_same =
            source_matches_selected(frame.header.source_port_identity);
        const bool selected_is_stale = selected_announce_stale(now);
        const bool should_select =
            !status_.selected_source.has_value() ||
            selected_is_stale ||
            current_is_same ||
            (selected_quality_.has_value() &&
             better_quality(quality, *selected_quality_));
        if (!should_select) return false;

        const bool changed = !current_is_same;
        status_.selected_source = frame.header.source_port_identity;
        status_.selected_grandmaster = announce.grandmaster_identity;
        status_.selected_source_globally_traceable =
            ptp_announce_is_globally_traceable(frame);
        selected_quality_ = quality;
        selected_announce_last_seen_ = now;
        if (changed) {
            status_.mean_path_delay_ns.reset();
            clear_exchanges();
        }
        return changed;
    }

    void note_pdelay_request(
        const std::uint16_t sequence_id,
        const PtpTimestamp& request_tx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        pending_pdelay_ = PendingPdelay{
            sequence_id,
            request_tx_timestamp,
            {},
            {},
            0LL,
            false,
            now,
        };
    }

    [[nodiscard]] bool observe_pdelay_response(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        PtpTimestamp peer_request_rx{};
        PtpPortIdentity requester{};
        const bool body_valid = receiver_detail::parse_pdelay_body(
            frame,
            peer_request_rx,
            requester);
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::pdelay_resp ||
            !pending_pdelay_.has_value() ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_->sequence_id ||
            !source_matches_selected(frame.header.source_port_identity) ||
            selected_announce_stale(now)) {
            ++status_.rejected_exchanges;
            return false;
        }
        ++status_.pdelay_responses;
        pending_pdelay_->t2 = peer_request_rx;
        pending_pdelay_->t4 = local_rx_timestamp;
        pending_pdelay_->response_correction_field = frame.header.correction_field;
        pending_pdelay_->response_received = true;
        return true;
    }

    [[nodiscard]] std::optional<PtpPeerDelayMeasurement>
    observe_pdelay_response_follow_up(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        PtpTimestamp peer_response_tx{};
        PtpPortIdentity requester{};
        const bool body_valid = receiver_detail::parse_pdelay_body(
            frame,
            peer_response_tx,
            requester);
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::pdelay_resp_follow_up ||
            !pending_pdelay_.has_value() ||
            !pending_pdelay_->response_received ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_->sequence_id ||
            !source_matches_selected(frame.header.source_port_identity) ||
            selected_announce_stale(now)) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        const PtpPeerDelayExchange exchange{
            pending_pdelay_->t1,
            pending_pdelay_->t2,
            peer_response_tx,
            pending_pdelay_->t4,
            pending_pdelay_->response_correction_field,
            frame.header.correction_field,
        };
        pending_pdelay_.reset();
        const auto measurement = calculate_peer_delay(exchange);
        if (!measurement.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        status_.mean_path_delay_ns = measurement->mean_path_delay_ns;
        ++status_.completed_pdelay_exchanges;
        return measurement;
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_sync(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::sync ||
            !source_matches_selected(frame.header.source_port_identity) ||
            selected_announce_stale(now)) {
            return std::nullopt;
        }
        ++status_.sync_frames;
        pending_sync_ = PendingSync{
            frame.header.source_port_identity,
            frame.header.sequence_id,
            local_rx_timestamp,
            frame.header.correction_field,
            now,
        };
        if (frame.header.is_two_step()) return std::nullopt;
        if (!frame.timestamp.has_value()) {
            ++status_.rejected_exchanges;
            pending_sync_.reset();
            return std::nullopt;
        }
        return complete_sync(*frame.timestamp, 0LL, frame.header.sequence_id);
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_follow_up(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::follow_up ||
            !source_matches_selected(frame.header.source_port_identity) ||
            !frame.timestamp.has_value() ||
            selected_announce_stale(now)) {
            return std::nullopt;
        }
        ++status_.follow_up_frames;
        if (!pending_sync_.has_value() ||
            pending_sync_->source != frame.header.source_port_identity ||
            pending_sync_->sequence_id != frame.header.sequence_id) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        return complete_sync(
            *frame.timestamp,
            frame.header.correction_field,
            frame.header.sequence_id);
    }

    [[nodiscard]] bool tick(
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (selected_announce_stale(now)) {
            status_.selected_source.reset();
            status_.selected_grandmaster.reset();
            status_.mean_path_delay_ns.reset();
            status_.selected_source_globally_traceable = false;
            selected_quality_.reset();
            selected_announce_last_seen_.reset();
            clear_exchanges();
            return true;
        }

        if (pending_sync_.has_value() &&
            now > pending_sync_->observed_at &&
            now - pending_sync_->observed_at > options_.exchange_timeout) {
            pending_sync_.reset();
            ++status_.rejected_exchanges;
        }
        if (pending_pdelay_.has_value() &&
            now > pending_pdelay_->started_at &&
            now - pending_pdelay_->started_at > options_.exchange_timeout) {
            pending_pdelay_.reset();
            ++status_.rejected_exchanges;
        }
        return false;
    }

    [[nodiscard]] const PtpTimeReceiverStatus& status() const noexcept {
        return status_;
    }

private:
    struct AnnounceQuality final {
        std::uint8_t priority1{255U};
        std::uint8_t clock_class{255U};
        std::uint8_t clock_accuracy{255U};
        std::uint16_t variance{0xFFFFU};
        std::uint8_t priority2{255U};
        PtpClockIdentity grandmaster_identity{};
        std::uint16_t steps_removed{0xFFFFU};
    };

    struct PendingSync final {
        PtpPortIdentity source{};
        std::uint16_t sequence_id{};
        PtpTimestamp local_rx_timestamp{};
        std::int64_t sync_correction_field{};
        std::chrono::steady_clock::time_point observed_at{};
    };

    struct PendingPdelay final {
        std::uint16_t sequence_id{};
        PtpTimestamp t1{};
        PtpTimestamp t2{};
        PtpTimestamp t4{};
        std::int64_t response_correction_field{};
        bool response_received{};
        std::chrono::steady_clock::time_point started_at{};
    };

    [[nodiscard]] bool profile_matches(const PtpFrame& frame) const noexcept {
        return frame.header.domain_number == options_.domain_number &&
               frame.header.transport_specific == options_.transport_specific;
    }

    [[nodiscard]] bool source_matches_selected(
        const PtpPortIdentity& source) const noexcept {
        return status_.selected_source.has_value() &&
               *status_.selected_source == source;
    }

    [[nodiscard]] bool selected_announce_stale(
        const std::chrono::steady_clock::time_point now) const noexcept {
        return selected_announce_last_seen_.has_value() &&
               now > *selected_announce_last_seen_ &&
               now - *selected_announce_last_seen_ > options_.source_timeout;
    }

    [[nodiscard]] static bool better_quality(
        const AnnounceQuality& left,
        const AnnounceQuality& right) noexcept {
        return std::tie(
                   left.priority1,
                   left.clock_class,
                   left.clock_accuracy,
                   left.variance,
                   left.priority2,
                   left.grandmaster_identity,
                   left.steps_removed) <
               std::tie(
                   right.priority1,
                   right.clock_class,
                   right.clock_accuracy,
                   right.variance,
                   right.priority2,
                   right.grandmaster_identity,
                   right.steps_removed);
    }

    void clear_exchanges() noexcept {
        pending_sync_.reset();
        pending_pdelay_.reset();
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> complete_sync(
        const PtpTimestamp& origin,
        const std::int64_t follow_up_correction_field,
        const std::uint16_t sequence_id) noexcept {
        if (!pending_sync_.has_value() ||
            pending_sync_->sequence_id != sequence_id ||
            !status_.mean_path_delay_ns.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        const PtpSyncExchange exchange{
            origin,
            pending_sync_->local_rx_timestamp,
            *status_.mean_path_delay_ns,
            pending_sync_->sync_correction_field,
            follow_up_correction_field,
        };
        const auto source = pending_sync_->source;
        pending_sync_.reset();
        const auto offset = calculate_offset_from_master(exchange);
        if (!offset.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        ++status_.completed_sync_exchanges;
        return PtpOffsetMeasurement{
            source,
            sequence_id,
            *offset,
            exchange.mean_path_delay_ns,
            status_.selected_source_globally_traceable,
        };
    }

    PtpTimeReceiverOptions options_{};
    PtpTimeReceiverStatus status_{};
    std::optional<AnnounceQuality> selected_quality_;
    std::optional<std::chrono::steady_clock::time_point> selected_announce_last_seen_;
    std::optional<PendingSync> pending_sync_;
    std::optional<PendingPdelay> pending_pdelay_;
};

} // namespace ar::iec61850::time_sync
