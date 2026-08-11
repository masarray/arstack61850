// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/scl/parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ar::iec61850::sampled_values::SvPublisherProfile;
using ar::iec61850::sampled_values::SvPublisherProfileCompileContext;
using ar::iec61850::sampled_values::SvPublisherProfileCompiler;
using ar::iec61850::sampled_values::SvSampleCounterPolicy;
using ar::iec61850::sampled_values::SvSampleMode;
using ar::iec61850::scl::SclDocument;
using ar::iec61850::scl::SclEdition;

std::string json_escape(std::string_view text) {
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

void quoted(std::ostream& out, std::string_view value) {
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
    default: return "unknown";
    }
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
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i != 0U) out << ':';
        out << std::setw(2) << static_cast<unsigned>(mac[i]);
    }
    return out.str();
}

std::string lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool current_device_layout_supported(const SvPublisherProfile& profile) {
    if (profile.no_asdu != 1U || profile.payload_size_bytes != 64U ||
        profile.channels.size() != 16U || !profile.publisher_rate_hz.has_value() ||
        *profile.publisher_rate_hz == 0U || *profile.publisher_rate_hz > 65535U ||
        profile.sample_counter_policy != SvSampleCounterPolicy::explicit_modulus ||
        !profile.sample_counter_modulus.has_value()) {
        return false;
    }
    if (profile.asdu_options.refresh_time || profile.asdu_options.security ||
        profile.asdu_options.synch_source_id) {
        return false;
    }
    for (std::size_t i = 0U; i < profile.channels.size(); ++i) {
        const auto& channel = profile.channels[i];
        if (channel.wire_width_bytes != 4U) return false;
        if ((i % 2U) == 0U) {
            if (channel.is_quality || lower_copy(channel.basic_type) != "int32") return false;
        } else if (!channel.is_quality) {
            return false;
        }
    }
    return true;
}

void emit_profile(std::ostream& out, const SvPublisherProfile& p) {
    out << '{';
    out << "\"schemaVersion\":" << p.schema_version << ',';
    out << "\"controlBlockReference\":"; quoted(out, p.control_block_reference); out << ',';
    out << "\"svID\":"; quoted(out, p.sv_id); out << ',';
    out << "\"dataSetReference\":"; quoted(out, p.data_set_reference); out << ',';
    out << "\"destinationMac\":"; quoted(out, mac_text(p.destination_mac)); out << ',';
    out << "\"appID\":" << p.app_id << ',';
    out << "\"vlanPresent\":" << (p.vlan_present ? "true" : "false") << ',';
    out << "\"vlanID\":" << p.vlan_id << ',';
    out << "\"vlanPriority\":" << static_cast<unsigned>(p.vlan_priority) << ',';
    out << "\"confRev\":" << p.configuration_revision << ',';
    out << "\"sampleRate\":" << p.sample_rate_value << ',';
    out << "\"sampleMode\":"; quoted(out, sample_mode_name(p.sample_mode)); out << ',';
    out << "\"publisherRateHz\":";
    if (p.publisher_rate_hz) out << *p.publisher_rate_hz; else out << "null";
    out << ',';
    out << "\"nofASDU\":" << p.no_asdu << ',';
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
    out << "\"channels\":[";
    for (std::size_t i = 0; i < p.channels.size(); ++i) {
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
    const std::optional<std::uint16_t> counter_modulus) {
    out << '{';
    out << "\"schemaVersion\":1,";
    out << "\"source\":"; quoted(out, document.source_name); out << ',';
    out << "\"edition\":"; quoted(out, edition_name(document.edition)); out << ',';
    out << "\"headerID\":"; quoted(out, document.header_id); out << ',';
    out << "\"warnings\":"; string_array(out, document.warnings); out << ',';
    out << "\"conflicts\":[";
    for (std::size_t i = 0; i < document.conflicts.size(); ++i) {
        if (i != 0U) out << ',';
        const auto& conflict = document.conflicts[i];
        out << '{';
        out << "\"kind\":"; quoted(out, conflict.kind); out << ',';
        out << "\"key\":"; quoted(out, conflict.key); out << ',';
        out << "\"description\":"; quoted(out, conflict.description);
        out << '}';
    }
    out << "],\"streams\":[";

    for (std::size_t index = 0; index < document.sampled_values_streams.size(); ++index) {
        if (index != 0U) out << ',';
        const auto& stream = document.sampled_values_streams[index];
        SvPublisherProfileCompileContext context;
        context.sample_counter_modulus = counter_modulus;
        const auto compiled = SvPublisherProfileCompiler::compile(stream, context);

        std::string compatibility = "C";
        std::string device_support = "blocked";
        if (compiled.ok()) {
            const auto& profile = *compiled.profile;
            compatibility = profile.sample_counter_policy == SvSampleCounterPolicy::explicit_modulus ? "A" : "B";
            if (current_device_layout_supported(profile)) device_support = "ready";
            else if (compatibility == "B") device_support = "needs-counter-confirmation";
            else device_support = "unsupported-layout";
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

std::optional<std::uint16_t> parse_u16(std::string_view text) {
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
        std::cerr << "usage: ariec61850_smv_profile_inspect <SCL-file> [--counter-modulus N]\n";
        return 2;
    }

    std::optional<std::uint16_t> counter_modulus;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--counter-modulus" && i + 1 < argc) {
            counter_modulus = parse_u16(argv[++i]);
            if (!counter_modulus) {
                std::cerr << "invalid counter modulus\n";
                return 2;
            }
        } else {
            std::cerr << "unknown argument\n";
            return 2;
        }
    }

    try {
        const auto document = ar::iec61850::scl::SclParser{}.load(std::filesystem::path{argv[1]});
        emit_document(std::cout, document, counter_modulus);
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"schemaVersion\":1,\"fatalError\":";
        quoted(std::cout, error.what());
        std::cout << "}\n";
        return 1;
    }
}
