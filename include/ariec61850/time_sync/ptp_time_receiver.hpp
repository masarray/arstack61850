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

[[nodiscard]] inline std::chrono::milliseconds announce_source_timeout(
    const std::int8_t log_message_interval,
    const std::chrono::milliseconds configured_floor) noexcept {
    // The default 3 s floor already tolerates three 1 s Announce periods and
    // all faster cadences. Extend only for slower advertised power-of-two
    // cadences. Unknown/reserved values fall back to the configured floor.
    if (log_message_interval <= 0 || log_message_interval > 7) {
        return configured_floor;
    }
    const auto shift = static_cast<unsigned>(log_message_interval);
    const auto interval_ms = static_cast<std::int64_t>(1000LL << shift);
    const auto missed_message_allowance =
        std::chrono::milliseconds{interval_ms * 3LL};
    return std::max(configured_floor, missed_message_allowance);
}

} // namespace receiver_detail

struct PtpTimeReceiverOptions final {
    std::uint8_t domain_number{};
    std::uint8_t transport_specific{};
    PtpPortIdentity local_port_identity{};
    // Minimum Announce receipt timeout. Slower selected sources extend this
    // from their advertised logMessageInterval with a 3-message allowance.
    std::chrono::milliseconds source_timeout{3000};
    std::chrono::milliseconds exchange_timeout{2000};
    // The default is derived from the configured exchange cadence. The ESP
    // adapter sets exchange_timeout to 3/4 of the Pdelay request interval, so
    // 2x keeps valid path-delay evidence alive for 1.5 request intervals while
    // still expiring independently when peer-delay traffic stops.
    std::chrono::milliseconds path_delay_timeout{exchange_timeout + exchange_timeout};
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

class PtpTimeReceiver final {
public:
    explicit PtpTimeReceiver(PtpTimeReceiverOptions options = {}) noexcept
        : options_(std::move(options)),
          selected_source_timeout_(options_.source_timeout) {}

    void reset() noexcept {
        status_ = {};
        selected_quality_.reset();
        selected_announce_last_seen_valid_ = false;
        selected_source_timeout_ = options_.source_timeout;
        last_path_delay_at_valid_ = false;
        receiver_correlation_enabled_ = false;
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

        // A same-port Announce arriving after the source evidence timeout is a
        // fresh selection event, not a continuation. This forces dependent
        // path-delay/exchange state and the adapter discipline to reacquire.
        const bool changed = !current_is_same || selected_is_stale;
        status_.selected_source = frame.header.source_port_identity;
        status_.selected_grandmaster = announce.grandmaster_identity;
        status_.selected_source_globally_traceable =
            ptp_announce_is_globally_traceable(frame);
        selected_quality_ = quality;
        selected_announce_last_seen_ = now;
        selected_announce_last_seen_valid_ = true;
        selected_source_timeout_ = receiver_detail::announce_source_timeout(
            frame.header.log_message_interval,
            options_.source_timeout);
        if (changed) {
            status_.mean_path_delay_ns.reset();
            last_path_delay_at_valid_ = false;
            clear_exchanges();
        }
        return changed;
    }

    void note_pdelay_request(
        const std::uint16_t sequence_id,
        const PtpTimestamp& request_tx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        // A caller that owns a Pdelay request is an active time receiver. From
        // this point, missing/mismatched Sync correlation is actionable timing
        // evidence and is counted as rejected. Passive MONITOR never calls this
        // method, so observed Sync/FU traffic remains telemetry, not a failure.
        receiver_correlation_enabled_ = true;
        pending_pdelay_ = PendingPdelay{
            sequence_id,
            request_tx_timestamp,
            {},
            {},
            0LL,
            false,
            now,
        };
        pending_pdelay_valid_ = true;
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
            !pending_pdelay_valid_ ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_.sequence_id ||
            !source_matches_selected(frame.header.source_port_identity) ||
            selected_announce_stale(now)) {
            ++status_.rejected_exchanges;
            return false;
        }
        ++status_.pdelay_responses;
        pending_pdelay_.t2 = peer_request_rx;
        pending_pdelay_.t4 = local_rx_timestamp;
        pending_pdelay_.response_correction_field = frame.header.correction_field;
        pending_pdelay_.response_received = true;
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
            !pending_pdelay_valid_ ||
            !pending_pdelay_.response_received ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_.sequence_id ||
            !source_matches_selected(frame.header.source_port_identity) ||
            selected_announce_stale(now)) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        const PtpPeerDelayExchange exchange{
            pending_pdelay_.t1,
            pending_pdelay_.t2,
            peer_response_tx,
            pending_pdelay_.t4,
            pending_pdelay_.response_correction_field,
            frame.header.correction_field,
        };
        pending_pdelay_valid_ = false;
        const auto measurement = calculate_peer_delay(exchange);
        if (!measurement.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        status_.mean_path_delay_ns = measurement->mean_path_delay_ns;
        last_path_delay_at_ = now;
        last_path_delay_at_valid_ = true;
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
            selected_announce_stale(now) ||
            path_delay_stale(now)) {
            return std::nullopt;
        }
        ++status_.sync_frames;

