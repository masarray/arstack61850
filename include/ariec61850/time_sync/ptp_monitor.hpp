// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::time_sync {

struct PtpObservedMessage final {
    std::chrono::system_clock::time_point observed_at{};
    PtpMessageType message_type{PtpMessageType::sync};
    std::uint8_t transport_specific{};
    std::uint8_t domain_number{};
    PtpPortIdentity source_port_identity{};
    std::uint16_t sequence_id{};
    bool two_step{};
    std::optional<std::uint16_t> vlan_id;
    std::optional<std::uint16_t> outer_vlan_id;
    bool peer_delay_multicast{};
};

struct PtpSourceClockSnapshot final {
    std::uint8_t domain_number{};
    PtpPortIdentity source_port_identity{};
    std::chrono::system_clock::time_point first_seen_at{};
    std::chrono::system_clock::time_point last_seen_at{};
    std::array<std::uint32_t, 16> message_counts{};
    std::array<std::optional<std::uint16_t>, 16> last_sequence_ids{};
    std::uint32_t sequence_anomaly_count{};
    std::optional<std::uint8_t> transport_specific;
    std::uint32_t transport_specific_change_count{};
    std::optional<std::uint16_t> vlan_id;
    std::optional<std::uint16_t> outer_vlan_id;

    [[nodiscard]] std::uint32_t count(PtpMessageType type) const noexcept;
};

struct PtpMonitorSnapshot final {
    std::chrono::system_clock::time_point captured_at{};
    std::uint64_t total_frames{};
    std::uint64_t valid_ptp_frames{};
    std::uint64_t invalid_frames{};
    std::vector<PtpObservedMessage> recent_messages;
    std::vector<PtpSourceClockSnapshot> sources;

    [[nodiscard]] bool has_ptp() const noexcept { return valid_ptp_frames != 0U; }
};

class PtpPassiveMonitor final {
public:
    explicit PtpPassiveMonitor(std::size_t recent_capacity = 256U);

    [[nodiscard]] bool observe_ethernet_frame(
        std::span<const std::uint8_t> ethernet_frame,
        std::chrono::system_clock::time_point observed_at = std::chrono::system_clock::now());
    void observe(
        const PtpFrame& frame,
        std::chrono::system_clock::time_point observed_at = std::chrono::system_clock::now());
    [[nodiscard]] PtpMonitorSnapshot snapshot(
        std::chrono::system_clock::time_point captured_at = std::chrono::system_clock::now()) const;
    void reset();

private:
    struct SourceState final {
        PtpSourceClockSnapshot snapshot;
    };

    [[nodiscard]] static std::size_t message_index(PtpMessageType type) noexcept;
    [[nodiscard]] SourceState* find_source(
        std::uint8_t domain_number,
        const PtpPortIdentity& source_port_identity) noexcept;

    std::size_t recent_capacity_;
    mutable std::mutex mutex_;
    std::uint64_t total_frames_{};
    std::uint64_t valid_frames_{};
    std::uint64_t invalid_frames_{};
    std::deque<PtpObservedMessage> recent_messages_;
    std::vector<SourceState> sources_;
};

enum class PtpHealthSeverity : std::uint8_t {
    ok,
    warning,
    fail,
};

struct PtpHealthCheck final {
    std::string id;
    PtpHealthSeverity severity{PtpHealthSeverity::ok};
    std::string message;
};

struct PtpTimingHealthOptions final {
    std::optional<std::uint8_t> expected_domain_number;
    std::optional<std::uint8_t> expected_transport_specific;
    std::chrono::milliseconds source_timeout{3000};
    bool require_announce{true};
    bool require_sync{true};
    bool require_follow_up_for_two_step{true};
    bool require_peer_delay_activity{false};
    std::uint32_t maximum_sequence_anomalies{};
};

struct PtpTimingHealthReport final {
    std::chrono::system_clock::time_point evaluated_at{};
    PtpHealthSeverity severity{PtpHealthSeverity::fail};
    PtpMonitorSnapshot snapshot;
    std::vector<PtpHealthCheck> checks;
    std::optional<PtpPortIdentity> selected_source;

    [[nodiscard]] bool is_healthy() const noexcept { return severity == PtpHealthSeverity::ok; }
};

class PtpTimingHealthValidator final {
public:
    [[nodiscard]] static PtpTimingHealthReport evaluate(
        const PtpMonitorSnapshot& snapshot,
        const PtpTimingHealthOptions& options = {});
};

enum class SmpSynchValue : std::uint8_t {
    not_synchronized = 0U,
    local_synchronized = 1U,
    global_synchronized = 2U,
};

[[nodiscard]] SmpSynchValue resolve_smp_synch(
    const PtpTimingHealthReport& report,
    bool allow_local_fallback = true) noexcept;

} // namespace ar::iec61850::time_sync
