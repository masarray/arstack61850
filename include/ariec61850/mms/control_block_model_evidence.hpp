// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_model.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ar::iec61850::mms {
namespace control_block_model_detail {

[[nodiscard]] inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

[[nodiscard]] inline bool same(
    const std::string_view left,
    const std::string_view right) {
    return lower(std::string{left}) == lower(std::string{right});
}

[[nodiscard]] inline bool path_matches_leaf(
    const std::string_view path,
    const std::string_view leaf) {
    if (same(path, leaf)) {
        return true;
    }
    const auto lowered_path = lower(std::string{path});
    const auto lowered_leaf = lower(std::string{leaf});
    return lowered_path.size() > lowered_leaf.size() &&
           lowered_path.ends_with("." + lowered_leaf);
}

[[nodiscard]] inline std::string hex_bytes(
    const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

[[nodiscard]] inline std::string value_text(
    const MmsDataValue& value,
    const std::size_t depth = 0U) {
    if (depth > 8U) {
        return "<nested-value-limit>";
    }

    switch (value.kind()) {
    case MmsDataKind::boolean:
        return std::get<bool>(value.value()) ? "true" : "false";
    case MmsDataKind::integer:
        return std::to_string(std::get<std::int64_t>(value.value()));
    case MmsDataKind::unsigned_integer:
        return std::to_string(std::get<std::uint64_t>(value.value()));
    case MmsDataKind::floating_point: {
        std::ostringstream output;
        if (const auto* float_value = std::get_if<float>(&value.value())) {
            output << *float_value;
        } else if (const auto* double_value = std::get_if<double>(&value.value())) {
            output << *double_value;
        }
        return output.str();
    }
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        return std::get<std::string>(value.value());
    case MmsDataKind::utc_time: {
        const auto& utc = std::get<Iec61850UtcTime>(value.value());
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            utc.value.time_since_epoch()).count();
        return "utcMs=" + std::to_string(milliseconds) +
               ";quality=" + std::to_string(utc.quality);
    }
    case MmsDataKind::array:
    case MmsDataKind::structure: {
        std::ostringstream output;
        output << '[';
        for (std::size_t index = 0U; index < value.children().size(); ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << value_text(value.children()[index], depth + 1U);
        }
        output << ']';
        return output.str();
    }
    case MmsDataKind::bit_string:
    case MmsDataKind::octet_string:
    case MmsDataKind::binary_time:
    case MmsDataKind::bcd:
    case MmsDataKind::boolean_array:
    case MmsDataKind::object_id:
    case MmsDataKind::unknown:
        return "0x" + hex_bytes(value.raw_value());
    }
    return "<unsupported>";
}

[[nodiscard]] inline const MmsControlBlockAttributeReadEvidence* find_attribute(
    const MmsControlBlockReadResult& evidence,
    const std::initializer_list<std::string_view> names) {
    const auto found = std::find_if(
        evidence.attributes.begin(), evidence.attributes.end(),
        [&](const auto& attribute) {
            return std::any_of(names.begin(), names.end(), [&](const auto name) {
                return path_matches_leaf(attribute.attribute_path, name);
            });
        });
    return found == evidence.attributes.end() ? nullptr : &*found;
}

[[nodiscard]] inline std::string read_text(
    const MmsControlBlockReadResult& evidence,
    const std::initializer_list<std::string_view> names) {
    const auto* attribute = find_attribute(evidence, names);
    return attribute != nullptr && attribute->value
        ? value_text(*attribute->value)
        : std::string{};
}

[[nodiscard]] inline std::string normalize_data_set_reference(std::string value) {
    std::replace(value.begin(), value.end(), '$', '.');
    return value;
}

[[nodiscard]] inline bool is_address_attribute(const std::string_view path) {
    const auto value = lower(std::string{path});
    return value == "addr" || value.starts_with("addr.") ||
           value == "dstaddress" || value.starts_with("dstaddress.") ||
           path_matches_leaf(path, "APPID") ||
           path_matches_leaf(path, "MAC-Address");
}

inline void project_one(
    MmsLiveControlBlock& control,
    const MmsLiveDiscoveryResult& discovery) {
    if (!discovery.control_block_value_reads_requested) {
        return;
    }

    const auto evidence_it = std::find_if(
        discovery.control_block_reads.begin(),
        discovery.control_block_reads.end(),
        [&](const auto& evidence) {
            return same(evidence.candidate.reference, control.reference);
        });

    if (evidence_it == discovery.control_block_reads.end()) {
        control.discovery_status = "ValueNotReadWithinBound";
        control.message =
            control.kind + " was inventoried from live MMS names, but its values "
            "were not selected by the configured bounded deep-reader.";
        return;
    }

    const auto& evidence = *evidence_it;
    const auto successful = evidence.successful_attribute_count();
    if (evidence.complete()) {
        control.discovery_status = "ValueReadComplete";
    } else if (successful != 0U) {
        control.discovery_status = "ValueReadPartial";
    } else {
        control.discovery_status = "ValueReadFailed";
    }

    const auto* data_set = find_attribute(evidence, {"DatSet"});
    if (data_set == nullptr) {
        control.data_set_reference_status = "AttributeNotPresentInNameList";
    } else if (data_set->value) {
        control.data_set_reference = normalize_data_set_reference(
            value_text(*data_set->value));
        control.data_set_reference_status = "ValueRead";
    } else {
        control.data_set_reference_status = "ValueReadFailed";
    }

    control.control_id = read_text(evidence, {"GoID"});
    control.app_id = read_text(evidence, {"APPID", "AppID"});
    control.smv_id = read_text(evidence, {"SvID", "svID"});
    control.configuration_revision = read_text(evidence, {"ConfRev"});
    control.minimum_time_ms = read_text(evidence, {"MinTime"});
    control.maximum_time_ms = read_text(evidence, {"MaxTime"});
    control.sample_rate = read_text(evidence, {"SmpRate"});
    control.sample_mode = read_text(evidence, {"SmpMod"});
    control.number_of_asdu = read_text(
        evidence, {"noASDU", "NoASDU", "NumOfASDU"});

    std::size_t address_attributes{};
    std::size_t address_values{};
    for (const auto& attribute : evidence.attributes) {
        if (!is_address_attribute(attribute.attribute_path)) {
            continue;
        }
        ++address_attributes;
        if (attribute.success()) {
            ++address_values;
        }
    }
    if (address_attributes == 0U) {
        control.address_status = "NotDiscovered";
    } else if (address_values == address_attributes) {
        control.address_status = "MmsValuesRead";
    } else if (address_values != 0U) {
        control.address_status = "MmsValuesPartial";
    } else {
        control.address_status = "MmsValueReadFailed";
    }

    std::ostringstream message;
    message << control.kind << " deep read " << successful << '/'
            << evidence.attributes.size() << " attributes";
    if (!evidence.attributes.empty()) {
        message << ": ";
    }
    for (std::size_t index = 0U; index < evidence.attributes.size(); ++index) {
        if (index != 0U) {
            message << ", ";
        }
        const auto& attribute = evidence.attributes[index];
        message << attribute.attribute_path << '=';
        if (attribute.value) {
            message << value_text(*attribute.value);
        } else if (attribute.failure_code) {
            message << "<access-failure:" << *attribute.failure_code << '>';
        } else {
            message << "<not-returned>";
        }
    }
    if (!evidence.error.empty()) {
        message << "; error=" << evidence.error;
    }
    control.message = message.str();
}

inline void append_control_usage(
    MmsLiveDataSet& data_set,
    const std::vector<MmsLiveControlBlock>& controls,
    std::vector<std::string>& target) {
    for (const auto& control : controls) {
        if (!control.data_set_reference.empty() &&
            same(control.data_set_reference, data_set.reference)) {
            target.push_back(control.reference);
        }
    }
    std::sort(target.begin(), target.end(), [](const auto& left, const auto& right) {
        return lower(left) < lower(right);
    });
    target.erase(
        std::unique(target.begin(), target.end(), [](const auto& left, const auto& right) {
            return same(left, right);
        }),
        target.end());
}

} // namespace control_block_model_detail

