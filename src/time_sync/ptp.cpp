// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace ar::iec61850::time_sync {
namespace {

[[nodiscard]] std::uint16_t read_u16_be(const std::span<const std::uint8_t> source) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(source[0]) << 8U) |
        static_cast<std::uint16_t>(source[1]));
}

[[nodiscard]] std::uint32_t read_u32_be(const std::span<const std::uint8_t> source) noexcept {
    return (static_cast<std::uint32_t>(source[0]) << 24U) |
           (static_cast<std::uint32_t>(source[1]) << 16U) |
           (static_cast<std::uint32_t>(source[2]) << 8U) |
           static_cast<std::uint32_t>(source[3]);
}

[[nodiscard]] std::uint64_t read_u64_be(const std::span<const std::uint8_t> source) noexcept {
    std::uint64_t value{};
    for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(source[index]);
    }
    return value;
}

void write_u16_be(const std::span<std::uint8_t> destination, const std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32_be(const std::span<std::uint8_t> destination, const std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u64_be(const std::span<std::uint8_t> destination, const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        destination[index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

[[nodiscard]] bool is_zero_identity(const PtpClockIdentity& identity) noexcept {
    return std::all_of(identity.begin(), identity.end(), [](const std::uint8_t byte) {
        return byte == 0U;
    });
}

[[nodiscard]] std::vector<std::uint8_t> build_message(
    const PtpMessageType message_type,
    const PtpBuildOptions& options,
    const std::uint8_t control_field,
    const std::span<const std::uint8_t> body) {
    if (body.size() > std::numeric_limits<std::uint16_t>::max() - ptp_header_length) {
        return {};
    }

    const auto message_length = ptp_header_length + body.size();
    std::vector<std::uint8_t> bytes(message_length, 0U);
    auto span = std::span<std::uint8_t>{bytes};
    span[0] = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(options.transport_specific << 4U) |
        (static_cast<std::uint8_t>(message_type) & 0x0FU));
    span[1] = ptp_version;
    write_u16_be(span.subspan(2U, 2U), static_cast<std::uint16_t>(message_length));
    span[4] = options.domain_number;
    write_u16_be(span.subspan(6U, 2U), options.two_step ? 0x0200U : 0U);
    write_u64_be(span.subspan(8U, 8U), static_cast<std::uint64_t>(options.correction_field));
    std::copy(
        options.source_port_identity.clock_identity.begin(),
        options.source_port_identity.clock_identity.end(),
        span.begin() + 20);
    write_u16_be(span.subspan(28U, 2U), options.source_port_identity.port_number);
    write_u16_be(span.subspan(30U, 2U), options.sequence_id);
    span[32] = control_field;
    span[33] = static_cast<std::uint8_t>(options.log_message_interval);
    std::copy(body.begin(), body.end(), span.begin() + static_cast<std::ptrdiff_t>(ptp_header_length));
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> build_timestamp_message(
    const PtpMessageType message_type,
    const PtpBuildOptions& options,
    const std::uint8_t control_field) {
    std::array<std::uint8_t, 10> body{};
    if (!options.timestamp.write(body)) {
        return {};
    }
    return build_message(message_type, options, control_field, body);
}

[[nodiscard]] std::uint16_t vlan_id_from_tci(const std::uint16_t tci) noexcept {
    return static_cast<std::uint16_t>(tci & 0x0FFFU);
}

} // namespace

PtpTimestamp PtpTimestamp::from_system_time(const std::chrono::system_clock::time_point time) noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        time.time_since_epoch()).count();
    if (elapsed <= 0) {
        return {};
    }
    constexpr std::int64_t nanos_per_second = 1'000'000'000LL;
    const auto seconds_value = elapsed / nanos_per_second;
    const auto nanos_value = elapsed % nanos_per_second;
    return {
        static_cast<std::uint64_t>(seconds_value),
        static_cast<std::uint32_t>(nanos_value),
    };
}

PtpTimestamp PtpTimestamp::now_utc() noexcept {
    return from_system_time(std::chrono::system_clock::now());
}

bool PtpTimestamp::try_read(
    const std::span<const std::uint8_t> source,
    PtpTimestamp& timestamp) noexcept {
    if (source.size() < 10U) {
        return false;
    }
    const auto seconds_high = static_cast<std::uint64_t>(read_u16_be(source.first(2U)));
    const auto seconds_low = static_cast<std::uint64_t>(read_u32_be(source.subspan(2U, 4U)));
    const auto nanoseconds_value = read_u32_be(source.subspan(6U, 4U));
    if (nanoseconds_value >= 1'000'000'000U) {
        return false;
    }
    timestamp.seconds = (seconds_high << 32U) | seconds_low;
    timestamp.nanoseconds = nanoseconds_value;
    return true;
}

bool PtpTimestamp::write(const std::span<std::uint8_t> destination) const noexcept {
    constexpr std::uint64_t max_seconds = 0x0000FFFFFFFFFFFFULL;
    if (destination.size() < 10U || seconds > max_seconds || nanoseconds >= 1'000'000'000U) {
        return false;
    }
    write_u16_be(destination.first(2U), static_cast<std::uint16_t>(seconds >> 32U));
    write_u32_be(destination.subspan(2U, 4U), static_cast<std::uint32_t>(seconds & 0xFFFFFFFFULL));
    write_u32_be(destination.subspan(6U, 4U), nanoseconds);
    return true;
}

std::vector<std::uint8_t> PtpCodec::build_sync(const PtpBuildOptions& options) {
    return build_timestamp_message(PtpMessageType::sync, options, 0x00U);
}

std::vector<std::uint8_t> PtpCodec::build_follow_up(const PtpBuildOptions& options) {
    return build_timestamp_message(PtpMessageType::follow_up, options, 0x02U);
}

std::vector<std::uint8_t> PtpCodec::build_pdelay_req(const PtpBuildOptions& options) {
    return build_timestamp_message(PtpMessageType::pdelay_req, options, 0x05U);
}

std::vector<std::uint8_t> PtpCodec::build_pdelay_resp(
    const PtpBuildOptions& options,
    const PtpPortIdentity& requesting_port_identity) {
    std::array<std::uint8_t, 20> body{};
    if (!options.timestamp.write(std::span<std::uint8_t>{body}.first(10U))) {
        return {};
    }
    std::copy(
        requesting_port_identity.clock_identity.begin(),
        requesting_port_identity.clock_identity.end(),
        body.begin() + 10);
    write_u16_be(std::span<std::uint8_t>{body}.subspan(18U, 2U), requesting_port_identity.port_number);
    return build_message(PtpMessageType::pdelay_resp, options, 0x05U, body);
}

std::vector<std::uint8_t> PtpCodec::build_pdelay_resp_follow_up(
    const PtpBuildOptions& options,
    const PtpPortIdentity& requesting_port_identity) {
    std::array<std::uint8_t, 20> body{};
    if (!options.timestamp.write(std::span<std::uint8_t>{body}.first(10U))) {
        return {};
    }
    std::copy(
        requesting_port_identity.clock_identity.begin(),
        requesting_port_identity.clock_identity.end(),
        body.begin() + 10);
    write_u16_be(std::span<std::uint8_t>{body}.subspan(18U, 2U), requesting_port_identity.port_number);
    return build_message(PtpMessageType::pdelay_resp_follow_up, options, 0x05U, body);
}

std::vector<std::uint8_t> PtpCodec::build_announce(
    const PtpBuildOptions& options,
    const std::int16_t current_utc_offset) {
    std::array<std::uint8_t, 30> body{};
    if (!options.timestamp.write(std::span<std::uint8_t>{body}.first(10U))) {
        return {};
    }
    write_u16_be(
        std::span<std::uint8_t>{body}.subspan(10U, 2U),
        static_cast<std::uint16_t>(current_utc_offset));
    body[13] = options.priority1;
    body[14] = options.clock_class;
    body[15] = static_cast<std::uint8_t>(options.clock_accuracy);
    write_u16_be(
        std::span<std::uint8_t>{body}.subspan(16U, 2U),
        options.offset_scaled_log_variance);
    body[18] = options.priority2;
    const auto& grandmaster = is_zero_identity(options.grandmaster_identity)
        ? options.source_port_identity.clock_identity
        : options.grandmaster_identity;
    std::copy(grandmaster.begin(), grandmaster.end(), body.begin() + 19);
    write_u16_be(std::span<std::uint8_t>{body}.subspan(27U, 2U), options.steps_removed);
    body[29] = static_cast<std::uint8_t>(options.time_source);
    return build_message(PtpMessageType::announce, options, 0x05U, body);
}

std::vector<std::uint8_t> PtpCodec::build_ethernet_frame(
    const std::array<std::uint8_t, 6>& destination_mac,
    const std::array<std::uint8_t, 6>& source_mac,
    const std::span<const std::uint8_t> ptp_message,
    const std::optional<std::uint16_t> vlan_id,
    const std::uint8_t vlan_priority) {
    if (vlan_priority > 7U || (vlan_id.has_value() && *vlan_id > 4095U)) {
        return {};
    }

    ethernet::EthernetFrame frame;
    frame.destination = ethernet::MacAddress{std::span<const std::uint8_t>{destination_mac}};
    frame.source = ethernet::MacAddress{std::span<const std::uint8_t>{source_mac}};
    frame.ether_type = ptp_ethertype;
    if (vlan_id.has_value()) {
        frame.vlan = ethernet::VlanTag{vlan_priority, *vlan_id};
    }
    frame.payload.assign(ptp_message.begin(), ptp_message.end());
    return ethernet::EthernetFrameCodec::encode(frame);
}

bool PtpCodec::try_parse_message(
    const std::span<const std::uint8_t> message,
    PtpFrame& frame) noexcept {
    if (message.size() < ptp_header_length) {
        return false;
    }

    const auto version = static_cast<std::uint8_t>(message[1] & 0x0FU);
    if (version != ptp_version) {
        return false;
    }
    const auto message_length = read_u16_be(message.subspan(2U, 2U));
    if (message_length < ptp_header_length || message.size() < message_length) {
        return false;
    }

    PtpFrame parsed;
    parsed.header.transport_specific = static_cast<std::uint8_t>(message[0] >> 4U);
    parsed.header.message_type = static_cast<PtpMessageType>(message[0] & 0x0FU);
    parsed.header.version = version;
    parsed.header.message_length = message_length;
    parsed.header.domain_number = message[4];
    parsed.header.flags = read_u16_be(message.subspan(6U, 2U));
    parsed.header.correction_field = static_cast<std::int64_t>(read_u64_be(message.subspan(8U, 8U)));
    std::copy_n(message.begin() + 20, 8, parsed.header.source_port_identity.clock_identity.begin());
    parsed.header.source_port_identity.port_number = read_u16_be(message.subspan(28U, 2U));
    parsed.header.sequence_id = read_u16_be(message.subspan(30U, 2U));
    parsed.header.control_field = message[32];
    parsed.header.log_message_interval = static_cast<std::int8_t>(message[33]);

    const auto body = message.subspan(ptp_header_length, message_length - ptp_header_length);
    PtpTimestamp timestamp;
    if ((parsed.header.message_type == PtpMessageType::sync ||
         parsed.header.message_type == PtpMessageType::follow_up) &&
        body.size() >= 10U && PtpTimestamp::try_read(body.first(10U), timestamp)) {
        parsed.timestamp = timestamp;
    }

    if (parsed.header.message_type == PtpMessageType::announce && body.size() >= 30U) {
        if (!PtpTimestamp::try_read(body.first(10U), timestamp)) {
            return false;
        }
        PtpAnnounceMessage announce;
        announce.origin_timestamp = timestamp;
        announce.current_utc_offset = static_cast<std::int16_t>(read_u16_be(body.subspan(10U, 2U)));
        announce.priority1 = body[13];
        announce.clock_class = body[14];
        announce.clock_accuracy = static_cast<PtpClockAccuracy>(body[15]);
        announce.offset_scaled_log_variance = read_u16_be(body.subspan(16U, 2U));
        announce.priority2 = body[18];
        std::copy_n(body.begin() + 19, 8, announce.grandmaster_identity.begin());
        announce.steps_removed = read_u16_be(body.subspan(27U, 2U));
        announce.time_source = static_cast<PtpTimeSource>(body[29]);
        parsed.timestamp = timestamp;
        parsed.announce = announce;
    }

    parsed.message.assign(message.begin(), message.begin() + static_cast<std::ptrdiff_t>(message_length));
    parsed.body.assign(body.begin(), body.end());
    frame = std::move(parsed);
    return true;
}

bool PtpCodec::try_parse_ethernet_frame(
    const std::span<const std::uint8_t> ethernet_frame,
    PtpFrame& frame) noexcept {
    if (ethernet_frame.size() < 14U) {
        return false;
    }

    std::size_t offset = 12U;
    std::optional<std::uint16_t> outer_vlan;
    std::optional<std::uint16_t> vlan;
    auto ether_type = read_u16_be(ethernet_frame.subspan(offset, 2U));
    offset += 2U;

    if (ether_type == ptp_qinq_ethertype) {
        if (ethernet_frame.size() < offset + 4U) {
            return false;
        }
        outer_vlan = vlan_id_from_tci(read_u16_be(ethernet_frame.subspan(offset, 2U)));
        ether_type = read_u16_be(ethernet_frame.subspan(offset + 2U, 2U));
        offset += 4U;
    }

    if (ether_type == ptp_vlan_ethertype) {
        if (ethernet_frame.size() < offset + 4U) {
            return false;
        }
        vlan = vlan_id_from_tci(read_u16_be(ethernet_frame.subspan(offset, 2U)));
        ether_type = read_u16_be(ethernet_frame.subspan(offset + 2U, 2U));
        offset += 4U;
    }

    if (ether_type != ptp_ethertype || ethernet_frame.size() < offset + ptp_header_length) {
        return false;
    }

    PtpFrame parsed;
    if (!try_parse_message(ethernet_frame.subspan(offset), parsed)) {
        return false;
    }
    parsed.outer_vlan_id = outer_vlan;
    parsed.vlan_id = vlan;
    parsed.peer_delay_multicast = std::equal(
        ptp_peer_delay_multicast_mac.begin(),
        ptp_peer_delay_multicast_mac.end(),
        ethernet_frame.begin());
    frame = std::move(parsed);
    return true;
}

} // namespace ar::iec61850::time_sync
