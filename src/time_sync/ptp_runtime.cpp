// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <utility>

namespace ar::iec61850::time_sync {
namespace {

[[nodiscard]] constexpr int hex_value(const char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool identity_is_zero(const PtpClockIdentity& identity) noexcept {
    return std::all_of(identity.begin(), identity.end(), [](const std::uint8_t byte) {
        return byte == 0U;
    });
}

} // namespace

bool try_parse_ptp_clock_identity(
    std::string_view text,
    PtpClockIdentity& identity) noexcept {
    text = trim_ascii(text);
    if (text.empty()) return false;

    PtpClockIdentity parsed{};
    std::size_t byte_index = 0U;
    std::size_t cursor = 0U;
    while (cursor < text.size() && byte_index < parsed.size()) {
        if (cursor + 2U > text.size()) return false;
        const int high = hex_value(text[cursor]);
        const int low = hex_value(text[cursor + 1U]);
        if (high < 0 || low < 0) return false;
        parsed[byte_index] = static_cast<std::uint8_t>((high << 4) | low);
        ++byte_index;
        cursor += 2U;
        if (byte_index == parsed.size()) break;
        if (cursor >= text.size() || (text[cursor] != ':' && text[cursor] != '-')) return false;
        ++cursor;
    }

    if (byte_index != parsed.size() || cursor != text.size()) return false;
    identity = parsed;
    return true;
}

std::string format_ptp_clock_identity(const PtpClockIdentity& identity) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(23U);
    for (std::size_t index = 0U; index < identity.size(); ++index) {
        if (index != 0U) result.push_back(':');
        const auto byte = identity[index];
        result.push_back(digits[(byte >> 4U) & 0x0FU]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

PtpClockIdentity ptp_clock_identity_from_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return {mac[0], mac[1], mac[2], 0xFFU, 0xFEU, mac[3], mac[4], mac[5]};
}

std::uint16_t PtpSequenceCounters::next(const PtpMessageType message_type) noexcept {
    const auto index = static_cast<std::size_t>(static_cast<std::uint8_t>(message_type) & 0x0FU);
    auto& counter = counters_[index];
    counter = static_cast<std::uint16_t>(counter + 1U);
    return counter;
}

void PtpSequenceCounters::reset() noexcept {
    counters_.fill(0U);
}

PtpPublisherRuntime::PtpPublisherRuntime(PtpPublisherOptions options)
    : options_(std::move(options)) {}

bool PtpPublisherRuntime::validate_options(
    const PtpPublisherOptions& options,
    std::string& error) noexcept {
    if (options.transport_specific > 0x0FU) {
        error = "transportSpecific must be 0..15";
        return false;
    }
    if (options.vlan_priority > 7U) {
        error = "VLAN priority must be 0..7";
        return false;
    }
    if (options.vlan_id.has_value() && (*options.vlan_id == 0U || *options.vlan_id > 4094U)) {
        error = "VLAN ID must be 1..4094";
        return false;
    }
    if (options.port_number == 0U) {
        error = "PTP port number must be non-zero";
        return false;
    }
    if (options.announce_interval <= std::chrono::milliseconds::zero()) {
        error = "Announce interval must be greater than zero";
        return false;
    }
    if (options.sync_interval <= std::chrono::milliseconds::zero()) {
        error = "Sync interval must be greater than zero";
        return false;
    }
    if (options.follow_up_delay < std::chrono::milliseconds::zero()) {
        error = "Follow-up delay cannot be negative";
        return false;
    }
    if (identity_is_zero(options.clock_identity)) {
        error = "PTP clock identity must be non-zero";
        return false;
    }
    error.clear();
    return true;
}

bool PtpPublisherRuntime::reconfigure(PtpPublisherOptions options, std::string& error) noexcept {
    if (status_.is_running) {
        error = "PTP runtime must be stopped before reconfiguration";
        return false;
    }
    if (!validate_options(options, error)) return false;
    options_ = std::move(options);
    status_.last_error.clear();
    return true;
}

bool PtpPublisherRuntime::start(
    const std::chrono::system_clock::time_point wall_time,
    const std::chrono::steady_clock::time_point monotonic_time) noexcept {
    std::string error;
    if (!validate_options(options_, error)) {
        status_.last_error = std::move(error);
        status_.is_running = false;
        return false;
    }
    status_.is_running = true;
    status_.started_at = wall_time;
    status_.last_error.clear();
    next_announce_ = monotonic_time;
    next_sync_ = monotonic_time;
    return true;
}

void PtpPublisherRuntime::stop() noexcept {
    status_.is_running = false;
}

PtpDueMessages PtpPublisherRuntime::poll_due(
    const std::chrono::steady_clock::time_point monotonic_time) noexcept {
    PtpDueMessages due;
    if (!status_.is_running) return due;

    if (monotonic_time >= next_announce_) {
        due.announce = true;
        next_announce_ = monotonic_time + options_.announce_interval;
    }
    if (monotonic_time >= next_sync_) {
        due.sync = true;
        next_sync_ = monotonic_time + options_.sync_interval;
    }
    return due;
}

std::int8_t PtpPublisherRuntime::interval_to_log2(
    const std::chrono::milliseconds interval) noexcept {
    auto milliseconds = interval.count();
    if (milliseconds <= 0) return 0;

    std::int8_t exponent = 0;
    while (milliseconds < 1000 && exponent > -7) {
        milliseconds *= 2;
        --exponent;
    }
    while (milliseconds >= 2000 && exponent < 7) {
        milliseconds = (milliseconds + 1) / 2;
        ++exponent;
    }
    return exponent;
}

PtpBuildOptions PtpPublisherRuntime::make_build_options(
    const std::uint16_t sequence_id,
    const std::int8_t log_message_interval,
    const bool two_step,
    const PtpTimestamp timestamp) const noexcept {
    PtpBuildOptions result;
    result.transport_specific = options_.transport_specific;
    result.domain_number = options_.domain_number;
    result.source_port_identity = options_.source_port_identity();
    result.sequence_id = sequence_id;
    result.log_message_interval = log_message_interval;
    result.two_step = two_step;
    result.priority1 = options_.priority1;
    result.priority2 = options_.priority2;
    result.clock_class = options_.clock_class;
    result.clock_accuracy = options_.clock_accuracy;
    result.offset_scaled_log_variance = options_.offset_scaled_log_variance;
    result.grandmaster_identity = options_.clock_identity;
    result.steps_removed = options_.steps_removed;
    result.time_source = options_.time_source;
    result.timestamp = timestamp;
    return result;
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::wrap(
    const PtpMessageType message_type,
    const std::uint16_t sequence_id,
    const std::array<std::uint8_t, 6>& destination_mac,
    std::vector<std::uint8_t> ptp_message) const {
    if (!status_.is_running || ptp_message.empty()) return std::nullopt;
    auto ethernet_frame = PtpCodec::build_ethernet_frame(
        destination_mac,
        options_.source_mac,
        ptp_message,
        options_.vlan_id,
        options_.vlan_priority);
    if (ethernet_frame.empty()) return std::nullopt;
    return PtpPreparedFrame{
        message_type,
        sequence_id,
        destination_mac,
        std::move(ethernet_frame),
    };
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::prepare_announce(
    const PtpTimestamp origin_timestamp) {
    if (!status_.is_running) return std::nullopt;
    const auto sequence = sequences_.next(PtpMessageType::announce);
    const auto options = make_build_options(
        sequence,
        interval_to_log2(options_.announce_interval),
        false,
        origin_timestamp);
    return wrap(
        PtpMessageType::announce,
        sequence,
        ptp_general_multicast_mac,
        PtpCodec::build_announce(options, options_.current_utc_offset));
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::prepare_sync(
    const PtpTimestamp origin_timestamp) {
    if (!status_.is_running) return std::nullopt;
    const auto sequence = sequences_.next(PtpMessageType::sync);
    const auto options = make_build_options(
        sequence,
        interval_to_log2(options_.sync_interval),
        options_.two_step_clock,
        origin_timestamp);
    return wrap(
        PtpMessageType::sync,
        sequence,
        ptp_general_multicast_mac,
        PtpCodec::build_sync(options));
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::prepare_follow_up(
    const std::uint16_t sync_sequence_id,
    const PtpTimestamp precise_origin_timestamp) {
    if (!status_.is_running || !options_.two_step_clock) return std::nullopt;
    const auto options = make_build_options(
        sync_sequence_id,
        interval_to_log2(options_.sync_interval),
        false,
        precise_origin_timestamp);
    return wrap(
        PtpMessageType::follow_up,
        sync_sequence_id,
        ptp_general_multicast_mac,
        PtpCodec::build_follow_up(options));
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::prepare_pdelay_response(
    const PtpPortIdentity& requesting_port_identity,
    const std::uint16_t request_sequence_id,
    const std::int8_t request_log_message_interval,
    const PtpTimestamp request_receipt_timestamp) {
    if (!status_.is_running || !options_.respond_to_peer_delay) return std::nullopt;
    const auto options = make_build_options(
        request_sequence_id,
        request_log_message_interval,
        options_.two_step_clock,
        request_receipt_timestamp);
    return wrap(
        PtpMessageType::pdelay_resp,
        request_sequence_id,
        ptp_peer_delay_multicast_mac,
        PtpCodec::build_pdelay_resp(options, requesting_port_identity));
}

std::optional<PtpPreparedFrame> PtpPublisherRuntime::prepare_pdelay_response_follow_up(
    const PtpPortIdentity& requesting_port_identity,
    const std::uint16_t request_sequence_id,
    const std::int8_t request_log_message_interval,
    const PtpTimestamp response_origin_timestamp) {
    if (!status_.is_running || !options_.respond_to_peer_delay || !options_.two_step_clock) {
        return std::nullopt;
    }
    const auto options = make_build_options(
        request_sequence_id,
        request_log_message_interval,
        false,
        response_origin_timestamp);
    return wrap(
        PtpMessageType::pdelay_resp_follow_up,
        request_sequence_id,
        ptp_peer_delay_multicast_mac,
        PtpCodec::build_pdelay_resp_follow_up(options, requesting_port_identity));
}

void PtpPublisherRuntime::record_sent(
    const PtpMessageType message_type,
    const std::chrono::system_clock::time_point wall_time) noexcept {
    if (!status_.is_running) return;
    status_.last_sent_at = wall_time;
    switch (message_type) {
    case PtpMessageType::announce:
        ++status_.announce_sent;
        break;
    case PtpMessageType::sync:
        ++status_.sync_sent;
        break;
    case PtpMessageType::follow_up:
        ++status_.follow_up_sent;
        break;
    case PtpMessageType::pdelay_resp:
    case PtpMessageType::pdelay_resp_follow_up:
        ++status_.peer_delay_responses_sent;
        break;
    default:
        break;
    }
}

void PtpPublisherRuntime::record_error(std::string error) {
    status_.last_error = std::move(error);
}

void PtpPublisherRuntime::clear_error() {
    status_.last_error.clear();
}

} // namespace ar::iec61850::time_sync