// Runtime evidence overlay for the C#-compatible structural live model.
// This deliberately runs after MmsLiveModelBuilder::build: mutable control-block
// values are therefore visible in the model without changing its canonical
// structural fingerprint or pretending that C# already performs this deep read.
class MmsControlBlockRuntimeProjector final {
public:
    static void apply(
        const MmsLiveDiscoveryResult& discovery,
        MmsLiveModelDocument& model) {
        if (!discovery.control_block_value_reads_requested) {
            return;
        }

        const auto project = [&](auto& controls) {
            for (auto& control : controls) {
                control_block_model_detail::project_one(control, discovery);
            }
        };
        project(model.goose_control_blocks);
        project(model.sampled_value_control_blocks);
        project(model.setting_group_controls);
        project(model.log_controls);

        for (auto& data_set : model.data_sets) {
            control_block_model_detail::append_control_usage(
                data_set,
                model.goose_control_blocks,
                data_set.used_by_goose_controls);
            control_block_model_detail::append_control_usage(
                data_set,
                model.sampled_value_control_blocks,
                data_set.used_by_sampled_value_controls);
        }

        model.warnings.erase(
            std::remove_if(
                model.warnings.begin(), model.warnings.end(), [](const auto& warning) {
                    return warning.code == "CONTROL_BLOCK_VALUE_READ_PENDING";
                }),
            model.warnings.end());

        std::size_t complete{};
        std::size_t partial{};
        std::size_t failed{};
        std::size_t not_read{};
        const auto count_status = [&](const auto& controls) {
            for (const auto& control : controls) {
                if (control.discovery_status == "ValueReadComplete") {
                    ++complete;
                } else if (control.discovery_status == "ValueReadPartial") {
                    ++partial;
                } else if (control.discovery_status == "ValueReadFailed") {
                    ++failed;
                } else {
                    ++not_read;
                }
            }
        };
        count_status(model.goose_control_blocks);
        count_status(model.sampled_value_control_blocks);
        count_status(model.setting_group_controls);
        count_status(model.log_controls);

        if (partial != 0U) {
            model.warnings.push_back({
                "CONTROL_BLOCK_VALUE_READ_PARTIAL",
                {},
                std::to_string(partial) +
                    " control blocks returned only part of their exposed MMS attribute values."});
        }
        if (failed != 0U) {
            model.warnings.push_back({
                "CONTROL_BLOCK_VALUE_READ_FAILED",
                {},
                std::to_string(failed) +
                    " control blocks could not return any exposed MMS attribute values."});
        }
        if (not_read != 0U) {
            model.warnings.push_back({
                "CONTROL_BLOCK_VALUE_READ_BOUNDED",
                {},
                std::to_string(not_read) +
                    " inventoried control blocks were intentionally not read because of the configured deep-read bound."});
        }

        if (!model.summary.empty() && model.summary.back() == '.') {
            model.summary.pop_back();
        }
        model.summary +=
            ", CBValueComplete=" + std::to_string(complete) +
            ", CBValuePartial=" + std::to_string(partial) +
            ", CBValueFailed=" + std::to_string(failed) +
            ", CBValueNotRead=" + std::to_string(not_read) + ".";
    }
};

} // namespace ar::iec61850::mms
