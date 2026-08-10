// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/scl/model.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::sampled_values {

enum class SvSampleMode : std::uint8_t {
    unknown,
    samples_per_period,
    samples_per_second,
};

enum class SvSampleCounterPolicy : std::uint8_t {
    unresolved,
    candidate_sample_rate_modulus,
    explicit_modulus,
};

struct SvAsduOptions final {
    bool element_present{};
    bool refresh_time{};
    bool sample_synchronized{};
    bool sample_rate{};
    bool data_set{};
    bool security{};
    bool synch_source_id{};

    friend bool operator==(const SvAsduOptions&, const SvAsduOptions&) = default;
};

struct SvPublisherChannel final {
    std::size_t index{};
    std::string signal_reference;
    std::string cdc;
    std::string basic_type;
    bool is_quality{};
    bool is_timestamp{};
    std::uint16_t wire_width_bytes{};

    friend bool operator==(const SvPublisherChannel&, const SvPublisherChannel&) = default;
};

struct SvPublisherProfileCompileContext final {
    // SCL describes sampling semantics but does not universally establish
    // every runtime sample-counter wrap policy. A standards/profile rule or
    // independently observed evidence may validate a modulus explicitly.
    std::optional<std::uint16_t> sample_counter_modulus;
};

struct SvPublisherProfile final {
    std::uint32_t schema_version{1U};
    std::string control_block_reference;
    std::string sv_id;
    std::string data_set_reference;

    std::array<std::uint8_t, 6> destination_mac{};
    std::uint16_t app_id{};
    bool vlan_present{};
    std::uint16_t vlan_id{};
    std::uint8_t vlan_priority{};

    std::uint32_t configuration_revision{};
    std::uint32_t sample_rate_value{};
    SvSampleMode sample_mode{SvSampleMode::unknown};
    std::optional<std::uint32_t> publisher_rate_hz;
    std::uint16_t no_asdu{1U};
    SvSampleCounterPolicy sample_counter_policy{SvSampleCounterPolicy::unresolved};
    std::optional<std::uint16_t> sample_counter_modulus;
    SvAsduOptions asdu_options;

    std::vector<SvPublisherChannel> channels;
    std::size_t payload_size_bytes{};

    friend bool operator==(const SvPublisherProfile&, const SvPublisherProfile&) = default;
};

struct SvPublisherProfileCompileResult final {
    std::optional<SvPublisherProfile> profile;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool ok() const noexcept {
        return profile.has_value() && errors.empty();
    }
};

