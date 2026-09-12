// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/simulation/ied_simulator_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <utility>

namespace ar::iec61850::simulation {
namespace {

[[nodiscard]] char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] bool ascii_ends_with(
    const std::string_view value,
    const std::string_view suffix) noexcept {
    if (suffix.size() > value.size()) return false;
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        if (ascii_lower(value[offset + index]) != ascii_lower(suffix[index])) return false;
    }
    return true;
}

[[nodiscard]] std::string format_number(const double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(3) << value;
    auto result = stream.str();
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result.empty() ? std::string{"0"} : result;
}

} // namespace

IedSimulatorEngine::IedSimulatorEngine(IedSimulatorProfile profile)
    : profile_(std::move(profile)) {
    for (const auto& device : profile_.logical_devices) {
        for (const auto& node : device.logical_nodes) {
            points_.insert(points_.end(), node.points.begin(), node.points.end());
        }
    }
    reset();
}

std::size_t IedSimulatorEngine::CaseInsensitiveHash::operator()(
    const std::string& value) const noexcept {
    std::size_t hash = static_cast<std::size_t>(1469598103934665603ULL);
    constexpr std::size_t prime = static_cast<std::size_t>(1099511628211ULL);
    for (const auto character : value) {
        hash ^= static_cast<std::size_t>(
            static_cast<unsigned char>(ascii_lower(character)));
        hash *= prime;
    }
    return hash;
}

bool IedSimulatorEngine::CaseInsensitiveEqual::operator()(
    const std::string& left,
    const std::string& right) const noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) return false;
    }
    return true;
}

void IedSimulatorEngine::rebuild_index() {
    index_.clear();
    index_.reserve(states_.size() * 3U);
    for (std::size_t index = 0U; index < states_.size(); ++index) {
        const auto& point = points_[index];
        index_.try_emplace(point.reference, index);
        if (!point.relative_reference.empty()) index_.try_emplace(point.relative_reference, index);
        if (!point.mms_domain.empty() && !point.mms_item.empty()) {
            index_.try_emplace(point.mms_domain + "/" + point.mms_item, index);
        }
    }
}

void IedSimulatorEngine::reset(const std::uint64_t timestamp_unix_milliseconds) {
    states_.clear();
    states_.reserve(points_.size());
    for (const auto& point : points_) {
        IedSimulatorPointState state;
        state.reference = point.reference;
        state.functional_constraint = point.functional_constraint;
        state.kind = point.kind;
        state.unit = point.unit;
        state.value = point.initial_value;
        state.quality = "good";
        state.origin = "Simulator";
        state.timestamp_unix_milliseconds = timestamp_unix_milliseconds;
        state.reason = "init";
        states_.push_back(std::move(state));
    }
    step_index_ = 0U;
    rebuild_index();
}

const IedSimulatorPointState* IedSimulatorEngine::find_point_state(
    const std::string& reference) const noexcept {
    const auto found = index_.find(reference);
    if (found == index_.end() || found->second >= states_.size()) return nullptr;
    return &states_[found->second];
}

std::optional<IedSimulatorEvent> IedSimulatorEngine::set_point_value(
    const std::string& reference,
    std::string value,
    std::string quality,
    std::string origin,
    const std::uint64_t timestamp_unix_milliseconds) {
    const auto found = index_.find(reference);
    if (found == index_.end() || found->second >= states_.size()) return std::nullopt;

    auto& state = states_[found->second];
    const auto previous_value = state.value;
    const auto previous_quality = state.quality;
    const bool value_changed = previous_value != value;
    const bool quality_changed = previous_quality != quality;
    const bool origin_changed = state.origin != origin;

    state.value = std::move(value);
    state.quality = std::move(quality);
    state.origin = std::move(origin);
    state.timestamp_unix_milliseconds = timestamp_unix_milliseconds;
    state.reason = value_changed ? "data-change"
        : (quality_changed ? "quality-change"
                           : (origin_changed ? "origin-change" : "manual"));

    if (!value_changed && !quality_changed && !origin_changed) return std::nullopt;

    IedSimulatorEvent event;
    event.timestamp_unix_milliseconds = timestamp_unix_milliseconds;
    event.reference = state.reference;
    event.functional_constraint = state.functional_constraint;
    event.previous_value = previous_value;
    event.new_value = state.value;
    event.previous_quality = previous_quality;
    event.new_quality = state.quality;
    event.reason = state.reason;
    return event;
}

