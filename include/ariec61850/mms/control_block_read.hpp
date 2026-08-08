// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsControlBlockKind : std::uint8_t {
    goose,
    sampled_value,
    setting_group,
    log,
};

[[nodiscard]] inline std::string_view mms_control_block_kind_name(
    const MmsControlBlockKind kind) noexcept {
    switch (kind) {
    case MmsControlBlockKind::goose: return "GSEControl";
    case MmsControlBlockKind::sampled_value: return "SampledValueControl";
    case MmsControlBlockKind::setting_group: return "SettingGroupControl";
    case MmsControlBlockKind::log: return "LogControl";
    }
    return "UnknownControl";
}

struct MmsControlBlockAttributeCandidate final {
    std::string attribute_path;
    MmsObjectName variable;

    friend bool operator==(
        const MmsControlBlockAttributeCandidate&,
        const MmsControlBlockAttributeCandidate&) = default;
};

struct MmsControlBlockCandidate final {
    MmsControlBlockKind kind{MmsControlBlockKind::goose};
    std::string domain;
    std::string logical_node;
    std::string functional_constraint;
    std::string name;
    std::string reference;
    std::vector<MmsControlBlockAttributeCandidate> attributes;
};

class MmsControlBlockInventoryBuilder final {
public:
    [[nodiscard]] static std::vector<MmsControlBlockCandidate> build(
        const MmsDiscoverySnapshot& snapshot) {
        std::vector<MmsControlBlockCandidate> result;
        for (const auto& [domain, variables] : snapshot.domain_variables) {
            for (const auto& item : variables) {
                const auto parts = split(item, '$');
                if (parts.size() < 4U || parts[0].empty() || parts[1].empty() ||
                    parts[2].empty() || parts[3].empty()) {
                    continue;
                }

                const auto kind = classify(parts[1], parts[2]);
                if (!kind) {
                    continue;
                }

                const auto attribute_path = join_attribute_path(parts, 3U);
                const auto existing = std::find_if(
                    result.begin(), result.end(), [&](const auto& candidate) {
                        return same(candidate.domain, domain) &&
                               same(candidate.logical_node, parts[0]) &&
                               same(candidate.functional_constraint, parts[1]) &&
                               same(candidate.name, parts[2]);
                    });

                MmsControlBlockCandidate* candidate{};
                if (existing == result.end()) {
                    MmsControlBlockCandidate created;
                    created.kind = *kind;
                    created.domain = domain;
                    created.logical_node = parts[0];
                    created.functional_constraint = parts[1];
                    created.name = parts[2];
                    created.reference = domain + "/" + parts[0] + "." +
                        parts[1] + "." + parts[2];
                    result.push_back(std::move(created));
                    candidate = &result.back();
                } else {
                    candidate = &*existing;
                }

                const auto duplicate = std::any_of(
                    candidate->attributes.begin(), candidate->attributes.end(),
                    [&](const auto& attribute) {
                        return same(attribute.variable.domain, domain) &&
                               same(attribute.variable.item, item);
                    });
                if (!duplicate) {
                    candidate->attributes.push_back({
                        attribute_path,
                        MmsObjectName::domain_specific(domain, item)});
                }
            }
        }

        for (auto& candidate : result) {
            std::sort(
                candidate.attributes.begin(), candidate.attributes.end(),
                [](const auto& left, const auto& right) {
                    return lower(left.attribute_path) < lower(right.attribute_path);
                });
        }
        std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            return lower(left.reference) < lower(right.reference);
        });
        return result;
    }

private:
    [[nodiscard]] static std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        return value;
    }

    [[nodiscard]] static bool same(
        const std::string_view left,
        const std::string_view right) {
        return lower(std::string{left}) == lower(std::string{right});
    }

    [[nodiscard]] static std::vector<std::string> split(
        const std::string_view value,
        const char separator) {
        std::vector<std::string> result;
        std::size_t start{};
        while (start <= value.size()) {
            const auto end = value.find(separator, start);
            result.emplace_back(value.substr(
                start,
                end == std::string_view::npos
                    ? std::string_view::npos
                    : end - start));
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1U;
        }
        return result;
    }

    [[nodiscard]] static std::string join_attribute_path(
        const std::vector<std::string>& parts,
        const std::size_t first) {
        std::string result;
        for (std::size_t index = first; index < parts.size(); ++index) {
            if (!result.empty()) {
                result += '.';
            }
            result += parts[index];
        }
        return result;
    }

    [[nodiscard]] static std::optional<MmsControlBlockKind> classify(
        const std::string_view functional_constraint,
        const std::string_view name) {
        const auto fc = lower(std::string{functional_constraint});
        if (fc == "go") {
            return MmsControlBlockKind::goose;
        }
        if (fc == "ms" || fc == "us") {
            return MmsControlBlockKind::sampled_value;
        }
        if (fc == "sg" || fc == "se") {
            return MmsControlBlockKind::setting_group;
        }
        if (fc == "sp" && same(name, "SGCB")) {
            return MmsControlBlockKind::setting_group;
        }
        if (fc == "lg") {
            return MmsControlBlockKind::log;
        }
        return std::nullopt;
    }
};

