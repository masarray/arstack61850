// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/reader.hpp"

#include "ariec61850/comtrade/channel_mapper.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ar::iec61850::comtrade {
namespace {

std::string trim_copy(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1U)};
}

std::string uppercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z')) {
            result.push_back(static_cast<char>(byte - static_cast<unsigned char>('a') +
                                               static_cast<unsigned char>('A')));
        } else {
            result.push_back(raw);
        }
    }
    return result;
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0U;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (!trim_copy(line).empty()) {
            lines.push_back(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return lines;
}

std::vector<std::string> split_csv(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        const char value = line[index];
        if (value == '"') {
            if (quoted && index + 1U < line.size() && line[index + 1U] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (value == ',' && !quoted) {
            fields.push_back(trim_copy(current));
            current.clear();
        } else {
            current.push_back(value);
        }
    }
    if (quoted) {
        throw std::runtime_error("COMTRADE CSV field has an unterminated quote.");
    }
    fields.push_back(trim_copy(current));
    return fields;
}

const std::string& field(const std::vector<std::string>& fields, const std::size_t index) {
    static const std::string empty;
    return index < fields.size() ? fields[index] : empty;
}

std::uint32_t parse_u32(const std::string_view text, const std::uint32_t fallback) {
    const auto value = trim_copy(text);
    if (value.empty()) {
        return fallback;
    }
    std::uint32_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? parsed : fallback;
}

double parse_double(const std::string_view text, const double fallback) {
    const auto value = trim_copy(text);
    if (value.empty()) {
        return fallback;
    }
    double parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? parsed : fallback;
}

std::uint32_t parse_channel_count(std::string_view text) {
    const auto value = trim_copy(text);
    std::size_t digits = 0U;
    while (digits < value.size() && value[digits] >= '0' && value[digits] <= '9') {
        ++digits;
    }
    return parse_u32(std::string_view{value}.substr(0U, digits), 0U);
}

std::uint32_t parse_typed_count(std::string_view text, const char suffix) {
    auto value = trim_copy(text);
    if (!value.empty() &&
        (value.back() == suffix || value.back() == static_cast<char>(suffix + ('a' - 'A')))) {
        value.pop_back();
    }
    return parse_u32(value, 0U);
}

DataFileType parse_data_type(std::string_view text) {
    const auto value = uppercase(trim_copy(text));
    if (value == "ASCII") {
        return DataFileType::ascii;
    }
    if (value == "BINARY") {
        return DataFileType::binary;
    }
    if (value == "BINARY32") {
        return DataFileType::binary32;
    }
    if (value == "FLOAT32") {
        return DataFileType::float32;
    }
