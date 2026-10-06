// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/profile_family.hpp"
#include "ariec61850/sampled_values/timing_semantics.hpp"
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
    // Profile family is never inferred from vendor, filename, svID or DataSet
    // shape. Unspecified remains inspectable but non-deployable.
    SvProfileFamily profile_family{SvProfileFamily::unspecified};

    // SCL describes sampling semantics but does not universally establish
    // every runtime sample-counter wrap policy. A standards/profile rule or
    // independently observed evidence may validate a modulus explicitly.
    std::optional<std::uint16_t> sample_counter_modulus;

    // SmpPerPeriod needs an explicit nominal-system-frequency context before
    // it can be resolved into samples/s and Ethernet frame cadence.
    std::optional<std::uint32_t> nominal_frequency_millihz;
};

struct SvPublisherProfile final {
    std::uint32_t schema_version{3U};
    std::string control_block_reference;
    std::string sv_id;
    std::string data_set_reference;

    std::array<std::uint8_t, 6> destination_mac{};
    std::uint16_t app_id{};
    SvTransportMode transport_mode{SvTransportMode::multicast};
    SvProfileFamily profile_family{SvProfileFamily::unspecified};
    SvProfileFamilyResolution profile_family_resolution{
        SvProfileFamilyResolution::unresolved};
    bool vlan_present{};
    std::uint16_t vlan_id{};
    std::uint8_t vlan_priority{};

    std::uint32_t configuration_revision{};
    SvPublicationTiming timing;
    SvSampleCounterPolicy sample_counter_policy{SvSampleCounterPolicy::unresolved};
    std::optional<std::uint16_t> sample_counter_modulus;
    SvAsduOptions asdu_options;

    std::vector<SvPublisherChannel> channels;
    std::size_t payload_size_bytes{};

    friend bool operator==(const SvPublisherProfile&, const SvPublisherProfile&) = default;
};