class SvPublisherProfileCompiler final {
public:
    [[nodiscard]] static SvPublisherProfileCompileResult compile(
        const scl::SclSampledValuesStream& stream,
        const SvPublisherProfileCompileContext& context = {}) {
        SvPublisherProfileCompileResult result;
        SvPublisherProfile profile;

        profile.control_block_reference = stream.control_block_reference;
        profile.sv_id = stream.sv_id.empty() ? stream.smv_id : stream.sv_id;
        profile.data_set_reference = stream.data_set_reference;
        profile.configuration_revision = stream.configuration_revision;
        profile.sample_rate_value = stream.sample_rate;
        profile.sample_mode = parse_sample_mode(stream.sample_mode);
        profile.no_asdu = stream.no_asdu;
        profile.asdu_options = compile_options(stream.smv_options);

        if (!stream.address.destination_mac.has_value()) {
            result.errors.push_back("SV stream has no valid destination MAC address.");
        } else {
            profile.destination_mac = *stream.address.destination_mac;
        }
        if (!stream.address.app_id.has_value()) {
            result.errors.push_back("SV stream has no valid APPID.");
        } else {
            profile.app_id = *stream.address.app_id;
        }

        const bool has_vlan_id = stream.address.vlan_id.has_value();
        const bool has_vlan_priority = stream.address.vlan_priority.has_value();
        if (has_vlan_id != has_vlan_priority) {
            result.errors.push_back(
                "SV stream has an incomplete VLAN binding; VLAN ID and priority must either both be present or both be absent.");
        } else if (has_vlan_id) {
            profile.vlan_present = true;
            profile.vlan_id = *stream.address.vlan_id;
            profile.vlan_priority = *stream.address.vlan_priority;
            if (profile.vlan_id > 4095U) {
                result.errors.push_back("SV VLAN ID exceeds the 12-bit Ethernet VLAN range.");
            }
            if (profile.vlan_priority > 7U) {
                result.errors.push_back("SV VLAN priority exceeds the 3-bit Ethernet PCP range.");
            }
        }

        if (profile.sv_id.empty()) {
            result.errors.push_back("SV stream has no svID/smvID.");
        }
        if (profile.no_asdu == 0U) {
            result.errors.push_back("SV nofASDU must be greater than zero.");
        }
        if (profile.sample_rate_value == 0U) {
            result.errors.push_back("SV sample rate is zero or missing.");
        }

        if (profile.sample_mode == SvSampleMode::samples_per_second) {
            profile.publisher_rate_hz = profile.sample_rate_value;
        } else if (profile.sample_mode == SvSampleMode::samples_per_period) {
            result.warnings.push_back(
                "SmpPerPeriod requires a nominal-system-frequency input before an absolute publisher rate can be scheduled.");
        } else {
            result.errors.push_back("SV sample mode is missing or unsupported.");
        }

        if (context.sample_counter_modulus.has_value()) {
            if (*context.sample_counter_modulus == 0U) {
                result.errors.push_back("SV sample-counter modulus must be greater than zero.");
            } else {
                profile.sample_counter_policy = SvSampleCounterPolicy::explicit_modulus;
                profile.sample_counter_modulus = context.sample_counter_modulus;
            }
        } else if (
            profile.sample_mode == SvSampleMode::samples_per_second &&
            profile.sample_rate_value > 0U &&
            profile.sample_rate_value <= std::numeric_limits<std::uint16_t>::max()) {
            // Several second-aligned interoperability families use a one-second
            // sample-count cycle. Preserve that useful candidate for inspection,
            // but mark it non-authoritative: device deployment must validate the
            // counter policy from a profile rule or observed evidence first.
            profile.sample_counter_policy =
                SvSampleCounterPolicy::candidate_sample_rate_modulus;
            profile.sample_counter_modulus =
                static_cast<std::uint16_t>(profile.sample_rate_value);
            result.warnings.push_back(
                "SV sample-counter modulus equals the SmpPerSec rate only as an unvalidated candidate; confirm it from the applicable profile rule or observed evidence before deployment.");
        } else {
            result.warnings.push_back(
                "SV sample-counter wrap policy is unresolved; supply a validated profile rule or observed-evidence modulus before deployment.");
        }

        if (stream.entries.empty()) {
            result.errors.push_back("SV DataSet is empty or unresolved.");
        }

        std::size_t payload_size{};
        for (const auto& entry : stream.entries) {
            const auto width = wire_width(entry);
            if (!width.has_value()) {
                result.errors.push_back(
                    "Unsupported or unresolved SV leaf type for " + entry.signal_reference +
                    ": '" + entry.basic_type + "'.");
                continue;
            }
            if (payload_size > std::numeric_limits<std::size_t>::max() - *width) {
                result.errors.push_back("SV payload size overflow.");
                break;
            }
            payload_size += *width;
            profile.channels.push_back({
                entry.index,
                entry.signal_reference,
                entry.cdc,
                entry.basic_type,
                entry.is_quality,
                entry.is_timestamp,
                *width,
            });
        }
        profile.payload_size_bytes = payload_size;

        if (result.errors.empty()) {
            result.profile = std::move(profile);
        }
        return result;
    }

private:
    [[nodiscard]] static std::string lower_copy(const std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (const char ch : text) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch))));
        }
        return result;
    }

    [[nodiscard]] static SvSampleMode parse_sample_mode(const std::string_view text) {
        const auto normalized = lower_copy(text);
        if (normalized == "smppersec") {
            return SvSampleMode::samples_per_second;
        }
        if (normalized == "smpperperiod") {
            return SvSampleMode::samples_per_period;
        }
        return SvSampleMode::unknown;
    }

    [[nodiscard]] static std::optional<std::uint16_t> wire_width(
        const scl::SclDataSetEntry& entry) {
        if (entry.is_quality || lower_copy(entry.basic_type) == "quality") {
            return static_cast<std::uint16_t>(4U);
        }
        if (entry.is_timestamp || lower_copy(entry.basic_type) == "timestamp") {
            return static_cast<std::uint16_t>(8U);
        }

        const auto type = lower_copy(entry.basic_type);
        if (type == "boolean" || type == "int8" || type == "int8u") {
            return static_cast<std::uint16_t>(1U);
        }
        if (type == "int16" || type == "int16u") {
            return static_cast<std::uint16_t>(2U);
        }
        if (type == "int32" || type == "int32u" || type == "float32") {
            return static_cast<std::uint16_t>(4U);
        }
        if (type == "int64" || type == "int64u" || type == "float64") {
            return static_cast<std::uint16_t>(8U);
        }
        return std::nullopt;
    }

    [[nodiscard]] static SvAsduOptions compile_options(
        const scl::SclSmvOptions& options) {
        return {
            options.element_present,
            options.refresh_time,
            options.sample_synchronized,
            options.sample_rate,
            options.data_set,
            options.security,
            options.synch_source_id,
        };
    }
};

} // namespace ar::iec61850::sampled_values
