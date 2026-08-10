// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/injector_controller.hpp"
#include "ariec61850/sampled_values/injector_runtime_program.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ar::iec61850::sampled_values {

enum class InjectorControlCommandKind : std::uint8_t {
    capabilities,
    configure,
    arm,
    start,
    stop,
    status,
    stats,
    set_channel,
    ramp_channel,
    sequence_begin,
    sequence_state_begin,
    sequence_set_channel,
    sequence_state_commit,
    sequence_commit,
    sequence_abort,
};

enum class InjectorControlParseStatus : std::uint8_t {
    ok,
    malformed,
    missing_command,
    unsupported_command,
    missing_scenario,
    unsupported_scenario,
    missing_channel,
    unsupported_channel,
    missing_value,
    invalid_value,
    unsupported_transition,
};

struct InjectorControlCommand final {
    InjectorControlCommandKind kind{InjectorControlCommandKind::status};
    InjectorScenarioKind scenario{InjectorScenarioKind::normal};
    InjectorChannelEdit channel_edit{};
    std::uint32_t duration_samples{};
    InjectorSegmentTransition transition{InjectorSegmentTransition::step};
};

struct InjectorControlParseResult final {
    InjectorControlParseStatus status{InjectorControlParseStatus::malformed};
    InjectorControlCommand command{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == InjectorControlParseStatus::ok;
    }
};