struct MmsControlBlockAttributeReadEvidence final {
    std::string attribute_path;
    MmsObjectName variable;
    std::optional<MmsDataValue> value;
    std::optional<std::uint32_t> failure_code;

    [[nodiscard]] bool success() const noexcept { return value.has_value(); }
};

struct MmsControlBlockReadResult final {
    MmsControlBlockCandidate candidate;
    std::vector<MmsControlBlockAttributeReadEvidence> attributes;
    std::string error;

    [[nodiscard]] std::size_t successful_attribute_count() const noexcept {
        return static_cast<std::size_t>(std::count_if(
            attributes.begin(), attributes.end(),
            [](const auto& attribute) { return attribute.success(); }));
    }

    [[nodiscard]] bool exchange_success() const noexcept {
        return error.empty();
    }

    [[nodiscard]] bool complete() const noexcept {
        return exchange_success() && !attributes.empty() &&
               successful_attribute_count() == attributes.size();
    }
};

class MmsControlBlockReadError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

// Generic read-only reader for GO/MS/US/SG/SE/SP(SGCB)/LG control blocks.
// Candidates come from exact GetNameList item names; the reader sends only MMS
// Read requests for those exact discovered variables. It never enables or
// writes a control block and never invents SCL-only APPID/MAC/VLAN evidence.
class MmsControlBlockReadClient final {
public:
    explicit MmsControlBlockReadClient(MmsAssociationRuntime& association)
        : association_{association} {}

    [[nodiscard]] MmsControlBlockReadResult read(
        const MmsControlBlockCandidate& candidate,
        const std::size_t maximum_attributes = 64U,
        const std::stop_token stop_token = {}) {
        if (!association_.associated()) {
            throw MmsControlBlockReadError(
                "Control-block read requires an active MMS association.");
        }
        if (maximum_attributes == 0U) {
            throw std::invalid_argument(
                "Control-block attribute read bound must be positive.");
        }

        MmsControlBlockReadResult result;
        result.candidate = candidate;
        const auto count = std::min(maximum_attributes, candidate.attributes.size());
        if (count == 0U) {
            result.error = "Control block has no exact MMS attributes to read.";
            return result;
        }

        MmsReadRequest request;
        request.invoke_id = association_.next_invoke_id();
        request.variables.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            request.variables.push_back(candidate.attributes[index].variable);
        }

        const auto encoded = MmsServiceCodec::encode_read_request_p_data(
            request, association_.negotiated().presentation_context_id);
        const auto exchange = association_.exchange_confirmed(
            encoded, request.invoke_id, stop_token);
        if (exchange.envelope.kind != MmsPduKind::confirmed_response) {
            result.error = "Control-block Read returned non-confirmed-response MMS PDU.";
            return result;
        }

        std::span<const std::uint8_t> payload;
        if (!exchange.presentation_payload.empty()) {
            payload = exchange.presentation_payload;
        } else if (!exchange.envelope.mms_payload.empty()) {
            payload = exchange.envelope.mms_payload;
        } else {
            result.error = "Control-block Read returned no decodable MMS payload.";
            return result;
        }

        const auto response = MmsServiceCodec::decode_read_response(
            payload, request.invoke_id);
        result.attributes.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            MmsControlBlockAttributeReadEvidence evidence;
            evidence.attribute_path = candidate.attributes[index].attribute_path;
            evidence.variable = candidate.attributes[index].variable;
            if (index < response.results.size()) {
                evidence.value = response.results[index].value;
                evidence.failure_code = response.results[index].failure_code;
            }
            result.attributes.push_back(std::move(evidence));
        }
        if (response.results.size() != count) {
            result.error = "Control-block Read result count did not match request count.";
        }
        return result;
    }

private:
    MmsAssociationRuntime& association_;
};

} // namespace ar::iec61850::mms