namespace detail {
[[nodiscard]] inline std::string profile_lower_copy(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

[[nodiscard]] inline bool profile_reference_matches_either(
    const std::string_view reference,
    const std::string_view canonical,
    const std::string_view legacy_9_2le) {
    return reference.find(canonical) != std::string_view::npos ||
           reference.find(legacy_9_2le) != std::string_view::npos;
}
} // namespace detail

// Shared semantic authority for the proven fixed 9-2LE-style 4I+4V layout.
// Device classifiers consume this instead of maintaining a second copy.
[[nodiscard]] inline bool legacy_9_2le_4i4v_layout_matches(
    const SvPublisherProfile& profile) {
    if (profile.payload_size_bytes != 64U || profile.channels.size() != 16U) {
        return false;
    }

    constexpr std::array<std::string_view, 8> expected_values{
        "tctr1.amp.instmag.i",
        "tctr2.amp.instmag.i",
        "tctr3.amp.instmag.i",
        "tctr4.amp.instmag.i",
        "tvtr1.vol.instmag.i",
        "tvtr2.vol.instmag.i",
        "tvtr3.vol.instmag.i",
        "tvtr4.vol.instmag.i",
    };
    constexpr std::array<std::string_view, 8> expected_values_9_2le{
        "tctr1.ampsv.instmag.i",
        "tctr2.ampsv.instmag.i",
        "tctr3.ampsv.instmag.i",
        "tctr4.ampsv.instmag.i",
        "tvtr1.volsv.instmag.i",
        "tvtr2.volsv.instmag.i",
        "tvtr3.volsv.instmag.i",
        "tvtr4.volsv.instmag.i",
    };
    constexpr std::array<std::string_view, 8> expected_qualities{
        "tctr1.amp.q",
        "tctr2.amp.q",
        "tctr3.amp.q",
        "tctr4.amp.q",
        "tvtr1.vol.q",
        "tvtr2.vol.q",
        "tvtr3.vol.q",
        "tvtr4.vol.q",
    };
    constexpr std::array<std::string_view, 8> expected_qualities_9_2le{
        "tctr1.ampsv.q",
        "tctr2.ampsv.q",
        "tctr3.ampsv.q",
        "tctr4.ampsv.q",
        "tvtr1.volsv.q",
        "tvtr2.volsv.q",
        "tvtr3.volsv.q",
        "tvtr4.volsv.q",
    };

    for (std::size_t i = 0U; i < profile.channels.size(); ++i) {
        const auto& channel = profile.channels[i];
        if (channel.wire_width_bytes != 4U) return false;
        if ((i % 2U) == 0U) {
            if (channel.is_quality ||
                detail::profile_lower_copy(channel.basic_type) != "int32") {
                return false;
            }
        } else if (!channel.is_quality) {
            return false;
        }
    }

    for (std::size_t signal = 0U; signal < expected_values.size(); ++signal) {
        const auto value_reference = detail::profile_lower_copy(
            profile.channels[signal * 2U].signal_reference);
        const auto quality_reference = detail::profile_lower_copy(
            profile.channels[signal * 2U + 1U].signal_reference);
        if (!detail::profile_reference_matches_either(
                value_reference,
                expected_values[signal],
                expected_values_9_2le[signal]) ||
            !detail::profile_reference_matches_either(
                quality_reference,
                expected_qualities[signal],
                expected_qualities_9_2le[signal])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool legacy_9_2le_timing_matches(
    const SvPublicationTiming& timing) noexcept {
    if (!timing.resolved()) return true;
    const auto sample_rate = timing.exact_sample_rate_hz();
    if (!sample_rate.has_value()) return false;
    const auto asdus = timing.asdus_per_frame;
    return (*sample_rate == 4000U && asdus == 1U) ||
           (*sample_rate == 4800U && asdus == 1U) ||
           (*sample_rate == 12800U && asdus == 8U) ||
           (*sample_rate == 15360U && asdus == 8U);
}

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
        profile.profile_family = context.profile_family;
        profile.transport_mode =
            stream.multicast ? SvTransportMode::multicast : SvTransportMode::unicast;
        if (!stream.multicast_valid) {
            result.errors.push_back(
                "SV SampledValueControl has an invalid explicit multicast attribute; "
                "transport mode must not be inferred from malformed engineering input.");
        }
        profile.data_set_reference = stream.data_set_reference;
        profile.configuration_revision = stream.configuration_revision;
        profile.timing = resolve_sv_publication_timing(
            parse_sample_mode(stream.sample_mode),
            stream.sample_rate,
            stream.no_asdu,
            context.nominal_frequency_millihz);
        profile.asdu_options = compile_options(stream.smv_options);

        if (!stream.address.destination_mac.has_value()) {
            result.errors.push_back("SV stream has no valid destination MAC address.");
        } else {
            profile.destination_mac = *stream.address.destination_mac;
            if (profile.transport_mode == SvTransportMode::multicast) {
                if (!is_iec_sv_multicast_mac(profile.destination_mac)) {
                    result.errors.push_back(
                        "Multicast SV destination MAC must be within "
                        "01-0C-CD-04-00-00..01-0C-CD-04-01-FF.");
                }
            } else if (!is_valid_sv_unicast_mac(profile.destination_mac)) {
                result.errors.push_back(
                    "Unicast SV destination MAC must be a nonzero unicast Ethernet address.");
            }
        }
        if (!stream.address.app_id.has_value()) {
            result.errors.push_back("SV stream has no valid APPID.");
        } else {
            profile.app_id = *stream.address.app_id;
            if (!is_valid_sv_app_id(profile.app_id)) {
                result.errors.push_back(
                    "SV APPID must be within the IEC 61850-9-2 allocation 0x4000..0x7FFF.");
            }
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
            if (!ethernet::is_valid_vlan_id(profile.vlan_id)) {
                result.errors.push_back("SV VLAN ID must be 0..4094; VID 4095 is reserved.");
            }
            if (!ethernet::is_valid_vlan_priority(profile.vlan_priority)) {
                result.errors.push_back("SV VLAN priority exceeds the 3-bit Ethernet PCP range.");
            }
        }

        if (profile.sv_id.empty()) {
            result.errors.push_back("SV stream has no svID/smvID.");
        }

        switch (profile.timing.resolution) {
        case SvTimingResolution::resolved:
            break;
        case SvTimingResolution::needs_nominal_frequency:
            result.warnings.push_back(
                "SmpPerPeriod requires an explicit nominal-system-frequency input before samples/s and Ethernet frame cadence are known.");
            break;
        case SvTimingResolution::invalid_sample_rate:
            result.errors.push_back("SV sample rate is zero or missing.");
            break;
        case SvTimingResolution::invalid_asdu_count:
            result.errors.push_back("SV nofASDU must be greater than zero.");
            break;
        case SvTimingResolution::unsupported_sample_mode:
            result.errors.push_back("SV sample mode is missing or unsupported.");
            break;
        case SvTimingResolution::invalid_nominal_frequency:
            result.errors.push_back("SV nominal system frequency must be greater than zero.");
            break;
        case SvTimingResolution::arithmetic_overflow:
            result.errors.push_back("SV timing semantics exceed the supported numeric range.");
            break;
        }

        if (context.sample_counter_modulus.has_value()) {
            if (*context.sample_counter_modulus == 0U) {
                result.errors.push_back("SV sample-counter modulus must be greater than zero.");
            } else {
                profile.sample_counter_policy = SvSampleCounterPolicy::explicit_modulus;
                profile.sample_counter_modulus = context.sample_counter_modulus;
            }
        } else if (
            profile.timing.sampling_basis == SvSampleMode::samples_per_second &&
            profile.timing.configured_sample_rate > 0U &&
            profile.timing.configured_sample_rate <= std::numeric_limits<std::uint16_t>::max()) {
            // Several second-aligned interoperability families use a one-second
            // sample-count cycle. Preserve that useful candidate for inspection,
            // but mark it non-authoritative: device deployment must validate the
            // counter policy from a profile rule or observed evidence first.
            profile.sample_counter_policy =
                SvSampleCounterPolicy::candidate_sample_rate_modulus;
            profile.sample_counter_modulus =
                static_cast<std::uint16_t>(profile.timing.configured_sample_rate);
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
        apply_profile_family_rules(profile, result);

        if (result.errors.empty()) {
            result.profile = std::move(profile);
        }
        return result;
    }

private:
    [[nodiscard]] static std::string lower_copy(const std::string_view text) {
        return detail::profile_lower_copy(text);
    }

    [[nodiscard]] static SvSampleMode parse_sample_mode(const std::string_view text) {
        const auto normalized = lower_copy(text);
        // IEC 61850-6 defines SmpPerPeriod as the SCL default when smpMod is
        // omitted. Keep that configured default explicit in the compiled model.
        if (normalized.empty() || normalized == "smpperperiod") {
            return SvSampleMode::samples_per_period;
        }
        if (normalized == "smppersec") {
            return SvSampleMode::samples_per_second;
        }
        // IEC 61850-6:2024 uses SecPerSample. Older SCL schema bindings and
        // deployed engineering tools also expose the legacy token SecPerSmp;
        // both carry the same sampling semantics and are accepted deliberately.
        if (normalized == "secpersample" || normalized == "secpersmp") {
            return SvSampleMode::seconds_per_sample;
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

    static void apply_profile_family_rules(
        SvPublisherProfile& profile,
        SvPublisherProfileCompileResult& result) {
        switch (profile.profile_family) {
        case SvProfileFamily::unspecified:
            profile.profile_family_resolution = SvProfileFamilyResolution::unresolved;
            result.warnings.push_back(
                "SV profile family is unspecified; select IEC 61850-9-2, "
                "legacy 9-2LE, or IEC 61869-9 explicitly before deployment.");
            return;

        case SvProfileFamily::iec61850_9_2:
            profile.profile_family_resolution = SvProfileFamilyResolution::resolved;
            return;

        case SvProfileFamily::legacy_9_2le: {
            bool valid = true;
            if (profile.transport_mode != SvTransportMode::multicast) {
                result.errors.push_back(
                    "Legacy 9-2LE compatibility target requires multicast Sampled Values.");
                valid = false;
            }
            if (profile.app_id != 0x4000U) {
                result.errors.push_back(
                    "Legacy 9-2LE compatibility target requires APPID 0x4000.");
                valid = false;
            }
            if (!legacy_9_2le_4i4v_layout_matches(profile)) {
                result.errors.push_back(
                    "Legacy 9-2LE compatibility target requires the fixed 4I+4V "
                    "INT32+Quality DataSet layout.");
                valid = false;
            }
            if (!legacy_9_2le_timing_matches(profile.timing)) {
                result.errors.push_back(
                    "Legacy 9-2LE compatibility target requires the supported "
                    "80/256 samples-per-cycle packetization families.");
                valid = false;
            }
            profile.profile_family_resolution = valid
                ? SvProfileFamilyResolution::resolved
                : SvProfileFamilyResolution::unresolved;
            if (valid) {
                result.warnings.push_back(
                    "Legacy 9-2LE compatibility rules are applied as an explicit "
                    "interoperability target; this is not a formal UCA conformance claim.");
            }
            return;
        }

        case SvProfileFamily::iec61869_9:
            // P1.1 deliberately stops at transport/address family identity.
            // Scaling, configurable variant constraints and full IEC 61869-9
            // dataset rules are a later authority and must not be guessed here.
            profile.profile_family_resolution = SvProfileFamilyResolution::incomplete;
            result.warnings.push_back(
                "IEC 61869-9 family selected: transport/address semantics are represented, "
                "but scaling and variant rules are not yet complete; deployment remains blocked.");
            return;
        }
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