namespace detail {

enum class JsonIntegerStatus : std::uint8_t {
    missing,
    ok,
    malformed,
};

[[nodiscard]] inline std::string_view trim_ascii(
    std::string_view value) noexcept {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] inline bool find_json_value_cursor(
    const std::string_view object,
    const std::string_view key,
    std::size_t& cursor) noexcept {
    const auto quoted_key_length = key.size() + 2U;
    if (quoted_key_length > object.size()) {
        return false;
    }

    std::size_t position = 0U;
    while (position + quoted_key_length <= object.size()) {
        const auto quote = object.find('"', position);
        if (quote == std::string_view::npos ||
            quote + quoted_key_length > object.size()) {
            return false;
        }
        if (object.substr(quote + 1U, key.size()) != key ||
            object[quote + 1U + key.size()] != '"') {
            position = quote + 1U;
            continue;
        }

        cursor = quote + quoted_key_length;
        while (cursor < object.size() &&
               std::isspace(static_cast<unsigned char>(object[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= object.size() || object[cursor] != ':') {
            return false;
        }
        ++cursor;
        while (cursor < object.size() &&
               std::isspace(static_cast<unsigned char>(object[cursor])) != 0) {
            ++cursor;
        }
        return cursor < object.size();
    }
    return false;
}

[[nodiscard]] inline bool json_string_value(
    const std::string_view object,
    const std::string_view key,
    std::string_view& output) noexcept {
    std::size_t cursor{};
    if (!find_json_value_cursor(object, key, cursor) || object[cursor] != '"') {
        return false;
    }
    ++cursor;

    const auto end = object.find('"', cursor);
    if (end == std::string_view::npos) {
        return false;
    }
    if (object.substr(cursor, end - cursor).find('\\') !=
        std::string_view::npos) {
        return false;
    }
    output = object.substr(cursor, end - cursor);
    return true;
}

[[nodiscard]] inline JsonIntegerStatus json_integer_value(
    const std::string_view object,
    const std::string_view key,
    std::int64_t& output) noexcept {
    std::size_t cursor{};
    if (!find_json_value_cursor(object, key, cursor)) {
        return JsonIntegerStatus::missing;
    }

    bool negative = false;
    if (object[cursor] == '-') {
        negative = true;
        ++cursor;
    }
    if (cursor >= object.size() ||
        std::isdigit(static_cast<unsigned char>(object[cursor])) == 0) {
        return JsonIntegerStatus::malformed;
    }

    std::uint64_t magnitude{};
    constexpr auto positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr auto negative_limit = positive_limit + 1ULL;
    const auto limit = negative ? negative_limit : positive_limit;

    while (cursor < object.size() &&
           std::isdigit(static_cast<unsigned char>(object[cursor])) != 0) {
        const auto digit = static_cast<std::uint64_t>(object[cursor] - '0');
        if (magnitude > (limit - digit) / 10ULL) {
            return JsonIntegerStatus::malformed;
        }
        magnitude = magnitude * 10ULL + digit;
        ++cursor;
    }

    while (cursor < object.size() &&
           std::isspace(static_cast<unsigned char>(object[cursor])) != 0) {
        ++cursor;
    }
    if (cursor < object.size() && object[cursor] != ',' && object[cursor] != '}') {
        return JsonIntegerStatus::malformed;
    }

    if (negative) {
        if (magnitude == negative_limit) {
            output = std::numeric_limits<std::int64_t>::min();
        } else {
            output = -static_cast<std::int64_t>(magnitude);
        }
    } else {
        output = static_cast<std::int64_t>(magnitude);
    }
    return JsonIntegerStatus::ok;
}

[[nodiscard]] inline bool parse_channel_index(
    const std::string_view name,
    std::uint8_t& index) noexcept {
    constexpr std::string_view names[injector_channel_count]{
        "Ia", "Ib", "Ic", "In", "Va", "Vb", "Vc", "Vn"};
    for (std::size_t candidate = 0U; candidate < injector_channel_count; ++candidate) {
        if (name == names[candidate]) {
            index = static_cast<std::uint8_t>(candidate);
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline InjectorControlParseStatus parse_channel_edit(
    const std::string_view line,
    InjectorChannelEdit& edit,
    const bool require_duration) noexcept {
    std::string_view channel_name;
    if (!json_string_value(line, "channel", channel_name)) {
        return InjectorControlParseStatus::missing_channel;
    }
    if (!parse_channel_index(channel_name, edit.channel_index)) {
        return InjectorControlParseStatus::unsupported_channel;
    }

    auto parse_signed = [&](const std::string_view key,
                            const std::uint16_t bit,
                            std::int32_t& target) noexcept -> bool {
        std::int64_t value{};
        const auto status = json_integer_value(line, key, value);
        if (status == JsonIntegerStatus::missing) {
            return true;
        }
        if (status != JsonIntegerStatus::ok ||
            value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        target = static_cast<std::int32_t>(value);
        edit.fields = static_cast<std::uint16_t>(edit.fields | bit);
        return true;
    };

    auto parse_unsigned = [&](const std::string_view key,
                              const std::uint16_t bit,
                              const std::uint64_t maximum,
                              std::uint32_t& target) noexcept -> bool {
        std::int64_t value{};
        const auto status = json_integer_value(line, key, value);
        if (status == JsonIntegerStatus::missing) {
            return true;
        }
        if (status != JsonIntegerStatus::ok || value < 0 ||
            static_cast<std::uint64_t>(value) > maximum) {
            return false;
        }
        target = static_cast<std::uint32_t>(value);
        edit.fields = static_cast<std::uint16_t>(edit.fields | bit);
        return true;
    };

    std::int64_t enabled{};
    const auto enabled_status = json_integer_value(line, "enabled", enabled);
    if (enabled_status == JsonIntegerStatus::malformed ||
        (enabled_status == JsonIntegerStatus::ok && enabled != 0 && enabled != 1)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if (enabled_status == JsonIntegerStatus::ok) {
        edit.enabled = enabled != 0;
        edit.fields = static_cast<std::uint16_t>(
            edit.fields | injector_field_enabled);
    }

    if (!parse_signed("rms", injector_field_rms, edit.rms_counts) ||
        !parse_signed("dc", injector_field_dc, edit.dc_counts) ||
        !parse_signed("phaseMilliDeg", injector_field_phase, edit.phase_millidegrees)) {
        return InjectorControlParseStatus::invalid_value;
    }

    std::uint32_t frequency{};
    if (!parse_unsigned(
            "frequencyMilliHz",
            injector_field_frequency,
            2'000'000ULL,
            frequency)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if ((edit.fields & injector_field_frequency) != 0U) {
        edit.frequency_millihz = frequency;
    }

    std::uint32_t harmonic{};
    if (!parse_unsigned(
            "harmonicPermyriad",
            injector_field_harmonic,
            injector_gain_one,
            harmonic)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if ((edit.fields & injector_field_harmonic) != 0U) {
        edit.harmonic_permyriad = static_cast<std::uint16_t>(harmonic);
    }

    std::uint32_t harmonic_order{};
    if (!parse_unsigned(
            "harmonicOrder",
            injector_field_harmonic_order,
            63U,
            harmonic_order)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if ((edit.fields & injector_field_harmonic_order) != 0U) {
        if (harmonic_order < 2U) {
            return InjectorControlParseStatus::invalid_value;
        }
        edit.harmonic_order = static_cast<std::uint8_t>(harmonic_order);
    }

    std::uint32_t clip{};
    if (!parse_unsigned(
            "clipPermyriad",
            injector_field_clip,
            injector_gain_one,
            clip)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if ((edit.fields & injector_field_clip) != 0U) {
        edit.clip_permyriad = static_cast<std::uint16_t>(clip);
    }

    std::uint32_t quality{};
    if (!parse_unsigned(
            "quality",
            injector_field_quality,
            std::numeric_limits<std::uint32_t>::max(),
            quality)) {
        return InjectorControlParseStatus::invalid_value;
    }
    if ((edit.fields & injector_field_quality) != 0U) {
        edit.quality = quality;
    }

    if (edit.fields == 0U) {
        return InjectorControlParseStatus::missing_value;
    }

    if (require_duration) {
        std::int64_t duration{};
        const auto duration_status =
            json_integer_value(line, "durationSamples", duration);
        if (duration_status == JsonIntegerStatus::missing) {
            return InjectorControlParseStatus::missing_value;
        }
        if (duration_status != JsonIntegerStatus::ok || duration < 2 ||
            duration > 100'000'000LL) {
            return InjectorControlParseStatus::invalid_value;
        }
        edit.duration_samples = static_cast<std::uint32_t>(duration);
    }

    return InjectorControlParseStatus::ok;
}

[[nodiscard]] inline InjectorControlParseStatus parse_sequence_state_begin(
    const std::string_view line,
    InjectorControlCommand& command) noexcept {
    std::int64_t duration{};
    const auto duration_status = json_integer_value(line, "durationSamples", duration);
    if (duration_status == JsonIntegerStatus::missing) {
        return InjectorControlParseStatus::missing_value;
    }
    if (duration_status != JsonIntegerStatus::ok || duration < 1 ||
        duration > 100'000'000LL) {
        return InjectorControlParseStatus::invalid_value;
    }
    command.duration_samples = static_cast<std::uint32_t>(duration);

    std::string_view transition;
    if (!json_string_value(line, "transition", transition)) {
        command.transition = InjectorSegmentTransition::step;
        return InjectorControlParseStatus::ok;
    }
    if (transition == "step") {
        command.transition = InjectorSegmentTransition::step;
    } else if (transition == "linear") {
        command.transition = InjectorSegmentTransition::linear_from_previous;
    } else {
        return InjectorControlParseStatus::unsupported_transition;
    }
    return InjectorControlParseStatus::ok;
}

} // namespace detail

// Parses the intentionally narrow control-plane JSON subset used on MCU serial
// links. Each command is one JSON object on one line. Escaped string values,
// nested objects, arrays, and arbitrary JSON are deliberately outside v1.
[[nodiscard]] inline InjectorControlParseResult parse_injector_control_command(
    std::string_view line) noexcept {
    line = detail::trim_ascii(line);
    if (line.size() < 2U || line.front() != '{' || line.back() != '}') {
        return {InjectorControlParseStatus::malformed, {}};
    }

    std::string_view command_name;
    if (!detail::json_string_value(line, "command", command_name)) {
        return {InjectorControlParseStatus::missing_command, {}};
    }

    InjectorControlCommand command{};
    if (command_name == "capabilities") {
        command.kind = InjectorControlCommandKind::capabilities;
    } else if (command_name == "configure") {
        command.kind = InjectorControlCommandKind::configure;
        std::string_view scenario_name;
        if (!detail::json_string_value(line, "scenario", scenario_name)) {
            return {InjectorControlParseStatus::missing_scenario, {}};
        }
        if (scenario_name == "normal") {
            command.scenario = InjectorScenarioKind::normal;
        } else if (scenario_name == "protection-fault") {
            command.scenario = InjectorScenarioKind::protection_fault;
        } else {
            return {InjectorControlParseStatus::unsupported_scenario, {}};
        }
    } else if (command_name == "arm") {
        command.kind = InjectorControlCommandKind::arm;
    } else if (command_name == "start") {
        command.kind = InjectorControlCommandKind::start;
    } else if (command_name == "stop") {
        command.kind = InjectorControlCommandKind::stop;
    } else if (command_name == "status") {
        command.kind = InjectorControlCommandKind::status;
    } else if (command_name == "stats") {
        command.kind = InjectorControlCommandKind::stats;
    } else if (command_name == "set-channel") {
        command.kind = InjectorControlCommandKind::set_channel;
        const auto status = detail::parse_channel_edit(
            line, command.channel_edit, false);
        if (status != InjectorControlParseStatus::ok) {
            return {status, {}};
        }
    } else if (command_name == "ramp-channel") {
        command.kind = InjectorControlCommandKind::ramp_channel;
        const auto status = detail::parse_channel_edit(
            line, command.channel_edit, true);
        if (status != InjectorControlParseStatus::ok) {
            return {status, {}};
        }
    } else if (command_name == "sequence-begin") {
        command.kind = InjectorControlCommandKind::sequence_begin;
    } else if (command_name == "sequence-state-begin") {
        command.kind = InjectorControlCommandKind::sequence_state_begin;
        const auto status = detail::parse_sequence_state_begin(line, command);
        if (status != InjectorControlParseStatus::ok) {
            return {status, {}};
        }
    } else if (command_name == "sequence-set-channel") {
        command.kind = InjectorControlCommandKind::sequence_set_channel;
        const auto status = detail::parse_channel_edit(
            line, command.channel_edit, false);
        if (status != InjectorControlParseStatus::ok) {
            return {status, {}};
        }
    } else if (command_name == "sequence-state-commit") {
        command.kind = InjectorControlCommandKind::sequence_state_commit;
    } else if (command_name == "sequence-commit") {
        command.kind = InjectorControlCommandKind::sequence_commit;
    } else if (command_name == "sequence-abort") {
        command.kind = InjectorControlCommandKind::sequence_abort;
    } else {
        return {InjectorControlParseStatus::unsupported_command, {}};
    }

    return {InjectorControlParseStatus::ok, command};
}

} // namespace ar::iec61850::sampled_values
