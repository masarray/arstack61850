// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/esp32p4_profile_support.hpp"
#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/scl/parser.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {
using ar::iec61850::sampled_values::SvPublisherProfile;
using ar::iec61850::sampled_values::SvPublisherProfileCompileContext;
using ar::iec61850::sampled_values::SvPublisherProfileCompiler;
using ar::iec61850::sampled_values::SvProfileFamily;
using ar::iec61850::sampled_values::SvProfileFamilyResolution;
using ar::iec61850::sampled_values::SvSampleCounterPolicy;
using ar::iec61850::sampled_values::SvSampleMode;
using ar::iec61850::sampled_values::SvTimingResolution;
using ar::iec61850::sampled_values::classify_esp32p4_sv_profile;
using ar::iec61850::sampled_values::esp32p4_sv_profile_support_name;
using ar::iec61850::sampled_values::sv_profile_family_name;
using ar::iec61850::sampled_values::sv_profile_family_resolution_name;
using ar::iec61850::sampled_values::sv_transport_mode_name;
using ar::iec61850::scl::SclDocument;
using ar::iec61850::scl::SclEdition;

std::string json_escape(const std::string_view text) {
    std::ostringstream out;
    for (const unsigned char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20U) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(ch) << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

void quoted(std::ostream& out, const std::string_view value) {
    out << '"' << json_escape(value) << '"';
}

template <typename Range>
void string_array(std::ostream& out, const Range& values) {
    out << '[';
    bool first = true;
    for (const auto& value : values) {
        if (!first) out << ',';
        first = false;
        quoted(out, value);
    }
    out << ']';
}

std::string edition_name(const SclEdition edition) {
    switch (edition) {
    case SclEdition::edition1: return "1";
    case SclEdition::edition2: return "2";
    case SclEdition::edition21: return "2.1";
    default: return "unknown";
    }
}

std::string sample_mode_name(const SvSampleMode mode) {
    switch (mode) {
    case SvSampleMode::samples_per_second: return "SmpPerSec";
    case SvSampleMode::samples_per_period: return "SmpPerPeriod";
    case SvSampleMode::seconds_per_sample: return "SecPerSample";
    default: return "unknown";
    }
}

std::string timing_resolution_name(const SvTimingResolution resolution) {
    switch (resolution) {
    case SvTimingResolution::resolved: return "resolved";
    case SvTimingResolution::needs_nominal_frequency: return "needs-nominal-frequency";
    case SvTimingResolution::invalid_sample_rate: return "invalid-sample-rate";
    case SvTimingResolution::invalid_asdu_count: return "invalid-asdu-count";
    case SvTimingResolution::unsupported_sample_mode: return "unsupported-sample-mode";
    case SvTimingResolution::invalid_nominal_frequency: return "invalid-nominal-frequency";
    case SvTimingResolution::arithmetic_overflow: return "arithmetic-overflow";
    }
    return "unsupported-sample-mode";
}

std::string counter_policy_name(const SvSampleCounterPolicy policy) {
    switch (policy) {
    case SvSampleCounterPolicy::explicit_modulus: return "explicit";
    case SvSampleCounterPolicy::candidate_sample_rate_modulus: return "candidate-rate";
    default: return "unresolved";
    }
}

std::string mac_text(const std::array<std::uint8_t, 6>& mac) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0U; i < mac.size(); ++i) {
        if (i != 0U) out << ':';
        out << std::setw(2) << static_cast<unsigned>(mac[i]);
    }
    return out.str();
}

std::string iec61869_variant_class_name(
    const ar::iec61850::sampled_values::Iec61869_9VariantClass value) {
    using ar::iec61850::sampled_values::Iec61869_9VariantClass;
    return value == Iec61869_9VariantClass::preferred
        ? "preferred" : "backward-compatible";
}

std::string iec61869_quantity_name(
    const ar::iec61850::sampled_values::Iec61869_9Quantity value) {
    using ar::iec61850::sampled_values::Iec61869_9Quantity;
    return value == Iec61869_9Quantity::current ? "current" : "voltage";
}

std::string engineering_unit_name(
    const ar::iec61850::sampled_values::SvEngineeringUnit value) {
    using ar::iec61850::sampled_values::SvEngineeringUnit;
    return value == SvEngineeringUnit::ampere ? "A" : "V";
}