        // A new Sync supersedes any still-pending two-step exchange. Active
        // receivers must count that abandoned correlation so a stream of Sync
        // frames without Follow_Up cannot keep rejection telemetry at zero.
        if (pending_sync_valid_) {
            record_sync_rejection();
            pending_sync_valid_ = false;
        }

        pending_sync_ = PendingSync{
            frame.header.source_port_identity,
            frame.header.sequence_id,
            local_rx_timestamp,
            frame.header.correction_field,
            now,
        };
        pending_sync_valid_ = true;
        if (frame.header.is_two_step()) return std::nullopt;
        if (!frame.timestamp.has_value()) {
            record_sync_rejection();
            pending_sync_valid_ = false;
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
            selected_announce_stale(now) ||
            path_delay_stale(now)) {
            return std::nullopt;
        }
        ++status_.follow_up_frames;
        if (!pending_sync_valid_ ||
            pending_sync_.source != frame.header.source_port_identity ||
            pending_sync_.sequence_id != frame.header.sequence_id) {
            record_sync_rejection();
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
            selected_announce_last_seen_valid_ = false;
            selected_source_timeout_ = options_.source_timeout;
            last_path_delay_at_valid_ = false;
            clear_exchanges();
            return true;
        }

        if (path_delay_stale(now)) {
            status_.mean_path_delay_ns.reset();
            last_path_delay_at_valid_ = false;
            pending_sync_valid_ = false;
        }
        if (pending_sync_valid_) {
            const auto observed_at = pending_sync_.observed_at;
            if (now > observed_at &&
                now - observed_at > options_.exchange_timeout) {
                pending_sync_valid_ = false;
                record_sync_rejection();
            }
        }
        if (pending_pdelay_valid_) {
            const auto started_at = pending_pdelay_.started_at;
            if (now > started_at &&
                now - started_at > options_.exchange_timeout) {
                pending_pdelay_valid_ = false;
                ++status_.rejected_exchanges;
            }
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
        if (!selected_announce_last_seen_valid_) return false;
        return now > selected_announce_last_seen_ &&
               now - selected_announce_last_seen_ > selected_source_timeout_;
    }

    [[nodiscard]] bool path_delay_stale(
        const std::chrono::steady_clock::time_point now) const noexcept {
        if (!status_.mean_path_delay_ns.has_value() ||
            !last_path_delay_at_valid_) {
            return false;
        }
        return now > last_path_delay_at_ &&
               now - last_path_delay_at_ > options_.path_delay_timeout;
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
        pending_sync_valid_ = false;
        pending_pdelay_valid_ = false;
    }

    void record_sync_rejection() noexcept {
        if (receiver_correlation_enabled_) {
            ++status_.rejected_exchanges;
        }
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> complete_sync(
        const PtpTimestamp& origin,
        const std::int64_t follow_up_correction_field,
        const std::uint16_t sequence_id) noexcept {
        if (!pending_sync_valid_ ||
            pending_sync_.sequence_id != sequence_id) {
            record_sync_rejection();
            return std::nullopt;
        }
        if (!status_.mean_path_delay_ns.has_value()) {
            // Passive observation intentionally has no peer-delay evidence.
            // Consume the correlated pair without treating that absence as a
            // receiver failure. Active TIME_RECEIVER enables rejection counting
            // when it owns Pdelay requests.
            pending_sync_valid_ = false;
            record_sync_rejection();
            return std::nullopt;
        }
        const PtpSyncExchange exchange{
            origin,
            pending_sync_.local_rx_timestamp,
            *status_.mean_path_delay_ns,
            pending_sync_.sync_correction_field,
            follow_up_correction_field,
        };
        const auto source = pending_sync_.source;
        pending_sync_valid_ = false;
        const auto offset = calculate_offset_from_master(exchange);
        if (!offset.has_value()) {
            record_sync_rejection();
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
    std::chrono::steady_clock::time_point selected_announce_last_seen_{};
    bool selected_announce_last_seen_valid_{false};
    std::chrono::milliseconds selected_source_timeout_{};
    std::chrono::steady_clock::time_point last_path_delay_at_{};
    bool last_path_delay_at_valid_{false};
    PendingSync pending_sync_{};
    bool pending_sync_valid_{false};
    PendingPdelay pending_pdelay_{};
    bool pending_pdelay_valid_{false};
    bool receiver_correlation_enabled_{false};
};

} // namespace ar::iec61850::time_sync