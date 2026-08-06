// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/channel_mapper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace ar::iec61850::comtrade {
namespace {

std::string normalize(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool previous_space = true;
    for (const char raw : value) {
        const auto value_byte = static_cast<unsigned char>(raw);
        const char upper = static_cast<char>(std::toupper(value_byte));
        const bool separator = upper == '/' || upper == '\\' || upper == '-' || upper == '_' ||
                               std::isspace(static_cast<unsigned char>(upper)) != 0;
        if (separator) {
            if (!previous_space) {
                result.push_back(' ');
                previous_space = true;
            }
        } else {
            result.push_back(upper);
            previous_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

bool contains_token(const std::string& text, const std::string_view token) {
    if (text == token) {
        return true;
    }
    std::size_t start = 0U;
    while (start < text.size()) {
        const auto end = text.find(' ', start);
        const auto length = (end == std::string::npos ? text.size() : end) - start;
        if (std::string_view{text}.substr(start, length) == token) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return false;
}

bool contains_any(const std::string& text, const std::initializer_list<std::string_view> tokens) {
    return std::any_of(tokens.begin(), tokens.end(), [&text](const std::string_view token) {
        return token.size() == 1U ? contains_token(text, token)
                                  : contains_token(text, token) || text.find(token) != std::string::npos;
    });
}

std::string phase_suffix(const std::string& text, const std::string& phase) {
    const auto combined = text + " " + phase;
    if (contains_any(combined, {"NEUTRAL", "N", "GND", "GROUND", "RES", "RESIDUAL", "E"})) {
        return "n";
    }
    if (contains_any(combined, {"PHASE A", "PHASEA", "A", "R", "L1", "AN"})) {
        return "a";
    }
    if (contains_any(combined, {"PHASE B", "PHASEB", "B", "S", "L2", "BN"})) {
        return "b";
    }
    if (contains_any(combined, {"PHASE C", "PHASEC", "C", "T", "L3", "CN"})) {
        return "c";
    }
    return {};
}

std::string resolve_key(const AnalogChannel& channel) {
    const auto text = normalize(channel.name + " " + channel.phase + " " + channel.circuit_component);
    const auto unit = normalize(channel.unit);
    const bool voltage = unit.find('V') != std::string::npos ||
                         (!text.empty() && text.front() == 'V') ||
                         text.find("VOLT") != std::string::npos ||
                         text.find("TVTR") != std::string::npos;
    const bool current = unit.find('A') != std::string::npos ||
                         (!text.empty() && text.front() == 'I') ||
                         text.find("AMP") != std::string::npos ||
                         text.find("CURR") != std::string::npos ||
                         text.find("TCTR") != std::string::npos;
    const auto suffix = phase_suffix(text, normalize(channel.phase));
    if (suffix.empty()) {
        return {};
    }
    if (voltage && !current) {
        return "V" + suffix;
    }
    if (current && !voltage) {
        return "I" + suffix;
    }
    if (voltage) {
        return "V" + suffix;
    }
    if (current) {
        return "I" + suffix;
    }
    return {};
}

} // namespace

std::map<std::string, std::size_t, std::less<>> ChannelMapper::create_default_map(
    const std::vector<AnalogChannel>& channels) {
    std::map<std::string, std::size_t, std::less<>> result;
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        const auto key = resolve_key(channels[index]);
        if (!key.empty() && !result.contains(key)) {
            result.emplace(key, index);
        }
    }
    return result;
}

} // namespace ar::iec61850::comtrade