void emit_profile(std::ostream& out, const SvPublisherProfile& p) {
    out << '{';
    out << "\"schemaVersion\":" << p.schema_version << ',';
    out << "\"controlBlockReference\":"; quoted(out, p.control_block_reference); out << ',';
    out << "\"svID\":"; quoted(out, p.sv_id); out << ',';
    out << "\"dataSetReference\":"; quoted(out, p.data_set_reference); out << ',';
    out << "\"destinationMac\":"; quoted(out, mac_text(p.destination_mac)); out << ',';
    out << "\"appID\":" << p.app_id << ',';
    out << "\"profileFamily\":"; quoted(out, sv_profile_family_name(p.profile_family)); out << ',';
    out << "\"profileFamilyResolution\":";
    quoted(out, sv_profile_family_resolution_name(p.profile_family_resolution)); out << ',';
    out << "\"transportMode\":"; quoted(out, sv_transport_mode_name(p.transport_mode)); out << ',';
    out << "\"vlanPresent\":" << (p.vlan_present ? "true" : "false") << ',';
    out << "\"vlanID\":" << p.vlan_id << ',';
    out << "\"vlanPriority\":" << static_cast<unsigned>(p.vlan_priority) << ',';
    out << "\"confRev\":" << p.configuration_revision << ',';
    out << "\"sampleRate\":" << p.timing.configured_sample_rate << ',';
    out << "\"sampleMode\":"; quoted(out, sample_mode_name(p.timing.sampling_basis)); out << ',';
    out << "\"timingResolution\":"; quoted(out, timing_resolution_name(p.timing.resolution)); out << ',';
    out << "\"nominalFrequencyMilliHz\":";
    if (p.timing.nominal_frequency_millihz) out << *p.timing.nominal_frequency_millihz; else out << "null";
    out << ',';
    out << "\"sampleRatePerSecondNumerator\":";
    if (p.timing.samples_per_second) out << p.timing.samples_per_second->numerator; else out << "null";
    out << ',';
    out << "\"sampleRatePerSecondDenominator\":";
    if (p.timing.samples_per_second) out << p.timing.samples_per_second->denominator; else out << "null";
    out << ',';
    out << "\"frameRateNumerator\":";
    if (p.timing.frames_per_second) out << p.timing.frames_per_second->numerator; else out << "null";
    out << ',';
    out << "\"frameRateDenominator\":";
    if (p.timing.frames_per_second) out << p.timing.frames_per_second->denominator; else out << "null";
    out << ',';
    out << "\"sampleRateHz\":";
    if (const auto rate = p.timing.exact_sample_rate_hz()) out << *rate; else out << "null";
    out << ',';
    out << "\"frameRateHz\":";
    if (const auto rate = p.timing.exact_frame_rate_hz()) out << *rate; else out << "null";
    out << ',';
    out << "\"publisherRateHz\":";
    if (const auto rate = p.timing.exact_frame_rate_hz()) out << *rate; else out << "null";
    out << ',';
    out << "\"nofASDU\":" << p.timing.asdus_per_frame << ',';
    out << "\"counterPolicy\":"; quoted(out, counter_policy_name(p.sample_counter_policy)); out << ',';
    out << "\"counterModulus\":";
    if (p.sample_counter_modulus) out << *p.sample_counter_modulus; else out << "null";
    out << ',';
    out << "\"payloadBytes\":" << p.payload_size_bytes << ',';
    out << "\"asduOptions\":{";
    out << "\"elementPresent\":" << (p.asdu_options.element_present ? "true" : "false") << ',';
    out << "\"refreshTime\":" << (p.asdu_options.refresh_time ? "true" : "false") << ',';
    out << "\"sampleSynchronized\":" << (p.asdu_options.sample_synchronized ? "true" : "false") << ',';
    out << "\"sampleRate\":" << (p.asdu_options.sample_rate ? "true" : "false") << ',';
    out << "\"dataSet\":" << (p.asdu_options.data_set ? "true" : "false") << ',';
    out << "\"security\":" << (p.asdu_options.security ? "true" : "false") << ',';
    out << "\"synchSourceId\":" << (p.asdu_options.synch_source_id ? "true" : "false") << "},";
    out << "\"iec61869\":";
    if (!p.iec61869_9.has_value()) {
        out << "null";
    } else {
        const auto& semantic = *p.iec61869_9;
        out << '{';
        out << "\"standardBasis\":\"IEC 61869-9:2016\",";
        out << "\"variant\":";
        if (!semantic.variant.has_value()) {
            out << "null";
        } else {
            const auto& variant = *semantic.variant;
            out << '{';
            out << "\"code\":";
            quoted(out, ar::iec61850::sampled_values::iec61869_9_variant_code(variant));
            out << ',';
            out << "\"class\":";
            quoted(out, iec61869_variant_class_name(variant.variant_class));
            out << ',';
            out << "\"sampleRateHz\":" << variant.sample_rate_hz << ',';
            out << "\"asdusPerFrame\":" << variant.asdus_per_frame << ',';
            out << "\"currentQuantities\":" << variant.current_quantity_count << ',';
            out << "\"voltageQuantities\":" << variant.voltage_quantity_count;
            out << '}';
        }
        out << ",\"channels\":[";
        for (std::size_t index = 0U; index < semantic.channels.size(); ++index) {
            if (index != 0U) out << ',';
            const auto& binding = semantic.channels[index];
            out << '{';
            out << "\"measurementIndex\":" << binding.measurement_entry_index << ',';
            out << "\"qualityIndex\":" << binding.quality_entry_index << ',';
            out << "\"quantity\":";
            quoted(out, iec61869_quantity_name(binding.quantity));
            out << ',';
            out << "\"scaleNumerator\":" << binding.engineering_scale.numerator << ',';
            out << "\"scaleDenominator\":" << binding.engineering_scale.denominator << ',';
            out << "\"scaleUnit\":";
            quoted(out, engineering_unit_name(binding.engineering_scale.unit));
            out << '}';
        }
        out << "]}";
    }
    out << ',';
    out << "\"channels\":[";
    for (std::size_t i = 0U; i < p.channels.size(); ++i) {
        if (i != 0U) out << ',';
        const auto& channel = p.channels[i];
        out << '{';
        out << "\"index\":" << channel.index << ',';
        out << "\"signalReference\":"; quoted(out, channel.signal_reference); out << ',';
        out << "\"cdc\":"; quoted(out, channel.cdc); out << ',';
        out << "\"basicType\":"; quoted(out, channel.basic_type); out << ',';
        out << "\"quality\":" << (channel.is_quality ? "true" : "false") << ',';
        out << "\"timestamp\":" << (channel.is_timestamp ? "true" : "false") << ',';
        out << "\"wireWidth\":" << channel.wire_width_bytes;
        out << '}';
    }
    out << "]}";
}

