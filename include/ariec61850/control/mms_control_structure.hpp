// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/control/guarded_control.hpp"
#include "ariec61850/mms/services.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ar::iec61850::control {

enum class DoublePointValue : std::uint8_t {
    intermediate = 0U,
    off = 1U,
    on = 2U,
    bad = 3U,
};

struct StepPosition final {
    std::int64_t position{};
    bool transient{};
};

enum class ControlValueKind : std::uint8_t {
    boolean,
    double_point,
    integer,
    unsigned_integer,
    floating_point,
    step_position,
    raw_mms,
};

class ControlValue final {
public:
    using Value = std::variant<
        bool,
        DoublePointValue,
        std::int64_t,
        std::uint64_t,
        double,
        StepPosition,
        mms::MmsDataValue>;

    [[nodiscard]] static ControlValue boolean(bool value);
    [[nodiscard]] static ControlValue double_point(DoublePointValue value);
    [[nodiscard]] static ControlValue integer(std::int64_t value);
    [[nodiscard]] static ControlValue unsigned_integer(std::uint64_t value);
    [[nodiscard]] static ControlValue floating_point(double value);
    [[nodiscard]] static ControlValue step_position(StepPosition value);
    [[nodiscard]] static ControlValue raw_mms(mms::MmsDataValue value);

    [[nodiscard]] ControlValueKind kind() const noexcept { return kind_; }
    [[nodiscard]] const Value& value() const noexcept { return value_; }

private:
    ControlValue(ControlValueKind kind, Value value)
        : kind_{kind}, value_{std::move(value)} {}

    ControlValueKind kind_;
    Value value_;
};

enum class MmsControlService : std::uint8_t {
    operate,
    select_with_value,
    cancel,
};

enum class MmsControlBuildStatus : std::uint8_t {
    ok,
    invalid_service_specification,
    unsupported_component,
    missing_required_component,
    type_mismatch,
    shape_mismatch,
    value_out_of_range,
    timestamp_missing,
    unsupported_control_value,
};

struct MmsControlBuildResult final {
    MmsControlBuildStatus status{MmsControlBuildStatus::invalid_service_specification};
    std::optional<mms::MmsDataValue> value;
    std::string path;

    [[nodiscard]] bool success() const noexcept {
        return status == MmsControlBuildStatus::ok && value.has_value();
    }
};

struct MmsControlSequenceContext final {
    ControlValue control_value{ControlValue::boolean(false)};
    OriginCategory origin_category{OriginCategory::station_control};
    std::vector<std::uint8_t> origin_identifier;
    std::uint8_t control_number{};
    std::optional<mms::MmsDataValue> timestamp;
    std::optional<mms::MmsDataValue> operate_at;
    bool test{};
    bool interlock_check{};
    bool synchro_check{};
};

// Hosted/live-control builder. It mirrors the C# oracle's conservative policy:
// consume the live MMS TypeSpecification, preserve its component order, and
// reject unknown/vendor-specific command fields rather than guessing them.
class MmsControlStructureBuilder final {
public:
    [[nodiscard]] static MmsControlBuildResult bind_control_value(
        const ControlValue& value,
        const mms::MmsTypeSpecification& specification);

    [[nodiscard]] static MmsControlBuildResult validate_value(
        const mms::MmsDataValue& value,
        const mms::MmsTypeSpecification& specification,
        std::string path = "ctlVal");

    [[nodiscard]] static MmsControlBuildResult build_operate(
        const MmsControlSequenceContext& context,
        const mms::MmsTypeSpecification& specification,
        bool require_exact_named_fields = true);

    [[nodiscard]] static MmsControlBuildResult build_select_with_value(
        const MmsControlSequenceContext& context,
        const mms::MmsTypeSpecification& specification,
        bool require_exact_named_fields = true);

    [[nodiscard]] static MmsControlBuildResult build_cancel(
        const MmsControlSequenceContext& context,
        const mms::MmsTypeSpecification& specification,
        bool require_exact_named_fields = true);

private:
    [[nodiscard]] static MmsControlBuildResult build_service(
        MmsControlService service,
        const MmsControlSequenceContext& context,
        const mms::MmsTypeSpecification& specification,
        bool require_exact_named_fields);
};

} // namespace ar::iec61850::control
