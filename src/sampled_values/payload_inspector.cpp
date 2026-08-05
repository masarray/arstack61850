// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/payload_inspector.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ar::iec61850::sampled_values {
namespace {

constexpr std::size_t word_bytes = 4U;
constexpr std::size_t structural_group_bytes = 8U;

std::uint32_t read_u32_be(const std::span<const std::uint8_t> bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

GenericPayloadWordRole resolve_role(
    const std::size_t index,
    const bool has_eight_byte_group_shape) noexcept {
    if (!has_eight_byte_group_shape) {
        return GenericPayloadWordRole::standalone_word;
    }
    return (index % 2U) == 0U
        ? GenericPayloadWordRole::first_word_in_eight_byte_group
        : GenericPayloadWordRole::second_word_in_eight_byte_group;
}

} // namespace

bool GenericPayloadWord::is_finite_float32() const noexcept {
    return std::isfinite(float32);
}

GenericPayloadInspection GenericPayloadInspector::inspect(
    const std::span<const std::uint8_t> payload) {
    GenericPayloadInspection result;
    result.payload_length = payload.size();
    result.complete_word_count = payload.size() / word_bytes;
    result.trailing_byte_count = payload.size() % word_bytes;
    result.is_four_byte_aligned = result.trailing_byte_count == 0U;
    result.has_eight_byte_group_shape =
        payload.size() >= structural_group_bytes &&
        (payload.size() % structural_group_bytes) == 0U;
    result.words.reserve(result.complete_word_count);

    for (std::size_t index = 0U; index < result.complete_word_count; ++index) {
        const auto offset = index * word_bytes;
        const auto bytes = payload.subspan(offset, word_bytes);
        const auto unsigned_value = read_u32_be(bytes);
        GenericPayloadWord word;
        word.index = index;
        word.byte_offset = offset;
        word.structural_role = resolve_role(index, result.has_eight_byte_group_shape);
        for (std::size_t byte_index = 0U; byte_index < word_bytes; ++byte_index) {
            word.raw_bytes[byte_index] = bytes[byte_index];
        }
        word.unsigned_int32 = unsigned_value;
        word.signed_int32 = std::bit_cast<std::int32_t>(unsigned_value);
        word.float32 = std::bit_cast<float>(unsigned_value);
        result.words.push_back(word);
    }

    if (result.trailing_byte_count > 0U) {
        const auto trailing = payload.last(result.trailing_byte_count);
        result.trailing_bytes.assign(trailing.begin(), trailing.end());
    }

    if (payload.empty()) {
        result.diagnostics.emplace_back("seqOfData is empty.");
        return result;
    }

    result.diagnostics.emplace_back(
        "Generic inspection exposed " +
        std::to_string(result.complete_word_count) +
        " complete big-endian 32-bit word(s) without assigning channel names or engineering units.");

    if (result.has_eight_byte_group_shape) {
        result.diagnostics.emplace_back(
            "The payload has an 8-byte grouping shape. This is structural evidence only; "
            "the second word is not treated as IEC 61850 quality until trusted engineering "
            "evidence resolves it.");
    }

    if (result.trailing_byte_count > 0U) {
        result.diagnostics.emplace_back(
            "The payload contains " + std::to_string(result.trailing_byte_count) +
            " trailing byte(s) after the last complete 32-bit word; those bytes are preserved verbatim.");
    }

    return result;
}

} // namespace ar::iec61850::sampled_values
