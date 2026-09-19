// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/control_block_read.hpp"
#include "ariec61850/mms/data_value.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

struct MmsSettingGroupState final {
    std::optional<std::uint64_t> numOfSG;
    std::optional<std::uint64_t> activeSG;
    std::optional<std::uint64_t> editSG;
    std::optional<bool> cnfEdit;
    ar::iec61850::mms::MmsDataKind activeKind{
        ar::iec61850::mms::MmsDataKind::unknown};
    bool activeAttributePresent{};
};

class MmsSettingGroupPolicy final {
public:
    using ReadResult = ar::iec61850::mms::MmsControlBlockReadResult;
    using Evidence = ar::iec61850::mms::MmsControlBlockAttributeReadEvidence;
    using DataValue = ar::iec61850::mms::MmsDataValue;
    using DataKind = ar::iec61850::mms::MmsDataKind;

    [[nodiscard]] static const Evidence* findAttribute(
        const ReadResult& result,
        const std::string_view name) noexcept {
        for (const auto& attribute : result.attributes) {
            if (matchesTail(attribute.attribute_path, name)) {
                return &attribute;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static MmsSettingGroupState inspect(
        const ReadResult& result) noexcept {
        MmsSettingGroupState state;
        if (const auto* attribute = findAttribute(result, "NumOfSG");
            attribute != nullptr && attribute->value) {
            state.numOfSG = unsignedValue(*attribute->value);
        }
        if (const auto* attribute = findAttribute(result, "ActSG");
            attribute != nullptr && attribute->value) {
            state.activeAttributePresent = true;
            state.activeKind = attribute->value->kind();
            state.activeSG = unsignedValue(*attribute->value);
        }
        if (const auto* attribute = findAttribute(result, "EditSG");
            attribute != nullptr && attribute->value) {
            state.editSG = unsignedValue(*attribute->value);
        }
        if (const auto* attribute = findAttribute(result, "CnfEdit");
            attribute != nullptr && attribute->value) {
            state.cnfEdit = boolValue(*attribute->value);
        }
        return state;
    }

    [[nodiscard]] static std::string activationBlockReason(
        const MmsSettingGroupState& state) {
        if (!state.numOfSG || *state.numOfSG == 0U) {
            return "NumOfSG is unavailable or zero.";
        }
        if (!state.activeAttributePresent || !state.activeSG) {
            return "ActSG is unavailable or unreadable.";
        }
        if (state.activeKind != DataKind::unsigned_integer &&
            state.activeKind != DataKind::integer) {
            return "ActSG is not an Integer/Unsigned MMS scalar.";
        }
        if (state.cnfEdit.value_or(false)) {
            return "CnfEdit is true; activation is blocked while an edit transaction is pending.";
        }
        if (state.editSG && *state.editSG != 0U) {
            return "EditSG is non-zero; activation is blocked while a setting group is being edited.";
        }
        return {};
    }

    static void validateActivation(
        const MmsSettingGroupState& state,
        const std::uint64_t requestedGroup) {
        const auto blocked = activationBlockReason(state);
        if (!blocked.empty()) {
            throw std::invalid_argument(blocked);
        }
        if (requestedGroup == 0U || requestedGroup > *state.numOfSG) {
            throw std::out_of_range(
                "Requested setting group is outside 1..NumOfSG.");
        }
    }

    [[nodiscard]] static DataValue activationValue(
        const MmsSettingGroupState& state,
        const std::uint64_t requestedGroup) {
        validateActivation(state, requestedGroup);
        if (state.activeKind == DataKind::integer) {
            return DataValue::integer(static_cast<std::int64_t>(requestedGroup));
        }
        return DataValue::unsigned_integer(requestedGroup);
    }

private:
    [[nodiscard]] static std::string lower(std::string value) noexcept {
        for (auto& character : value) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    }

    [[nodiscard]] static bool matchesTail(
        const std::string_view path,
        const std::string_view name) noexcept {
        const auto normalizedPath = lower(std::string{path});
        const auto normalizedName = lower(std::string{name});
        if (normalizedPath == normalizedName) return true;
        if (normalizedPath.size() <= normalizedName.size()) return false;
        const auto offset = normalizedPath.size() - normalizedName.size();
        return normalizedPath.compare(offset, normalizedName.size(), normalizedName) == 0 &&
               (normalizedPath[offset - 1U] == '.' || normalizedPath[offset - 1U] == '$');
    }

    [[nodiscard]] static std::optional<std::uint64_t> unsignedValue(
        const DataValue& value) noexcept {
        if (value.kind() == DataKind::unsigned_integer) {
            if (const auto* parsed = std::get_if<std::uint64_t>(&value.value())) {
                return *parsed;
            }
        }
        if (value.kind() == DataKind::integer) {
            if (const auto* parsed = std::get_if<std::int64_t>(&value.value());
                parsed != nullptr && *parsed >= 0) {
                return static_cast<std::uint64_t>(*parsed);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<bool> boolValue(
        const DataValue& value) noexcept {
        if (value.kind() != DataKind::boolean) return std::nullopt;
        if (const auto* parsed = std::get_if<bool>(&value.value())) {
            return *parsed;
        }
        return std::nullopt;
    }
};
