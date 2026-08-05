// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

enum class GenericPayloadWordRole : std::uint8_t {
    standalone_word,
    first_word_in_eight_byte_group,
    second_word_in_eight_byte_group
};

struct GenericPayloadWord final {
    std::size_t index{};
    std::size_t byte_offset{};
    GenericPayloadWordRole structural_role{GenericPayloadWordRole::standalone_word};
    std::array<std::uint8_t, 4> raw_bytes{};
    std::int32_t signed_int32{};
    std::uint32_t unsigned_int32{};
    float float32{};

    [[nodiscard]] bool is_finite_float32() const noexcept;
};

struct GenericPayloadInspection final {
    std::size_t payload_length{};
    std::size_t complete_word_count{};
    std::size_t trailing_byte_count{};
    bool is_four_byte_aligned{};
    bool has_eight_byte_group_shape{};
    std::vector<GenericPayloadWord> words;
    std::vector<std::uint8_t> trailing_bytes;
    std::vector<std::string> diagnostics;
};

class GenericPayloadInspector final {
public:
    [[nodiscard]] static GenericPayloadInspection inspect(
        std::span<const std::uint8_t> payload);
};

} // namespace ar::iec61850::sampled_values
