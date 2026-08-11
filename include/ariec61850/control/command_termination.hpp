// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/control/guarded_control.hpp"
#include "ariec61850/mms/reporting.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::control {

struct LastApplError final {
    std::string control_object;
    std::optional<std::int64_t> origin_category;
    std::vector<std::uint8_t> origin_identifier;
    std::optional<std::uint8_t> control_number;
    std::int64_t raw_control_error{};
    std::int64_t raw_add_cause{25};
    ControlError control_error{ControlError::no_error};
    AddCause add_cause{AddCause::none};
    std::string control_error_name;
    std::string add_cause_name;
};

struct CommandCorrelation final {
    std::int64_t origin_category{};
    std::span<const std::uint8_t> origin_identifier;
    std::uint8_t control_number{};
};

struct CommandTermination final {
    bool is_for_control_object{};
    bool is_termination{};
    bool positive{};
    ControlError control_error{ControlError::no_error};
    AddCause add_cause{AddCause::none};
    std::int64_t raw_control_error{};
    std::int64_t raw_add_cause{25};
    std::string control_error_name;
    std::string add_cause_name;
    std::optional<LastApplError> last_appl_error;
};

class CommandTerminationDecoder final {
public:
    [[nodiscard]] static CommandTermination decode(
        const mms::MmsInformationReport& report,
        const ControlObjectReference& object,
        const CommandCorrelation* correlation = nullptr);

    [[nodiscard]] static std::optional<LastApplError> try_decode_last_appl_error(
        const mms::MmsDataValue& value);

    [[nodiscard]] static bool matches_operate_reference(
        const ControlObjectReference& object,
        const std::string& reference);

    [[nodiscard]] static bool matches_reported_reference(
        const ControlObjectReference& object,
        const std::string& reference);

    [[nodiscard]] static std::string control_error_name(std::int64_t value);
    [[nodiscard]] static std::string add_cause_name(std::int64_t value);
};

} // namespace ar::iec61850::control
