// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ar::iec61850::control {
namespace {

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

[[nodiscard]] mms::MmsObjectName object_name(
    const ControlObjectReference& object,
    const std::string_view functional_constraint,
    const std::string_view leaf) {
    std::array<char, 1'024U> item{};
    std::size_t bytes{};
    if (!build_control_item(object, functional_constraint, leaf, item, bytes) ||
        bytes == 0U || bytes > item.size()) {
        throw std::invalid_argument("Control MMS object name exceeds the bounded identifier buffer.");
    }
    return mms::MmsObjectName::domain_specific(
        std::string{object.domain}, std::string{item.data(), bytes});
}

[[nodiscard]] std::optional<std::int64_t> numeric_value(
    const mms::MmsDataValue& value) noexcept {
    if (value.kind() == mms::MmsDataKind::integer) {
        if (const auto* number = std::get_if<std::int64_t>(&value.value())) {
            return *number;
        }
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

[[nodiscard]] ControlModel decode_ctl_model(const mms::MmsDataValue& value) noexcept {
    const auto number = numeric_value(value);
    if (!number.has_value()) {
        return ControlModel::unknown;
    }
    switch (number.value()) {
    case 0: return ControlModel::status_only;
    case 1: return ControlModel::direct_normal;
    case 2: return ControlModel::select_before_operate_normal;
    case 3: return ControlModel::direct_enhanced;
    case 4: return ControlModel::select_before_operate_enhanced;
    default: return ControlModel::unknown;
    }
}

[[nodiscard]] const mms::MmsTypeSpecification* find_child(
    const mms::MmsTypeSpecification& parent,
    const std::string& name) noexcept {
    const auto target = normalize_name(name);
    for (const auto& child : parent.children) {
        if (normalize_name(child.name) == target) {
            return &child;
        }
    }
    return nullptr;
}

[[nodiscard]] bool specifications_compatible(
    const mms::MmsTypeSpecification& left,
    const mms::MmsTypeSpecification& right) noexcept {
    if (left.kind != right.kind || left.size != right.size ||
        left.exponent_width != right.exponent_width ||
        left.children.size() != right.children.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.children.size(); ++index) {
        if (!specifications_compatible(left.children[index], right.children[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool contains_name(
    const std::vector<std::string>& names,
    const std::string& item) {
    return std::any_of(names.begin(), names.end(), [&](const std::string& candidate) {
        if (candidate.size() != item.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < item.size(); ++index) {
            const auto left = static_cast<unsigned char>(candidate[index]);
            const auto right = static_cast<unsigned char>(item[index]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    });
}

[[nodiscard]] std::chrono::milliseconds read_timeout_or_fallback(
    ControlTransport& transport,
    const mms::MmsObjectName& object,
    const std::vector<std::string>& names,
    const std::chrono::milliseconds fallback,
    const std::stop_token stop_token) {
    if (!contains_name(names, object.item)) {
        return fallback;
    }
    const auto value = transport.read(object, stop_token);
    if (!value.has_value()) {
        return fallback;
    }
    const auto number = numeric_value(value.value());
    if (!number.has_value() || number.value() <= 0) {
        return fallback;
    }
    return std::chrono::milliseconds{number.value()};
}

[[nodiscard]] std::optional<mms::MmsObjectName> find_status_object(
    const ControlObjectReference& object,
    const std::vector<std::string>& names,
    std::string& functional_constraint) {
    std::string data_path{object.data_object_path};
    std::replace(data_path.begin(), data_path.end(), '.', '$');
    const std::string ln{object.logical_node};
    const std::array<std::pair<std::string, std::string>, 4U> candidates{{
        {ln + "$ST$" + data_path + "$stVal", "ST"},
        {ln + "$ST$" + data_path + "$posVal", "ST"},
        {ln + "$MX$" + data_path + "$mag$f", "MX"},
        {ln + "$MX$" + data_path + "$mag$i", "MX"},
    }};
    for (const auto& [item, fc] : candidates) {
        if (contains_name(names, item)) {
            functional_constraint = fc;
            return mms::MmsObjectName::domain_specific(
                std::string{object.domain}, item);
        }
    }
    functional_constraint.clear();
    return std::nullopt;
}

[[nodiscard]] std::string infer_cdc(const mms::MmsTypeSpecification& ctl_val) {
    switch (ctl_val.kind) {
    case mms::MmsTypeKind::boolean: return "SPC";
    case mms::MmsTypeKind::bit_string: return "DPC";
    case mms::MmsTypeKind::floating_point: return "APC";
    case mms::MmsTypeKind::integer:
    case mms::MmsTypeKind::unsigned_integer:
    case mms::MmsTypeKind::bcd:
        return "INC/ISC";
    case mms::MmsTypeKind::structure: {
        bool pos = false;
        bool trans = false;
        bool f = false;
        bool i = false;
        for (const auto& child : ctl_val.children) {
            const auto name = normalize_name(child.name);
            pos = pos || name == "posval" || name == "position";
            trans = trans || name == "transind" || name == "transient";
            f = f || name == "f";
            i = i || name == "i";
        }
        if (pos || trans) return "BSC";
        if (f || i) return "APC";
        return "vendor-specific";
    }
    default:
        return "vendor-specific";
    }
}

[[nodiscard]] bool has_named_child(
    const mms::MmsTypeSpecification& specification,
    const std::string& name) noexcept {
    return find_child(specification, name) != nullptr;
}

} // namespace

bool ControlObjectDescriptor::operationally_ready() const noexcept {
    if (!object.valid() || model == ControlModel::status_only ||
        model == ControlModel::unknown ||
        operate_specification.kind != mms::MmsTypeKind::structure ||
        ctl_val_specification.kind == mms::MmsTypeKind::unknown) {
        return false;
    }
    if (requires_select() && !cancel_specification.has_value()) {
        return false;
    }
    if (model == ControlModel::select_before_operate_enhanced &&
        !select_with_value_specification.has_value()) {
        return false;
    }
    if (enhanced() && !supports_command_termination) {
        return false;
    }
    return true;
}

ControlObjectDescriptor ControlDescriptorDiscovery::discover(
    ControlTransport& transport,
    const std::string& object_reference_text,
    const ControlDiscoveryOptions options,
    const std::stop_token stop_token) {
    if (!transport.associated()) {
        throw std::invalid_argument("IEC 61850 control discovery requires an active MMS association.");
    }
    if (options.default_sbo_timeout <= std::chrono::milliseconds::zero() ||
        options.default_operate_timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Control discovery fallback timeouts must be positive.");
    }

    ControlObjectReference object;
    if (!try_parse_control_object_reference(object_reference_text, object)) {
        throw std::invalid_argument(
            "Control object reference must use the controllable Data Object root LD/LN.DO.");
    }

    ControlObjectDescriptor descriptor;
    descriptor.object = object;

    const auto ctl_model_object = object_name(object, "CF", "ctlModel");
    const auto ctl_model_value = transport.read(ctl_model_object, stop_token);
    if (!ctl_model_value.has_value()) {
        throw std::runtime_error("Cannot discover ctlModel for the control object.");
    }
    descriptor.model = decode_ctl_model(ctl_model_value.value());
    descriptor.discovery_evidence.push_back(
        "ctlModel=" + std::to_string(static_cast<unsigned>(descriptor.model)));
    if (descriptor.model == ControlModel::status_only ||
        descriptor.model == ControlModel::unknown) {
        throw std::runtime_error("Control object is not command-ready according to ctlModel.");
    }

    const auto oper_object = object_name(object, "CO", "Oper");
    const auto oper_specification = transport.variable_specification(oper_object, stop_token);
    if (!oper_specification.has_value() ||
        oper_specification->kind != mms::MmsTypeKind::structure) {
        throw std::runtime_error("Cannot retrieve exact live Oper TypeSpecification.");
    }
    const auto* ctl_val = find_child(oper_specification.value(), "ctlVal");
    if (ctl_val == nullptr) {
        throw std::runtime_error(
            "Live Oper TypeSpecification has no named ctlVal field; positional guessing is refused.");
    }
    descriptor.operate_specification = oper_specification.value();
    descriptor.ctl_val_specification = *ctl_val;
    descriptor.discovery_evidence.push_back(
        "Oper=" + descriptor.operate_specification.signature());

    if (descriptor.model == ControlModel::select_before_operate_enhanced) {
        const auto sbow_object = object_name(object, "CO", "SBOw");
        const auto sbow = transport.variable_specification(sbow_object, stop_token);
        if (!sbow.has_value() || sbow->kind != mms::MmsTypeKind::structure) {
            throw std::runtime_error("SBO enhanced requires an exact live SBOw TypeSpecification.");
        }
        const auto* sbow_ctl_val = find_child(sbow.value(), "ctlVal");
        if (sbow_ctl_val == nullptr ||
            !specifications_compatible(descriptor.ctl_val_specification, *sbow_ctl_val)) {
            throw std::runtime_error(
                "Live ctlVal TypeSpecification differs between Oper and SBOw.");
        }
        descriptor.select_with_value_specification = sbow.value();
        descriptor.discovery_evidence.push_back("SBOw=" + sbow->signature());
    }

    const auto cancel_object = object_name(object, "CO", "Cancel");
    const auto cancel = transport.variable_specification(cancel_object, stop_token);
    if (cancel.has_value()) {
        descriptor.cancel_specification = cancel.value();
        descriptor.discovery_evidence.push_back("Cancel=" + cancel->signature());
    }

    const auto names = transport.domain_variable_names(std::string{object.domain}, stop_token);
    descriptor.sbo_timeout = read_timeout_or_fallback(
        transport,
        object_name(object, "CF", "sboTimeout"),
        names,
        options.default_sbo_timeout,
        stop_token);
    descriptor.operate_timeout = read_timeout_or_fallback(
        transport,
        object_name(object, "CF", "operTimeout"),
        names,
        options.default_operate_timeout,
        stop_token);
    descriptor.status_object = find_status_object(
        object, names, descriptor.status_functional_constraint);
    descriptor.supports_time_activated_operate =
        has_named_child(descriptor.operate_specification, "operTm");
    descriptor.supports_command_termination = model_is_enhanced(descriptor.model);
    descriptor.cdc = infer_cdc(descriptor.ctl_val_specification);

    if (!descriptor.operationally_ready()) {
        throw std::runtime_error(
            "Control descriptor is incomplete and cannot safely execute commands.");
    }
    return descriptor;
}

} // namespace ar::iec61850::control