void emit_document(
    std::ostream& out,
    const SclDocument& document,
    const SvProfileFamily default_profile_family,
    const std::map<std::size_t, SvProfileFamily>& stream_profile_families,
    const std::optional<std::uint16_t> counter_modulus,
    const std::optional<std::uint32_t> nominal_frequency_millihz) {
    out << '{';
    out << "\"schemaVersion\":4,";
    out << "\"source\":"; quoted(out, document.source_name); out << ',';
    out << "\"edition\":"; quoted(out, edition_name(document.edition)); out << ',';
    out << "\"headerID\":"; quoted(out, document.header_id); out << ',';
    out << "\"warnings\":"; string_array(out, document.warnings); out << ',';
    out << "\"conflicts\":[";
    for (std::size_t i = 0U; i < document.conflicts.size(); ++i) {
        if (i != 0U) out << ',';
        const auto& conflict = document.conflicts[i];
        out << '{';
        out << "\"kind\":"; quoted(out, conflict.kind); out << ',';
        out << "\"key\":"; quoted(out, conflict.key); out << ',';
        out << "\"description\":"; quoted(out, conflict.description);
        out << '}';
    }
    out << "],\"streams\":[";

    for (std::size_t index = 0U; index < document.sampled_values_streams.size(); ++index) {
        if (index != 0U) out << ',';
        const auto& stream = document.sampled_values_streams[index];
        SvPublisherProfileCompileContext context;
        const auto selected_family = stream_profile_families.find(index);
        context.profile_family = selected_family != stream_profile_families.end()
            ? selected_family->second
            : default_profile_family;
        context.sample_counter_modulus = counter_modulus;
        context.nominal_frequency_millihz = nominal_frequency_millihz;
        const auto compiled = SvPublisherProfileCompiler::compile(stream, context);

        std::string compatibility{"C"};
        std::string device_support{"blocked"};
        if (compiled.ok()) {
            const auto& profile = *compiled.profile;
            compatibility =
                profile.profile_family_resolution == SvProfileFamilyResolution::resolved &&
                profile.timing.resolved() &&
                profile.sample_counter_policy == SvSampleCounterPolicy::explicit_modulus
                    ? "A" : "B";
            device_support = std::string{
                esp32p4_sv_profile_support_name(classify_esp32p4_sv_profile(profile))};
        }

        out << '{';
        out << "\"index\":" << index << ',';
        out << "\"ied\":"; quoted(out, stream.ied_name); out << ',';
        out << "\"control\":"; quoted(out, stream.control_name); out << ',';
        out << "\"controlBlockReference\":"; quoted(out, stream.control_block_reference); out << ',';
        out << "\"compatibilityClass\":"; quoted(out, compatibility); out << ',';
        out << "\"deviceSupport\":"; quoted(out, device_support); out << ',';
        out << "\"errors\":"; string_array(out, compiled.errors); out << ',';
        out << "\"warnings\":"; string_array(out, compiled.warnings); out << ',';
        out << "\"profile\":";
        if (compiled.profile) emit_profile(out, *compiled.profile); else out << "null";
        out << '}';
    }
    out << "]}";
}

