// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <algorithm>
#include <sstream>

namespace ar::iec61850::time_sync {
namespace {

[[nodiscard]] std::string port_identity_text(const PtpPortIdentity& identity) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(8U * 3U + 5U);
    for (std::size_t index = 0U; index < identity.clock_identity.size(); ++index) {
        if (index != 0U) text.push_back(':');
        const auto byte = identity.clock_identity[index];
        text.push_back(digits[(byte >> 4U) & 0x0FU]);
        text.push_back(digits[byte & 0x0FU]);
    }
    text.push_back('/');
    text += std::to_string(identity.port_number);
    return text;
}

void add_presence_check(
    std::vector<PtpHealthCheck>& checks,
    const PtpSourceClockSnapshot& source,
    const PtpMessageType type,
    std::string id,
    std::string label) {
    const auto message_count = source.count(type);
    if (message_count != 0U) {
        checks.push_back({
            std::move(id),
            PtpHealthSeverity::ok,
            std::move(label) + " messages are visible from " + port_identity_text(source.source_port_identity) + ".",
        });
    } else {
        checks.push_back({
            std::move(id),
            PtpHealthSeverity::fail,
            "No " + std::move(label) + " message is visible from selected PTP source " +
                port_identity_text(source.source_port_identity) + ".",
        });
    }
}

[[nodiscard]] PtpHealthSeverity report_severity(const std::vector<PtpHealthCheck>& checks) noexcept {
    if (std::any_of(checks.begin(), checks.end(), [](const PtpHealthCheck& check) {
            return check.severity == PtpHealthSeverity::fail;
        })) {
        return PtpHealthSeverity::fail;
    }
    if (std::any_of(checks.begin(), checks.end(), [](const PtpHealthCheck& check) {
            return check.severity == PtpHealthSeverity::warning;
        })) {
        return PtpHealthSeverity::warning;
    }
    return PtpHealthSeverity::ok;
}

} // namespace

std::uint32_t PtpSourceClockSnapshot::count(const PtpMessageType type) const noexcept {
    const auto index = static_cast<std::size_t>(static_cast<std::uint8_t>(type) & 0x0FU);
    return message_counts[index];
}

PtpPassiveMonitor::PtpPassiveMonitor(const std::size_t recent_capacity)
    : recent_capacity_(std::max<std::size_t>(recent_capacity, 8U)) {}

std::size_t PtpPassiveMonitor::message_index(const PtpMessageType type) noexcept {
    return static_cast<std::size_t>(static_cast<std::uint8_t>(type) & 0x0FU);
}

PtpPassiveMonitor::SourceState* PtpPassiveMonitor::find_source(
    const std::uint8_t domain_number,
    const PtpPortIdentity& source_port_identity) noexcept {
    const auto iterator = std::find_if(sources_.begin(), sources_.end(), [&](const SourceState& state) {
        return state.snapshot.domain_number == domain_number &&
               state.snapshot.source_port_identity == source_port_identity;
    });
    return iterator == sources_.end() ? nullptr : &*iterator;
}

bool PtpPassiveMonitor::observe_ethernet_frame(
    const std::span<const std::uint8_t> ethernet_frame,
    const std::chrono::system_clock::time_point observed_at) {
    {
        const std::scoped_lock lock{mutex_};
        ++total_frames_;
    }

    PtpFrame frame;
    if (!PtpCodec::try_parse_ethernet_frame(ethernet_frame, frame)) {
        const std::scoped_lock lock{mutex_};
        ++invalid_frames_;
        return false;
    }
    observe(frame, observed_at);
    return true;
}

