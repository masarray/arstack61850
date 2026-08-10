// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/mms_control_structure.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

namespace ar::iec61850::control {
namespace {

using mms::MmsDataKind;
using mms::MmsDataValue;
using mms::MmsTypeKind;
using mms::MmsTypeSpecification;

[[nodiscard]] MmsControlBuildResult failure(
    const MmsControlBuildStatus status,
    std::string path) {
    return {status, std::nullopt, std::move(path)};
}

[[nodiscard]] MmsControlBuildResult success(MmsDataValue value) {
    return {MmsControlBuildStatus::ok, std::move(value), {}};
}

[[nodiscard]] std::string normalize_name(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            result.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return result;
}

[[nodiscard]] bool numeric_kind(const MmsTypeKind kind) noexcept {
    return kind == MmsTypeKind::integer ||
        kind == MmsTypeKind::unsigned_integer ||
        kind == MmsTypeKind::floating_point ||
        kind == MmsTypeKind::bcd;
}

[[nodiscard]] bool compatible_kind(
    const MmsDataKind actual,
    const MmsTypeKind expected) noexcept {
    switch (expected) {
    case MmsTypeKind::array: return actual == MmsDataKind::array;
    case MmsTypeKind::structure: return actual == MmsDataKind::structure;
    case MmsTypeKind::boolean: return actual == MmsDataKind::boolean;
    case MmsTypeKind::bit_string: return actual == MmsDataKind::bit_string;
    case MmsTypeKind::integer: return actual == MmsDataKind::integer;
    case MmsTypeKind::unsigned_integer: return actual == MmsDataKind::unsigned_integer;
    case MmsTypeKind::floating_point: return actual == MmsDataKind::floating_point;
    case MmsTypeKind::octet_string: return actual == MmsDataKind::octet_string;
    case MmsTypeKind::visible_string: return actual == MmsDataKind::visible_string;
    case MmsTypeKind::binary_time: return actual == MmsDataKind::binary_time;
    case MmsTypeKind::bcd: return actual == MmsDataKind::integer;
    case MmsTypeKind::boolean_array: return actual == MmsDataKind::boolean_array;
    case MmsTypeKind::object_id: return actual == MmsDataKind::object_id;
    case MmsTypeKind::mms_string: return actual == MmsDataKind::mms_string;
    case MmsTypeKind::utc_time: return actual == MmsDataKind::utc_time;
    case MmsTypeKind::unknown: return false;
    }
    return false;
}

[[nodiscard]] MmsControlBuildResult validate_recursive(
    const MmsDataValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (!compatible_kind(value.kind(), specification.kind)) {
        return failure(MmsControlBuildStatus::type_mismatch, path);
    }

    if (specification.kind == MmsTypeKind::bit_string) {
        const auto& raw = value.raw_value();
        if (raw.size() <= 1U || raw.front() > 7U) {
            return failure(MmsControlBuildStatus::shape_mismatch, path);
        }
        const auto total_bits = (raw.size() - 1U) * 8U;
        const auto unused_bits = static_cast<std::size_t>(raw.front());
        if (unused_bits > total_bits) {
            return failure(MmsControlBuildStatus::shape_mismatch, path);
        }
        const auto actual_bits = total_bits - unused_bits;
        if (specification.size.has_value() && specification.size.value() != 0U &&
            actual_bits != static_cast<std::size_t>(specification.size.value())) {
            return failure(MmsControlBuildStatus::shape_mismatch, path);
        }
    }

    if (specification.kind == MmsTypeKind::octet_string &&
        specification.size.has_value() && specification.size.value() != 0U &&
        value.raw_value().size() > static_cast<std::size_t>(specification.size.value())) {
        return failure(MmsControlBuildStatus::shape_mismatch, path);
    }

    if ((specification.kind == MmsTypeKind::visible_string ||
         specification.kind == MmsTypeKind::mms_string) &&
        specification.size.has_value() && specification.size.value() != 0U) {
        const auto* text = std::get_if<std::string>(&value.value());
        if (text == nullptr ||
            text->size() > static_cast<std::size_t>(specification.size.value())) {
            return failure(MmsControlBuildStatus::shape_mismatch, path);
        }
    }

    if (specification.kind == MmsTypeKind::structure ||
        specification.kind == MmsTypeKind::array) {
        const auto& children = value.children();
        if (!specification.children.empty() && children.size() != specification.children.size()) {
            return failure(MmsControlBuildStatus::shape_mismatch, path);
        }
        const auto count = std::min(children.size(), specification.children.size());
        for (std::size_t index = 0U; index < count; ++index) {
            const auto child_name = specification.children[index].name.empty()
                ? "[" + std::to_string(index) + "]"
                : specification.children[index].name;
            auto child = validate_recursive(
                children[index], specification.children[index], path + "." + child_name);
            if (!child.success()) {
                return child;
            }
        }
    }

    return success(value);
}

[[nodiscard]] MmsControlBuildResult default_value(
    const MmsTypeSpecification& specification,
    const std::string& path);

[[nodiscard]] MmsControlBuildResult bind_value(
    const ControlValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path);

[[nodiscard]] MmsControlBuildResult zero_bit_string(
    const MmsTypeSpecification& specification,
    const std::string& path) {
    auto bit_count = specification.size.value_or(1U);
    if (bit_count == 0U || bit_count > 1024U) {
        bit_count = 1U;
    }
    const auto byte_count = static_cast<std::size_t>((bit_count + 7U) / 8U);
    std::vector<std::uint8_t> bytes(byte_count, 0U);
    const auto unused = static_cast<std::uint8_t>(byte_count * 8U - bit_count);
    auto result = MmsDataValue::bit_string(unused, bytes);
    return validate_recursive(result, specification, path);
}

[[nodiscard]] MmsControlBuildResult default_value(
    const MmsTypeSpecification& specification,
    const std::string& path) {
    switch (specification.kind) {
    case MmsTypeKind::boolean:
        return success(MmsDataValue::boolean(false));
    case MmsTypeKind::bit_string:
        return zero_bit_string(specification, path);
    case MmsTypeKind::integer:
    case MmsTypeKind::bcd:
        return success(MmsDataValue::integer(0));
    case MmsTypeKind::unsigned_integer:
        return success(MmsDataValue::unsigned_integer(0U));
    case MmsTypeKind::floating_point:
        return success(MmsDataValue::floating_point(0.0));
    case MmsTypeKind::octet_string:
        return success(MmsDataValue::octet_string({}));
    case MmsTypeKind::visible_string:
        return success(MmsDataValue::visible_string({}));
    case MmsTypeKind::mms_string:
        return success(MmsDataValue::mms_string({}));
    case MmsTypeKind::utc_time:
        return success(MmsDataValue::utc_time(mms::Iec61850UtcTime{}));
    case MmsTypeKind::binary_time: {
        const auto size = specification.size.value_or(6U) == 4U ? 4U : 6U;
        std::array<std::uint8_t, 6U> zero{};
        return success(MmsDataValue::binary_time(
            std::span<const std::uint8_t>{zero}.first(size)));
    }
    case MmsTypeKind::structure: {
        std::vector<MmsDataValue> children;
        children.reserve(specification.children.size());
        for (const auto& child_specification : specification.children) {
            auto child = default_value(
                child_specification,
                path + "." + child_specification.name);
            if (!child.success()) {
                return child;
            }
            children.push_back(std::move(child.value.value()));
        }
        return success(MmsDataValue::structure(std::move(children)));
    }
    default:
        return failure(MmsControlBuildStatus::unsupported_component, path);
    }
}

[[nodiscard]] MmsControlBuildResult bind_boolean(
    const ControlValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    bool result{};
    switch (value.kind()) {
    case ControlValueKind::boolean:
        result = std::get<bool>(value.value());
        break;
    case ControlValueKind::integer:
        result = std::get<std::int64_t>(value.value()) != 0;
        break;
    case ControlValueKind::unsigned_integer:
        result = std::get<std::uint64_t>(value.value()) != 0U;
        break;
    default:
        return failure(MmsControlBuildStatus::type_mismatch, path);
    }
    auto encoded = MmsDataValue::boolean(result);
    return validate_recursive(encoded, specification, path);
}

[[nodiscard]] bool try_signed(
    const ControlValue& value,
    std::int64_t& result) noexcept {
    result = 0;
    switch (value.kind()) {
    case ControlValueKind::integer:
        result = std::get<std::int64_t>(value.value());
        return true;
    case ControlValueKind::unsigned_integer: {
        const auto input = std::get<std::uint64_t>(value.value());
        if (input > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        result = static_cast<std::int64_t>(input);
        return true;
    }
    case ControlValueKind::double_point:
        result = static_cast<std::int64_t>(std::get<DoublePointValue>(value.value()));
        return true;
    case ControlValueKind::floating_point: {
        const auto input = std::get<double>(value.value());
        if (!std::isfinite(input) ||
            input < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            input > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        result = static_cast<std::int64_t>(input);
        return true;
    }
    default:
        return false;
    }
}

[[nodiscard]] bool try_unsigned(
    const ControlValue& value,
    std::uint64_t& result) noexcept {
    result = 0U;
    switch (value.kind()) {
    case ControlValueKind::unsigned_integer:
        result = std::get<std::uint64_t>(value.value());
        return true;
    case ControlValueKind::integer: {
        const auto input = std::get<std::int64_t>(value.value());
        if (input < 0) {
            return false;
        }
        result = static_cast<std::uint64_t>(input);
        return true;
    }
    case ControlValueKind::double_point:
        result = static_cast<std::uint64_t>(std::get<DoublePointValue>(value.value()));
        return true;
    default:
        return false;
    }
}

[[nodiscard]] MmsControlBuildResult bind_bit_string(
    const ControlValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    std::uint64_t numeric{};
    if (!try_unsigned(value, numeric)) {
        return failure(MmsControlBuildStatus::type_mismatch, path);
    }

    auto bit_count = specification.size.value_or(2U);
    if (bit_count == 0U || bit_count > 32U) {
        bit_count = 2U;
    }
    const auto limit = std::uint64_t{1U} << bit_count;
    if (numeric >= limit) {
        return failure(MmsControlBuildStatus::value_out_of_range, path);
    }

    const auto byte_count = static_cast<std::size_t>((bit_count + 7U) / 8U);
    std::vector<std::uint8_t> bytes(byte_count, 0U);
    for (std::uint32_t encoded_bit = 0U; encoded_bit < bit_count; ++encoded_bit) {
        const auto numeric_bit = bit_count - 1U - encoded_bit;
        if ((numeric & (std::uint64_t{1U} << numeric_bit)) != 0U) {
            const auto byte_index = static_cast<std::size_t>(encoded_bit / 8U);
            const auto bit_in_byte = encoded_bit % 8U;
            bytes[byte_index] = static_cast<std::uint8_t>(
                bytes[byte_index] | static_cast<std::uint8_t>(0x80U >> bit_in_byte));
        }
    }
    const auto unused = static_cast<std::uint8_t>(byte_count * 8U - bit_count);
    auto encoded = MmsDataValue::bit_string(unused, bytes);
    return validate_recursive(encoded, specification, path);
}

[[nodiscard]] MmsControlBuildResult bind_structure(
    const ControlValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (specification.children.empty()) {
        return failure(MmsControlBuildStatus::shape_mismatch, path);
    }

    if (value.kind() == ControlValueKind::step_position) {
        const auto step = std::get<StepPosition>(value.value());
        std::vector<MmsDataValue> children;
        children.reserve(specification.children.size());
        for (const auto& child_specification : specification.children) {
            const auto name = normalize_name(child_specification.name);
            MmsControlBuildResult child;
            if (name == "posval" || name == "position" || name == "i") {
                child = bind_value(
                    ControlValue::integer(step.position), child_specification,
                    path + "." + child_specification.name);
            } else if (name == "transind" || name == "transient") {
                child = bind_value(
                    ControlValue::boolean(step.transient), child_specification,
                    path + "." + child_specification.name);
            } else {
                return failure(
                    MmsControlBuildStatus::unsupported_component,
                    path + "." + child_specification.name);
            }
            if (!child.success()) {
                return child;
            }
            children.push_back(std::move(child.value.value()));
        }
        auto encoded = MmsDataValue::structure(std::move(children));
        return validate_recursive(encoded, specification, path);
    }

    const bool scalar_numeric = value.kind() == ControlValueKind::floating_point ||
        value.kind() == ControlValueKind::integer ||
        value.kind() == ControlValueKind::unsigned_integer;
    if (scalar_numeric) {
        std::vector<std::size_t> numeric_indices;
        for (std::size_t index = 0U; index < specification.children.size(); ++index) {
            if (numeric_kind(specification.children[index].kind)) {
                numeric_indices.push_back(index);
            }
        }

        std::optional<std::size_t> selected_index;
        if (numeric_indices.size() == 1U) {
            selected_index = numeric_indices.front();
        } else {
            const auto desired_name = value.kind() == ControlValueKind::floating_point ? "f" : "i";
            for (std::size_t index = 0U; index < specification.children.size(); ++index) {
                if (normalize_name(specification.children[index].name) == desired_name) {
                    selected_index = index;
                    break;
                }
            }
        }

        if (selected_index.has_value()) {
            std::vector<MmsDataValue> children;
            children.reserve(specification.children.size());
            for (std::size_t index = 0U; index < specification.children.size(); ++index) {
                MmsControlBuildResult child = index == selected_index.value()
                    ? bind_value(value, specification.children[index],
                        path + "." + specification.children[index].name)
                    : default_value(specification.children[index],
                        path + "." + specification.children[index].name);
                if (!child.success()) {
                    return child;
                }
                children.push_back(std::move(child.value.value()));
            }
            auto encoded = MmsDataValue::structure(std::move(children));
            return validate_recursive(encoded, specification, path);
        }
    }

    if (specification.children.size() == 1U) {
        auto child = bind_value(
            value, specification.children.front(),
            path + "." + specification.children.front().name);
        if (!child.success()) {
            return child;
        }
        std::vector<MmsDataValue> children;
        children.push_back(std::move(child.value.value()));
        auto encoded = MmsDataValue::structure(std::move(children));
        return validate_recursive(encoded, specification, path);
    }

    return failure(MmsControlBuildStatus::unsupported_control_value, path);
}

[[nodiscard]] MmsControlBuildResult bind_value(
    const ControlValue& value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (value.kind() == ControlValueKind::raw_mms) {
        return validate_recursive(
            std::get<MmsDataValue>(value.value()), specification, path);
    }

    switch (specification.kind) {
    case MmsTypeKind::boolean:
        return bind_boolean(value, specification, path);
    case MmsTypeKind::bit_string:
        return bind_bit_string(value, specification, path);
    case MmsTypeKind::integer:
    case MmsTypeKind::bcd: {
        std::int64_t numeric{};
        if (!try_signed(value, numeric)) {
            return failure(MmsControlBuildStatus::value_out_of_range, path);
        }
        auto encoded = MmsDataValue::integer(numeric);
        return validate_recursive(encoded, specification, path);
    }
    case MmsTypeKind::unsigned_integer: {
        std::uint64_t numeric{};
        if (!try_unsigned(value, numeric)) {
            return failure(MmsControlBuildStatus::value_out_of_range, path);
        }
        auto encoded = MmsDataValue::unsigned_integer(numeric);
        return validate_recursive(encoded, specification, path);
    }
    case MmsTypeKind::floating_point: {
        double numeric{};
        switch (value.kind()) {
        case ControlValueKind::floating_point:
            numeric = std::get<double>(value.value());
            break;
        case ControlValueKind::integer:
            numeric = static_cast<double>(std::get<std::int64_t>(value.value()));
            break;
        case ControlValueKind::unsigned_integer:
            numeric = static_cast<double>(std::get<std::uint64_t>(value.value()));
            break;
        default:
            return failure(MmsControlBuildStatus::type_mismatch, path);
        }
        auto encoded = MmsDataValue::floating_point(numeric);
        return validate_recursive(encoded, specification, path);
    }
    case MmsTypeKind::structure:
        return bind_structure(value, specification, path);
    default:
        return failure(MmsControlBuildStatus::unsupported_control_value, path);
    }
}

[[nodiscard]] MmsControlBuildResult build_integer_like(
    const std::int64_t value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (specification.kind == MmsTypeKind::integer || specification.kind == MmsTypeKind::bcd) {
        auto encoded = MmsDataValue::integer(value);
        return validate_recursive(encoded, specification, path);
    }
    if (specification.kind == MmsTypeKind::unsigned_integer) {
        if (value < 0) {
            return failure(MmsControlBuildStatus::value_out_of_range, path);
        }
        auto encoded = MmsDataValue::unsigned_integer(static_cast<std::uint64_t>(value));
        return validate_recursive(encoded, specification, path);
    }
    return failure(MmsControlBuildStatus::type_mismatch, path);
}

[[nodiscard]] MmsControlBuildResult build_origin(
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification) {
    if (specification.kind != MmsTypeKind::structure || specification.children.empty()) {
        return failure(MmsControlBuildStatus::type_mismatch, "origin");
    }

    std::vector<MmsDataValue> children;
    children.reserve(specification.children.size());
    for (const auto& child_specification : specification.children) {
        const auto name = normalize_name(child_specification.name);
        MmsControlBuildResult child;
        if (name == "orcat") {
            child = build_integer_like(
                static_cast<std::int64_t>(context.origin_category),
                child_specification,
                "origin.orCat");
        } else if (name == "orident") {
            if (child_specification.kind == MmsTypeKind::octet_string) {
                auto encoded = MmsDataValue::octet_string(context.origin_identifier);
                child = validate_recursive(encoded, child_specification, "origin.orIdent");
            } else if (child_specification.kind == MmsTypeKind::visible_string) {
                std::string text;
                text.reserve(context.origin_identifier.size());
                for (const auto byte : context.origin_identifier) {
                    text.push_back(byte <= 0x7FU ? static_cast<char>(byte) : '?');
                }
                auto encoded = MmsDataValue::visible_string(std::move(text));
                child = validate_recursive(encoded, child_specification, "origin.orIdent");
            } else {
                return failure(MmsControlBuildStatus::type_mismatch, "origin.orIdent");
            }
        } else {
            return failure(
                MmsControlBuildStatus::unsupported_component,
                "origin." + child_specification.name);
        }
        if (!child.success()) {
            return child;
        }
        children.push_back(std::move(child.value.value()));
    }

    auto encoded = MmsDataValue::structure(std::move(children));
    return validate_recursive(encoded, specification, "origin");
}

[[nodiscard]] MmsControlBuildResult build_boolean_field(
    const bool value,
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (specification.kind != MmsTypeKind::boolean) {
        return failure(MmsControlBuildStatus::type_mismatch, path);
    }
    auto encoded = MmsDataValue::boolean(value);
    return validate_recursive(encoded, specification, path);
}

[[nodiscard]] MmsControlBuildResult build_check(
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification) {
    if (specification.kind != MmsTypeKind::bit_string) {
        return failure(MmsControlBuildStatus::type_mismatch, "Check");
    }
    std::uint8_t bits = 0U;
    if (context.synchro_check) {
        bits = static_cast<std::uint8_t>(bits | 0x80U);
    }
    if (context.interlock_check) {
        bits = static_cast<std::uint8_t>(bits | 0x40U);
    }
    const std::array<std::uint8_t, 1U> data{bits};
    auto encoded = MmsDataValue::bit_string(6U, data);
    return validate_recursive(encoded, specification, "Check");
}

[[nodiscard]] MmsControlBuildResult build_default_timestamp(
    const MmsTypeSpecification& specification,
    const std::string& path) {
    if (specification.kind == MmsTypeKind::utc_time) {
        auto encoded = MmsDataValue::utc_time(mms::Iec61850UtcTime{});
        return validate_recursive(encoded, specification, path);
    }
    if (specification.kind == MmsTypeKind::binary_time) {
        const auto size = specification.size.value_or(6U) == 4U ? 4U : 6U;
        std::array<std::uint8_t, 6U> zero{};
        auto encoded = MmsDataValue::binary_time(
            std::span<const std::uint8_t>{zero}.first(size));
        return validate_recursive(encoded, specification, path);
    }
    return failure(MmsControlBuildStatus::type_mismatch, path);
}

[[nodiscard]] bool required_field(
    const MmsControlService service,
    const std::string& name) noexcept {
    if (name == "ctlval" || name == "origin" || name == "ctlnum" ||
        name == "t" || name == "test") {
        return true;
    }
    return service != MmsControlService::cancel && name == "check";
}

} // namespace

ControlValue ControlValue::boolean(const bool value) {
    return {ControlValueKind::boolean, value};
}

ControlValue ControlValue::double_point(const DoublePointValue value) {
    return {ControlValueKind::double_point, value};
}

ControlValue ControlValue::integer(const std::int64_t value) {
    return {ControlValueKind::integer, value};
}

ControlValue ControlValue::unsigned_integer(const std::uint64_t value) {
    return {ControlValueKind::unsigned_integer, value};
}

ControlValue ControlValue::floating_point(const double value) {
    return {ControlValueKind::floating_point, value};
}

ControlValue ControlValue::step_position(const StepPosition value) {
    return {ControlValueKind::step_position, value};
}

ControlValue ControlValue::raw_mms(MmsDataValue value) {
    return {ControlValueKind::raw_mms, std::move(value)};
}

MmsControlBuildResult MmsControlStructureBuilder::bind_control_value(
    const ControlValue& value,
    const MmsTypeSpecification& specification) {
    return bind_value(value, specification, "ctlVal");
}

MmsControlBuildResult MmsControlStructureBuilder::validate_value(
    const MmsDataValue& value,
    const MmsTypeSpecification& specification,
    std::string path) {
    return validate_recursive(value, specification, path);
}

MmsControlBuildResult MmsControlStructureBuilder::build_operate(
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification,
    const bool require_exact_named_fields) {
    return build_service(
        MmsControlService::operate, context, specification, require_exact_named_fields);
}

MmsControlBuildResult MmsControlStructureBuilder::build_select_with_value(
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification,
    const bool require_exact_named_fields) {
    return build_service(
        MmsControlService::select_with_value, context, specification,
        require_exact_named_fields);
}

MmsControlBuildResult MmsControlStructureBuilder::build_cancel(
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification,
    const bool require_exact_named_fields) {
    return build_service(
        MmsControlService::cancel, context, specification, require_exact_named_fields);
}

MmsControlBuildResult MmsControlStructureBuilder::build_service(
    const MmsControlService service,
    const MmsControlSequenceContext& context,
    const MmsTypeSpecification& specification,
    const bool require_exact_named_fields) {
    if (specification.kind != MmsTypeKind::structure || specification.children.empty()) {
        return failure(MmsControlBuildStatus::invalid_service_specification, "service");
    }

    std::array<bool, 6U> found{};
    const auto mark = [&](const std::string& name) {
        if (name == "ctlval") found[0] = true;
        else if (name == "origin") found[1] = true;
        else if (name == "ctlnum") found[2] = true;
        else if (name == "t") found[3] = true;
        else if (name == "test") found[4] = true;
        else if (name == "check") found[5] = true;
    };

    std::vector<MmsDataValue> children;
    children.reserve(specification.children.size());
    for (const auto& child_specification : specification.children) {
        const auto name = normalize_name(child_specification.name);
        MmsControlBuildResult child;
        if (name == "ctlval") {
            child = bind_value(context.control_value, child_specification, "ctlVal");
        } else if (name == "opertm" && service != MmsControlService::cancel) {
            child = context.operate_at.has_value()
                ? validate_recursive(context.operate_at.value(), child_specification, "operTm")
                : build_default_timestamp(child_specification, "operTm");
        } else if (name == "origin") {
            child = build_origin(context, child_specification);
        } else if (name == "ctlnum") {
            child = build_integer_like(
                static_cast<std::int64_t>(context.control_number),
                child_specification,
                "ctlNum");
        } else if (name == "t") {
            if (!context.timestamp.has_value()) {
                return failure(MmsControlBuildStatus::timestamp_missing, "T");
            }
            child = validate_recursive(context.timestamp.value(), child_specification, "T");
        } else if (name == "test") {
            child = build_boolean_field(context.test, child_specification, "Test");
        } else if (name == "check") {
            child = build_check(context, child_specification);
        } else {
            return failure(
                MmsControlBuildStatus::unsupported_component,
                child_specification.name);
        }

        if (!child.success()) {
            return child;
        }
        mark(name);
        children.push_back(std::move(child.value.value()));
    }

    if (require_exact_named_fields) {
        constexpr std::array<std::string_view, 6U> names{
            "ctlval", "origin", "ctlnum", "t", "test", "check"};
        for (std::size_t index = 0U; index < names.size(); ++index) {
            if (required_field(service, std::string{names[index]}) && !found[index]) {
                return failure(
                    MmsControlBuildStatus::missing_required_component,
                    std::string{names[index]});
            }
        }
    }

    auto encoded = MmsDataValue::structure(std::move(children));
    const auto service_name = service == MmsControlService::operate
        ? "Oper"
        : (service == MmsControlService::select_with_value ? "SBOw" : "Cancel");
    return validate_recursive(encoded, specification, service_name);
}

} // namespace ar::iec61850::control