bool IedSimulatorEngine::should_simulate(const IedSimulatorPoint& point) noexcept {
    return point.dynamic ||
        ascii_ends_with(point.relative_reference, "PTOC1.Str.general") ||
        ascii_ends_with(point.relative_reference, "PTOC1.Op.general") ||
        ascii_ends_with(point.relative_reference, "XCBR1.Pos.stVal") ||
        ascii_ends_with(point.relative_reference, "CSWI1.Pos.stVal");
}

std::string IedSimulatorEngine::compute_value(
    const IedSimulatorPoint& point,
    const double angle) const {
    if (point.kind == SimulatorPointKind::measurement) {
        constexpr double pi = 3.14159265358979323846;
        const auto radians = angle + point.phase_degrees * pi / 180.0;
        return format_number(point.base_value + std::sin(radians) * point.amplitude);
    }
    if (ascii_ends_with(point.relative_reference, "PTOC1.Str.general")) {
        const auto phase = step_index_ % 40U;
        return phase >= 25U && phase <= 31U ? "true" : "false";
    }
    if (ascii_ends_with(point.relative_reference, "PTOC1.Op.general")) {
        const auto phase = step_index_ % 40U;
        return phase >= 30U && phase <= 31U ? "true" : "false";
    }
    if (ascii_ends_with(point.relative_reference, "XCBR1.Pos.stVal") ||
        ascii_ends_with(point.relative_reference, "CSWI1.Pos.stVal")) {
        return (step_index_ % 80U) >= 60U ? "open" : "closed";
    }
    return point.initial_value;
}

std::vector<IedSimulatorEvent> IedSimulatorEngine::step(
    const std::uint64_t now_unix_milliseconds) {
    ++step_index_;
    std::vector<IedSimulatorEvent> events;
    const auto angle = static_cast<double>(step_index_) * 0.12;
    for (std::size_t index = 0U; index < points_.size(); ++index) {
        const auto& point = points_[index];
        if (!should_simulate(point)) continue;
        auto& state = states_[index];
        const auto previous = state.value;
        const auto next = compute_value(point, angle);
        state.value = next;
        state.timestamp_unix_milliseconds = now_unix_milliseconds;
        state.reason = previous == next ? "sample" : "data-change";
        if (previous == next) continue;

        IedSimulatorEvent event;
        event.timestamp_unix_milliseconds = now_unix_milliseconds;
        event.reference = state.reference;
        event.functional_constraint = state.functional_constraint;
        event.previous_value = previous;
        event.new_value = next;
        event.previous_quality = state.quality;
        event.new_quality = state.quality;
        event.reason = state.reason;
        events.push_back(std::move(event));
    }
    return events;
}

IedSimulatorSnapshot IedSimulatorEngine::snapshot(
    const std::uint64_t now_unix_milliseconds) const {
    IedSimulatorSnapshot result;
    result.generated_at_unix_milliseconds = now_unix_milliseconds;
    result.profile_name = profile_.name;
    result.logical_device_count = profile_.logical_devices.size();
    result.logical_node_count = profile_.logical_node_count();
    result.point_count = profile_.point_count();
    result.data_set_count = profile_.data_sets.size();
    result.report_control_block_count = profile_.report_control_blocks.size();
    result.points = states_;
    return result;
}

} // namespace ar::iec61850::simulation