void PtpPassiveMonitor::observe(
    const PtpFrame& frame,
    const std::chrono::system_clock::time_point observed_at) {
    const PtpObservedMessage observed{
        observed_at,
        frame.header.message_type,
        frame.header.domain_number,
        frame.header.source_port_identity,
        frame.header.sequence_id,
        frame.header.is_two_step(),
        frame.vlan_id,
        frame.outer_vlan_id,
        frame.peer_delay_multicast,
    };

    const std::scoped_lock lock{mutex_};
    ++valid_frames_;
    recent_messages_.push_back(observed);
    while (recent_messages_.size() > recent_capacity_) {
        recent_messages_.pop_front();
    }

    auto* source = find_source(frame.header.domain_number, frame.header.source_port_identity);
    if (source == nullptr) {
        SourceState state;
        state.snapshot.domain_number = frame.header.domain_number;
        state.snapshot.source_port_identity = frame.header.source_port_identity;
        state.snapshot.first_seen_at = observed_at;
        state.snapshot.last_seen_at = observed_at;
        sources_.push_back(state);
        source = &sources_.back();
    }

    auto& state = source->snapshot;
    state.last_seen_at = observed_at;
    state.vlan_id = frame.vlan_id;
    state.outer_vlan_id = frame.outer_vlan_id;
    const auto index = message_index(frame.header.message_type);
    ++state.message_counts[index];
    if (state.last_sequence_ids[index].has_value()) {
        const auto expected = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(*state.last_sequence_ids[index]) + 1U);
        if (frame.header.sequence_id != expected) {
            ++state.sequence_anomaly_count;
        }
    }
    state.last_sequence_ids[index] = frame.header.sequence_id;
}

PtpMonitorSnapshot PtpPassiveMonitor::snapshot(
    const std::chrono::system_clock::time_point captured_at) const {
    const std::scoped_lock lock{mutex_};
    PtpMonitorSnapshot result;
    result.captured_at = captured_at;
    result.total_frames = total_frames_;
    result.valid_ptp_frames = valid_frames_;
    result.invalid_frames = invalid_frames_;
    result.recent_messages.assign(recent_messages_.begin(), recent_messages_.end());
    result.sources.reserve(sources_.size());
    for (const auto& source : sources_) {
        result.sources.push_back(source.snapshot);
    }
    std::sort(result.sources.begin(), result.sources.end(), [](const auto& left, const auto& right) {
        if (left.domain_number != right.domain_number) {
            return left.domain_number < right.domain_number;
        }
        return left.source_port_identity.clock_identity < right.source_port_identity.clock_identity;
    });
    return result;
}

void PtpPassiveMonitor::reset() {
    const std::scoped_lock lock{mutex_};
    total_frames_ = 0U;
    valid_frames_ = 0U;
    invalid_frames_ = 0U;
    recent_messages_.clear();
    sources_.clear();
}

