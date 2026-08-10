// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/command_termination.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::control {
namespace {

[[nodiscard]] char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] bool ascii_equal(
    const std::string& left,
    const std::string& right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ascii_ends_with(
    const std::string& value,
    const std::string& suffix) noexcept {
    if (suffix.size() > value.size()) {
        return false;
    }
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        if (ascii_lower(value[offset + index]) != ascii_lower(suffix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ascii_starts_with(
    const std::string& value,
    const std::string& prefix) noexcept {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (ascii_lower(value[index]) != ascii_lower(prefix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ascii_contains(
    const std::string& value,
    const std::string& needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > value.size()) {
        return false;
    }
    for (std::size_t offset = 0U; offset + needle.size() <= value.size(); ++offset) {
        bool match = true;
        for (std::size_t index = 0U; index < needle.size(); ++index) {
            if (ascii_lower(value[offset + index]) != ascii_lower(needle[index])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string trim_delimiter(std::string value, const char delimiter) {
    while (!value.empty() && value.front() == delimiter) {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == delimiter) {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::string object_path_dollars(const ControlObjectReference& object) {
    std::string result{object.data_object_path};
    std::replace(result.begin(), result.end(), '.', '$');
    return result;
}

[[nodiscard]] std::string object_reference(const ControlObjectReference& object) {
    return std::string{object.domain} + "/" + std::string{object.logical_node} + "." +
        std::string{object.data_object_path};
}

[[nodiscard]] std::string mms_reference(const mms::MmsObjectName& name) {
    switch (name.kind) {
    case mms::MmsObjectNameKind::domain_specific:
        return name.domain + "/" + name.item;
    case mms::MmsObjectNameKind::vmd_specific:
    case mms::MmsObjectNameKind::aa_specific:
        return name.item;
    }
    return {};
}

[[nodiscard]] std::string read_text(const mms::MmsDataValue& value) {
    if (value.kind() != mms::MmsDataKind::visible_string &&
        value.kind() != mms::MmsDataKind::mms_string) {
        return {};
    }
    const auto* text = std::get_if<std::string>(&value.value());
    return text == nullptr ? std::string{} : *text;
}

[[nodiscard]] std::optional<std::int64_t> read_number(
    const mms::MmsDataValue& value) noexcept {
    if (value.kind() == mms::MmsDataKind::integer) {
        if (const auto* number = std::get_if<std::int64_t>(&value.value())) {
            return *number;
        }
        return std::nullopt;
    }
    if (value.kind() == mms::MmsDataKind::unsigned_integer) {
        if (const auto* number = std::get_if<std::uint64_t>(&value.value())) {
            if (*number <= static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return static_cast<std::int64_t>(*number);
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::uint8_t> read_octets(
    const mms::MmsDataValue& value) {
    if (value.kind() != mms::MmsDataKind::octet_string) {
        return {};
    }
    return value.raw_value();
}

[[nodiscard]] bool is_generic_last_appl_error_reference(
    const std::string& reference) noexcept {
    return ascii_equal(reference, "LastApplError") ||
        ascii_ends_with(reference, "$LastApplError") ||
        ascii_ends_with(reference, ".LastApplError");
}

[[nodiscard]] bool matches_correlation(
    const LastApplError& error,
    const CommandCorrelation& expected) noexcept {
    return error.control_object.empty() &&
        error.origin_category.has_value() &&
        error.origin_category.value() == expected.origin_category &&
        error.control_number.has_value() &&
        error.control_number.value() == expected.control_number &&
        std::equal(
            error.origin_identifier.begin(), error.origin_identifier.end(),
            expected.origin_identifier.begin(), expected.origin_identifier.end());
}

[[nodiscard]] ControlError map_control_error(const std::int64_t value) noexcept {
    switch (value) {
    case 0: return ControlError::no_error;
    case 1: return ControlError::unknown;
    case 2: return ControlError::timeout_test;
    case 3: return ControlError::operator_test;
    default: return ControlError::unknown;
    }
}

[[nodiscard]] AddCause map_add_cause(const std::int64_t value) noexcept {
    if (value >= 0 && value <= 27) {
        return static_cast<AddCause>(static_cast<std::uint8_t>(value));
    }
    return AddCause::unknown;
}

[[nodiscard]] bool positive_termination(
    const std::int64_t error,
    const std::int64_t add_cause) noexcept {
    return error == 0 && (add_cause == 0 || add_cause == 25);
}

} // namespace

std::string CommandTerminationDecoder::control_error_name(const std::int64_t value) {
    switch (value) {
    case 0: return "no-error";
    case 1: return "unknown";
    case 2: return "timeout-test";
    case 3: return "operator-test";
    default: return "control-error-" + std::to_string(value);
    }
}

std::string CommandTerminationDecoder::add_cause_name(const std::int64_t value) {
    switch (value) {
    case 0: return "unknown";
    case 1: return "not-supported";
    case 2: return "blocked-by-switching-hierarchy";
    case 3: return "select-failed";
    case 4: return "invalid-position";
    case 5: return "position-reached";
    case 6: return "parameter-change-in-execution";
    case 7: return "step-limit";
    case 8: return "blocked-by-mode";
    case 9: return "blocked-by-process";
    case 10: return "blocked-by-interlocking";
    case 11: return "blocked-by-synchrocheck";
    case 12: return "command-already-in-execution";
    case 13: return "blocked-by-health";
    case 14: return "one-of-n-control";
    case 15: return "abortion-by-cancel";
    case 16: return "time-limit-over";
    case 17: return "abortion-by-trip";
    case 18: return "object-not-selected";
    case 19: return "object-already-selected";
    case 20: return "no-access-authority";
    case 21: return "ended-with-overshoot";
    case 22: return "abortion-due-to-deviation";
    case 23: return "abortion-by-communication-loss";
    case 24: return "abortion-by-command";
    case 25: return "none";
    case 26: return "inconsistent-parameters";
    case 27: return "locked-by-other-client";
    default: return "add-cause-" + std::to_string(value);
    }
}

bool CommandTerminationDecoder::matches_operate_reference(
    const ControlObjectReference& object,
    const std::string& reference) {
    if (!object.valid() || reference.empty()) {
        return false;
    }
    std::string normalized = reference;
    std::replace(normalized.begin(), normalized.end(), '.', '$');
    std::replace(normalized.begin(), normalized.end(), '/', '$');
    normalized = trim_delimiter(std::move(normalized), '$');

    const auto data_path = object_path_dollars(object);
    const auto expected = std::string{object.domain} + "$" +
        std::string{object.logical_node} + "$CO$" + data_path + "$Oper";
    const auto suffix = std::string{object.logical_node} + "$CO$" +
        data_path + "$Oper";
    return ascii_equal(normalized, expected) || ascii_ends_with(normalized, suffix);
}

bool CommandTerminationDecoder::matches_reported_reference(
    const ControlObjectReference& object,
    const std::string& reference) {
    if (!object.valid() || reference.empty()) {
        return false;
    }
    std::string normalized = reference;
    std::replace(normalized.begin(), normalized.end(), '$', '.');
    const auto expected = object_reference(object);
    const auto logical_suffix = "/" + std::string{object.logical_node} + "." +
        std::string{object.data_object_path};
    const auto co_object = std::string{object.domain} + "/" +
        std::string{object.logical_node} + ".CO." +
        std::string{object.data_object_path};
    const auto co_suffix = "/" + std::string{object.logical_node} + ".CO." +
        std::string{object.data_object_path};
    return ascii_equal(normalized, expected) ||
        ascii_starts_with(normalized, expected + ".") ||
        ascii_ends_with(normalized, logical_suffix) ||
        ascii_equal(normalized, co_object) ||
        ascii_equal(normalized, co_object + ".") ||
        ascii_ends_with(normalized, co_suffix) ||
        ascii_contains(normalized, co_suffix + ".");
}

std::optional<LastApplError> CommandTerminationDecoder::try_decode_last_appl_error(
    const mms::MmsDataValue& value) {
    if (value.kind() != mms::MmsDataKind::structure) {
        return std::nullopt;
    }
    const auto& children = value.children();
    if (children.size() < 2U) {
        return std::nullopt;
    }

    std::vector<std::pair<std::size_t, std::int64_t>> numeric;
    numeric.reserve(children.size());
    for (std::size_t index = 0U; index < children.size(); ++index) {
        if (const auto number = read_number(children[index]); number.has_value()) {
            numeric.emplace_back(index, number.value());
        }
    }
    if (numeric.size() < 2U) {
        return std::nullopt;
    }

    std::optional<std::int64_t> error;
    for (const auto& [index, number] : numeric) {
        if (index == 0U || index == 1U) {
            error = number;
            break;
        }
    }
    if (!error.has_value()) {
        return std::nullopt;
    }

    const auto add_cause = numeric.back().second;
    LastApplError result;
    result.control_object = read_text(children.front());
    const auto error_index = std::find_if(
        numeric.begin(), numeric.end(), [](const auto& entry) {
            return entry.first == 0U || entry.first == 1U;
        })->first;
    const auto origin_index = error_index + 1U;
    const auto control_number_index = error_index + 2U;
    if (origin_index < children.size() &&
        children[origin_index].kind() == mms::MmsDataKind::structure) {
        const auto& origin = children[origin_index].children();
        if (origin.size() >= 2U) {
            result.origin_category = read_number(origin[0]);
            result.origin_identifier = read_octets(origin[1]);
        }
    }
    if (control_number_index < children.size()) {
        const auto control_number = read_number(children[control_number_index]);
        if (control_number.has_value() && control_number.value() >= 0 &&
            control_number.value() <= std::numeric_limits<std::uint8_t>::max()) {
            result.control_number = static_cast<std::uint8_t>(control_number.value());
        }
    }
    result.raw_control_error = error.value();
    result.raw_add_cause = add_cause;
    result.control_error = map_control_error(error.value());
    result.add_cause = map_add_cause(add_cause);
    result.control_error_name = control_error_name(error.value());
    result.add_cause_name = add_cause_name(add_cause);
    return result;
}

CommandTermination CommandTerminationDecoder::decode(
    const mms::MmsInformationReport& report,
    const ControlObjectReference& object,
    const CommandCorrelation* correlation) {
    CommandTermination result;
    if (!object.valid()) {
        return result;
    }

    bool matching_object = false;
    bool matching_operate = false;
    bool generic_last_appl_error = false;
    for (const auto& reference : report.variable_references) {
        const auto text = mms_reference(reference);
        matching_object = matching_object || matches_reported_reference(object, text);
        matching_operate = matching_operate || matches_operate_reference(object, text);
        generic_last_appl_error = generic_last_appl_error ||
            is_generic_last_appl_error_reference(text);
    }

    for (const auto& item : report.items) {
        if (!item.value.has_value() ||
            item.value->kind() != mms::MmsDataKind::structure) {
            continue;
        }
        const auto last_error = try_decode_last_appl_error(item.value.value());
        if (!last_error.has_value()) {
            continue;
        }

        const bool embedded_match = !last_error->control_object.empty() &&
            (matches_reported_reference(object, last_error->control_object) ||
             matches_operate_reference(object, last_error->control_object));
        const bool correlated_generic = generic_last_appl_error &&
            correlation != nullptr && matches_correlation(*last_error, *correlation);
        if (!matching_object && !matching_operate && !embedded_match &&
            !correlated_generic) {
            continue;
        }

        result.is_for_control_object = true;
        result.is_termination = true;
        result.positive = positive_termination(
            last_error->raw_control_error, last_error->raw_add_cause);
        result.control_error = last_error->control_error;
        result.add_cause = last_error->add_cause;
        result.raw_control_error = last_error->raw_control_error;
        result.raw_add_cause = last_error->raw_add_cause;
        result.control_error_name = last_error->control_error_name;
        result.add_cause_name = last_error->add_cause_name;
        result.last_appl_error = last_error;
        return result;
    }

    // Mirror the C# oracle: a positive enhanced-security termination may carry
    // only the exact CO/Oper variable. Ordinary ST/MX process reports must not
    // complete the pending command.
    if (!matching_operate) {
        return result;
    }

    result.is_for_control_object = true;
    result.is_termination = true;
    result.positive = true;
    result.control_error = ControlError::no_error;
    result.add_cause = AddCause::none;
    result.raw_control_error = 0;
    result.raw_add_cause = 25;
    result.control_error_name = "no-error";
    result.add_cause_name = "none";
    return result;
}

} // namespace ar::iec61850::control
