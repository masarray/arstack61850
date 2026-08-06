// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/scl/parser.hpp"

#include "ariec61850/scl/dataset_reference.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ar::iec61850::scl {
namespace {

constexpr std::size_t max_xml_bytes = 64U * 1024U * 1024U;
constexpr std::size_t max_xml_depth = 128U;
constexpr std::size_t max_xml_nodes = 500000U;
constexpr std::size_t max_attributes_per_element = 256U;

std::string trim_copy(const std::string_view text) {
    auto first = text.begin();
    while (first != text.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    auto last = text.end();
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }
    return {first, last};
}

std::string lower_copy(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::string upper_copy(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return result;
}

bool same(const std::string_view left, const std::string_view right) {
    return lower_copy(trim_copy(left)) == lower_copy(trim_copy(right));
}

std::string_view local_name(const std::string_view qualified_name) {
    const auto separator = qualified_name.find_last_of(':');
    return separator == std::string_view::npos
        ? qualified_name
        : qualified_name.substr(separator + 1U);
}

struct XmlAttribute final {
    std::string name;
    std::string value;
};

struct XmlNode final {
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::string text;
    std::vector<XmlNode> children;
};

class SecureXmlParser final {
public:
    explicit SecureXmlParser(const std::string_view input) : input_(input) {
        if (input_.size() > max_xml_bytes) {
            throw std::runtime_error("SCL XML exceeds the 64 MiB parser limit.");
        }
    }

    [[nodiscard]] XmlNode parse_document() {
        skip_utf8_bom();
        skip_misc();
        if (position_ >= input_.size()) {
            throw std::runtime_error("SCL XML is empty.");
        }
        auto root = parse_element(0U);
        skip_misc();
        if (position_ != input_.size()) {
            fail("Unexpected data after the root element.");
        }
        return root;
    }

private:
    std::string_view input_;
    std::size_t position_{};
    std::size_t node_count_{};

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(message + " XML byte offset " + std::to_string(position_) + ".");
    }

    [[nodiscard]] bool starts_with(const std::string_view text) const noexcept {
        return input_.substr(position_).starts_with(text);
    }

    void skip_utf8_bom() noexcept {
        if (input_.size() >= 3U &&
            static_cast<unsigned char>(input_[0]) == 0xEFU &&
            static_cast<unsigned char>(input_[1]) == 0xBBU &&
            static_cast<unsigned char>(input_[2]) == 0xBFU) {
            position_ = 3U;
        }
    }

    void skip_whitespace() noexcept {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    void skip_misc() {
        while (true) {
            skip_whitespace();
            if (starts_with("<?")) {
                skip_processing_instruction();
                continue;
            }
            if (starts_with("<!--")) {
                skip_comment();
                continue;
            }
            if (starts_with("<!DOCTYPE") || starts_with("<!ENTITY")) {
                fail("DTD and entity declarations are prohibited");
            }
            break;
        }
    }

    void skip_processing_instruction() {
        const auto end = input_.find("?>", position_ + 2U);
        if (end == std::string_view::npos) {
            fail("Unterminated processing instruction");
        }
        position_ = end + 2U;
    }

    void skip_comment() {
        const auto end = input_.find("-->", position_ + 4U);
        if (end == std::string_view::npos) {
            fail("Unterminated XML comment");
        }
        position_ = end + 3U;
    }

    [[nodiscard]] static bool is_name_start(const char ch) noexcept {
        const auto value = static_cast<unsigned char>(ch);
        return std::isalpha(value) != 0 || ch == '_' || ch == ':';
    }

    [[nodiscard]] static bool is_name_char(const char ch) noexcept {
        const auto value = static_cast<unsigned char>(ch);
        return std::isalnum(value) != 0 || ch == '_' || ch == ':' || ch == '-' || ch == '.';
    }

    [[nodiscard]] std::string parse_name() {
        if (position_ >= input_.size() || !is_name_start(input_[position_])) {
            fail("Expected XML name");
        }
        const auto start = position_++;
        while (position_ < input_.size() && is_name_char(input_[position_])) {
            ++position_;
        }
        return std::string{input_.substr(start, position_ - start)};
    }

    [[nodiscard]] static char32_t parse_code_point(
        const std::string_view digits,
        const int base) {
        if (digits.empty()) {
            throw std::runtime_error("Empty numeric XML entity.");
        }
        std::uint32_t value{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
            value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU) || value == 0U) {
            throw std::runtime_error("Invalid numeric XML entity.");
        }
        return static_cast<char32_t>(value);
    }

    static void append_utf8(std::string& output, const char32_t code_point) {
        const auto value = static_cast<std::uint32_t>(code_point);
        if (value <= 0x7FU) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

