// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/injector_controller.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
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
};

enum class InjectorControlParseStatus : std::uint8_t {
    ok,
    malformed,
    missing_command,
    unsupported_command,
    missing_scenario,
    unsupported_scenario,
};

struct InjectorControlCommand final {
    InjectorControlCommandKind kind{InjectorControlCommandKind::status};
    InjectorScenarioKind scenario{InjectorScenarioKind::normal};
};

struct InjectorControlParseResult final {
    InjectorControlParseStatus status{InjectorControlParseStatus::malformed};
    InjectorControlCommand command{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == InjectorControlParseStatus::ok;
    }
};

namespace detail {

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

[[nodiscard]] inline bool json_string_value(
    const std::string_view object,
    const std::string_view key,
    std::string_view& output) noexcept {
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

        auto cursor = quote + quoted_key_length;
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
        if (cursor >= object.size() || object[cursor] != '"') {
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
    return false;
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
    } else {
        return {InjectorControlParseStatus::unsupported_command, {}};
    }

    return {InjectorControlParseStatus::ok, command};
}

} // namespace ar::iec61850::sampled_values
