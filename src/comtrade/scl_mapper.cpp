// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/scl_mapper.hpp"

#include "ariec61850/comtrade/channel_mapper.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>

namespace ar::iec61850::comtrade {
namespace {

std::string uppercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char raw : value) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(raw))));
    }
    return result;
}

bool contains_any(const std::string& text, const std::initializer_list<std::string_view> values) {
    return std::any_of(values.begin(), values.end(), [&text](const std::string_view value) {
        return text.find(value) != std::string::npos;
    });
}

std::string phase_from_entry(const scl::SclDataSetEntry& entry) {
    const auto text = uppercase(
        entry.signal_reference + " " + entry.do_name + " " + entry.da_name + " " + entry.ln_inst);
    if (contains_any(text, {"PHSA", "PHASEA", "L1", ".A.", "_A_"})) {
        return "a";
    }
    if (contains_any(text, {"PHSB", "PHASEB", "L2", ".B.", "_B_"})) {
        return "b";
    }
    if (contains_any(text, {"PHSC", "PHASEC", "L3", ".C.", "_C_"})) {
        return "c";
    }
    if (contains_any(text, {"PHSN", "NEUT", "RESID", "GROUND", "GND", ".N.", "_N_"})) {
        return "n";
    }

    if (!entry.ln_inst.empty() &&
        std::all_of(entry.ln_inst.begin(), entry.ln_inst.end(), [](const char value) {
            return std::isdigit(static_cast<unsigned char>(value)) != 0;
        })) {
        const auto last = entry.ln_inst.back();
        if (last == '1') {
            return "a";
        }
        if (last == '2') {
            return "b";
        }
        if (last == '3') {
            return "c";
        }
        if (last == '4') {
            return "n";
        }
    }
    return {};
}

std::string semantic_key(const scl::SclDataSetEntry& entry) {
    const auto text = uppercase(entry.signal_reference + " " + entry.ln_class + " " + entry.do_name);
    const bool current = contains_any(text, {"TCTR", "AMP", "CURRENT", "CURR"});
    const bool voltage = contains_any(text, {"TVTR", "VOLT", "VOLTAGE"});
    const auto phase = phase_from_entry(entry);
    if (phase.empty()) {
        return {};
    }
    if (current && !voltage) {
        return "I" + phase;
    }
    if (voltage && !current) {
        return "V" + phase;
    }
    return {};
}

} // namespace

bool SclMappingReport::complete() const noexcept {
    return std::all_of(bindings.begin(), bindings.end(), [](const SclChannelBinding& binding) {
        return binding.analog_channel_index.has_value();
    });
}

SclMappingReport SclMapper::map(
    const scl::SclSampledValuesStream& stream,
    const Configuration& configuration) {
    SclMappingReport report;
    const auto default_map = ChannelMapper::create_default_map(configuration.analog_channels);
    std::set<std::size_t> used_channels;

    std::vector<const scl::SclDataSetEntry*> analog_entries;
    analog_entries.reserve(stream.entries.size());
    for (const auto& entry : stream.entries) {
        if (!entry.is_quality && !entry.is_timestamp) {
            analog_entries.push_back(&entry);
        }
    }
    report.bindings.reserve(analog_entries.size());

    for (const auto* entry : analog_entries) {
        SclChannelBinding binding;
        binding.scl_entry_index = entry->index;
        binding.signal_reference = entry->signal_reference;
        binding.channel_key = semantic_key(*entry);
        if (!binding.channel_key.empty()) {
            const auto channel = default_map.find(binding.channel_key);
            if (channel != default_map.end() && !used_channels.contains(channel->second)) {
                binding.analog_channel_index = channel->second;
                binding.confidence = MappingConfidence::semantic;
                binding.reason = "Matched IEC 61850 quantity and phase to COMTRADE channel key " +
                                 binding.channel_key + ".";
                used_channels.insert(channel->second);
            }
        }
        report.bindings.push_back(std::move(binding));
    }

    std::size_t next_channel = 0U;
    for (auto& binding : report.bindings) {
        if (binding.analog_channel_index.has_value()) {
            continue;
        }
        while (next_channel < configuration.analog_channels.size() && used_channels.contains(next_channel)) {
            ++next_channel;
        }
        if (next_channel < configuration.analog_channels.size()) {
            binding.analog_channel_index = next_channel;
            binding.confidence = MappingConfidence::ordered_fallback;
            binding.reason = binding.channel_key.empty()
                                 ? "No unambiguous quantity/phase key; used next available COMTRADE analog channel."
                                 : "Semantic key " + binding.channel_key +
                                       " was unavailable; used next available COMTRADE analog channel.";
            used_channels.insert(next_channel);
            ++next_channel;
        } else {
            binding.confidence = MappingConfidence::none;
            binding.reason = "No unused COMTRADE analog channel remains.";
            report.warnings.push_back(
                "No COMTRADE analog channel is available for SCL signal '" +
                binding.signal_reference + "'.");
        }
    }

    if (configuration.analog_channels.size() > report.bindings.size()) {
        report.warnings.push_back(
            std::to_string(configuration.analog_channels.size() - report.bindings.size()) +
            " COMTRADE analog channel(s) remain unmapped.");
    }
    return report;
}

std::string to_string(const MappingConfidence confidence) {
    switch (confidence) {
    case MappingConfidence::none:
        return "none";
    case MappingConfidence::ordered_fallback:
        return "ordered-fallback";
    case MappingConfidence::semantic:
        return "semantic";
    }
    return "none";
}

} // namespace ar::iec61850::comtrade
