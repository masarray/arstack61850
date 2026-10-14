// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/data_value.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Presentation-only, bounded MMS formatting. The canonical codec and the
// original decoded value are unchanged. Scalar formatting delegates to the
// canonical codec; only potentially unbounded strings, raw bytes and nested
// collections are streamed with a character budget.
namespace arstack::iedsim {

struct BoundedMmsPreview final {
    std::string text;
    bool truncated{};
};

namespace preview_detail {
struct Writer final {
    BoundedMmsPreview result;
    std::size_t limit{};

    explicit Writer(const std::size_t maximum) : limit(maximum) {
        result.text.reserve(std::min(maximum, std::size_t{1024}));
    }

    void append(const std::string_view text) {
        if (text.empty()) return;
        const auto available = limit - result.text.size();
        const auto count = std::min(available, text.size());
        result.text.append(text.data(), count);
        if (count < text.size()) result.truncated = true;
    }

    // Avoid cutting a valid UTF-8 sequence in half on a byte budget.
    // IEC VisibleString is usually ASCII; MMSString may contain UTF-8.
    void appendUtf8(const std::string_view text) {
        if (text.empty()) return;
        const auto available = limit - result.text.size();
        auto count = std::min(available, text.size());
        if (count < text.size()) {
            while (count > 0 &&
                   (static_cast<unsigned char>(text[count]) & 0xC0U) == 0x80U)
                --count;
        }
        result.text.append(text.data(), count);
        if (count < text.size()) result.truncated = true;
    }

    void appendChar(const char ch) {
        if (result.text.size() < limit) result.text.push_back(ch);
        else result.truncated = true;
    }

    void appendHex(const std::vector<std::uint8_t>& bytes) {
        static constexpr char hex[] = "0123456789ABCDEF";
        for (const auto byte : bytes) {
            if (result.text.size() >= limit) {
                result.truncated = true;
                return;
            }
            appendChar(hex[(byte >> 4U) & 0x0FU]);
            appendChar(hex[byte & 0x0FU]);
            if (result.truncated) return;
        }
    }
};

inline void appendMmsValue(Writer& writer,
                           const ar::iec61850::mms::MmsDataValue& value,
                           const unsigned depth) {
    using namespace ar::iec61850::mms;
    if (writer.result.truncated) return;
    switch (value.kind()) {
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        writer.appendUtf8(std::get<std::string>(value.value()));
        return;
    case MmsDataKind::array:
    case MmsDataKind::structure: {
        if (depth >= 16U) {
            writer.append("...");
            writer.result.truncated = true;
            return;
        }
        writer.appendChar(value.kind() == MmsDataKind::array ? '[' : '{');
        const auto& children = value.children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (index != 0U) writer.append(", ");
            if (writer.result.truncated) break;
            appendMmsValue(writer, children[index], depth + 1U);
            if (writer.result.truncated) break;
        }
        if (!writer.result.truncated)
            writer.appendChar(value.kind() == MmsDataKind::array ? ']' : '}');
        return;
    }
    case MmsDataKind::bit_string:
    case MmsDataKind::octet_string:
    case MmsDataKind::binary_time:
    case MmsDataKind::unknown:
    case MmsDataKind::bcd:
    case MmsDataKind::boolean_array:
    case MmsDataKind::object_id:
        writer.appendHex(value.raw_value());
        return;
    default:
        // All remaining scalar display strings have bounded size; formatting
        // belongs to the canonical codec, not a parallel IEC decoder.
        writer.append(MmsDataCodec::to_display_string(value));
        return;
    }
}
} // namespace preview_detail

[[nodiscard]] inline BoundedMmsPreview previewMmsValue(
    const ar::iec61850::mms::MmsDataValue& value,
    const std::size_t maximumCharacters) {
    preview_detail::Writer writer(maximumCharacters);
    preview_detail::appendMmsValue(writer, value, 0U);
    return std::move(writer.result);
}

} // namespace arstack::iedsim