std::optional<SvProfileFamily> parse_profile_family(const std::string_view text) {
    if (text == "iec61850-9-2") return SvProfileFamily::iec61850_9_2;
    if (text == "9-2le") return SvProfileFamily::legacy_9_2le;
    if (text == "iec61869-9") return SvProfileFamily::iec61869_9;
    if (text == "unspecified") return SvProfileFamily::unspecified;
    return std::nullopt;
}

std::optional<std::pair<std::size_t, SvProfileFamily>> parse_stream_profile_family(
    const std::string_view text) {
    const auto separator = text.find(':');
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U >= text.size()) {
        return std::nullopt;
    }
    try {
        const auto index_value = std::stoull(std::string{text.substr(0U, separator)});
        if (index_value > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        const auto family = parse_profile_family(text.substr(separator + 1U));
        if (!family.has_value() || *family == SvProfileFamily::unspecified) {
            return std::nullopt;
        }
        return std::pair<std::size_t, SvProfileFamily>{
            static_cast<std::size_t>(index_value), *family};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> parse_u32_nonzero(const std::string_view text) {
    try {
        const auto value = std::stoull(std::string{text});
        if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        return static_cast<std::uint32_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint16_t> parse_u16(const std::string_view text) {
    try {
        const auto value = std::stoul(std::string{text});
        if (value == 0U || value > 65535U) return std::nullopt;
        return static_cast<std::uint16_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ariec61850_smv_profile_inspect <SCL-file> [--profile-family FAMILY] [--stream-profile-family INDEX:FAMILY] [--counter-modulus N] [--nominal-frequency-hz N]\n";
        return 2;
    }

    SvProfileFamily profile_family = SvProfileFamily::unspecified;
    std::map<std::size_t, SvProfileFamily> stream_profile_families;
    std::optional<std::uint16_t> counter_modulus;
    std::optional<std::uint32_t> nominal_frequency_millihz;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--profile-family" && i + 1 < argc) {
            const auto parsed = parse_profile_family(argv[++i]);
            if (!parsed.has_value() || *parsed == SvProfileFamily::unspecified) {
                std::cerr << "invalid profile family\n";
                return 2;
            }
            profile_family = *parsed;
        } else if (std::string_view{argv[i]} == "--stream-profile-family" && i + 1 < argc) {
            const auto parsed = parse_stream_profile_family(argv[++i]);
            if (!parsed.has_value()) {
                std::cerr << "invalid stream profile family\n";
                return 2;
            }
            stream_profile_families[parsed->first] = parsed->second;
        } else if (std::string_view{argv[i]} == "--counter-modulus" && i + 1 < argc) {
            counter_modulus = parse_u16(argv[++i]);
            if (!counter_modulus) {
                std::cerr << "invalid counter modulus\n";
                return 2;
            }
        } else if (std::string_view{argv[i]} == "--nominal-frequency-hz" && i + 1 < argc) {
            const auto frequency_hz = parse_u32_nonzero(argv[++i]);
            if (!frequency_hz || *frequency_hz > std::numeric_limits<std::uint32_t>::max() / 1000U) {
                std::cerr << "invalid nominal frequency\n";
                return 2;
            }
            nominal_frequency_millihz = *frequency_hz * 1000U;
        } else {
            std::cerr << "unknown argument\n";
            return 2;
        }
    }

    try {
        const auto document = ar::iec61850::scl::SclParser{}.load(
            std::filesystem::path{argv[1]});
        for (const auto& [index, family] : stream_profile_families) {
            (void)family;
            if (index >= document.sampled_values_streams.size()) {
                std::cerr << "stream profile family index out of range\n";
                return 2;
            }
        }
        emit_document(
            std::cout, document, profile_family, stream_profile_families,
            counter_modulus, nominal_frequency_millihz);
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"schemaVersion\":4,\"fatalError\":";
        quoted(std::cout, error.what());
        std::cout << "}\n";
        return 1;
    }
}