PtpTimingHealthReport PtpTimingHealthValidator::evaluate(
    const PtpMonitorSnapshot& snapshot,
    const PtpTimingHealthOptions& options) {
    PtpTimingHealthReport report;
    report.evaluated_at = snapshot.captured_at;
    report.snapshot = snapshot;

    auto& checks = report.checks;
    if (!snapshot.has_ptp()) {
        checks.push_back({
            "ptp.visibility",
            PtpHealthSeverity::fail,
            "No valid PTP frame has been observed on the selected interface.",
        });
        report.severity = PtpHealthSeverity::fail;
        return report;
    }

    checks.push_back({
        "ptp.visibility",
        PtpHealthSeverity::ok,
        "Observed " + std::to_string(snapshot.valid_ptp_frames) + " valid PTP frame(s).",
    });

    std::vector<const PtpSourceClockSnapshot*> active_sources;
    active_sources.reserve(snapshot.sources.size());
    for (const auto& source : snapshot.sources) {
        if (snapshot.captured_at >= source.last_seen_at &&
            snapshot.captured_at - source.last_seen_at <= options.source_timeout) {
            active_sources.push_back(&source);
        }
    }

    if (active_sources.empty()) {
        checks.push_back({
            "ptp.liveness",
            PtpHealthSeverity::fail,
            "No PTP source is within the configured liveness timeout.",
        });
    } else {
        checks.push_back({
            "ptp.liveness",
            PtpHealthSeverity::ok,
            std::to_string(active_sources.size()) + " active PTP source(s) are visible.",
        });
    }

    if (options.expected_domain_number.has_value()) {
        const auto matching = std::count_if(active_sources.begin(), active_sources.end(), [&](const auto* source) {
            return source->domain_number == *options.expected_domain_number;
        });
        if (matching == 0) {
            checks.push_back({
                "ptp.domain",
                PtpHealthSeverity::fail,
                "No active source is using expected PTP domain " +
                    std::to_string(*options.expected_domain_number) + ".",
            });
        } else {
            checks.push_back({
                "ptp.domain",
                PtpHealthSeverity::ok,
                "PTP domain " + std::to_string(*options.expected_domain_number) + " is visible.",
            });
        }
    }

    const PtpSourceClockSnapshot* selected = nullptr;
    for (const auto* source : active_sources) {
        if (options.expected_domain_number.has_value() &&
            source->domain_number != *options.expected_domain_number) {
            continue;
        }
        if (selected == nullptr ||
            source->count(PtpMessageType::announce) > selected->count(PtpMessageType::announce) ||
            (source->count(PtpMessageType::announce) == selected->count(PtpMessageType::announce) &&
             source->count(PtpMessageType::sync) > selected->count(PtpMessageType::sync)) ||
            (source->count(PtpMessageType::announce) == selected->count(PtpMessageType::announce) &&
             source->count(PtpMessageType::sync) == selected->count(PtpMessageType::sync) &&
             source->last_seen_at > selected->last_seen_at)) {
            selected = source;
        }
    }

    if (selected == nullptr) {
        report.severity = report_severity(checks);
        return report;
    }
    report.selected_source = selected->source_port_identity;

    if (options.require_announce) {
        add_presence_check(checks, *selected, PtpMessageType::announce, "ptp.announce", "Announce");
    }
    if (options.require_sync) {
        add_presence_check(checks, *selected, PtpMessageType::sync, "ptp.sync", "Sync");
    }
    if (options.require_follow_up_for_two_step) {
        add_presence_check(checks, *selected, PtpMessageType::follow_up, "ptp.followup", "Follow_Up");
    }
    if (options.require_peer_delay_activity) {
        const auto peer_delay_count =
            selected->count(PtpMessageType::pdelay_req) +
            selected->count(PtpMessageType::pdelay_resp) +
            selected->count(PtpMessageType::pdelay_resp_follow_up);
        checks.push_back(peer_delay_count != 0U
            ? PtpHealthCheck{
                "ptp.pdelay", PtpHealthSeverity::ok, "Peer-delay activity is visible."}
            : PtpHealthCheck{
                "ptp.pdelay", PtpHealthSeverity::warning,
                "No peer-delay activity is visible; some power-utility profiles expect peer-delay behavior."});
    }

    if (selected->sequence_anomaly_count > options.maximum_sequence_anomalies) {
        checks.push_back({
            "ptp.sequence",
            PtpHealthSeverity::warning,
            "Detected " + std::to_string(selected->sequence_anomaly_count) +
                " PTP sequence anomaly/anomalies for " + port_identity_text(selected->source_port_identity) + ".",
        });
    } else {
        checks.push_back({
            "ptp.sequence",
            PtpHealthSeverity::ok,
            "No PTP sequence anomaly above threshold.",
        });
    }

    report.severity = report_severity(checks);
    return report;
}

SmpSynchValue resolve_smp_synch(
    const PtpTimingHealthReport& report,
    const bool allow_local_fallback) noexcept {
    if (report.is_healthy()) {
        return SmpSynchValue::global_synchronized;
    }
    if (allow_local_fallback && report.snapshot.has_ptp()) {
        return SmpSynchValue::local_synchronized;
    }
    return SmpSynchValue::not_synchronized;
}

} // namespace ar::iec61850::time_sync
