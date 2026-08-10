// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::iec61850::asn1 {

struct BerTlvView final {
    std::uint8_t encoded_tag{};
    BerClass tag_class{BerClass::universal};
    bool constructed{};
    std::int32_t tag_number{};
    std::size_t header_bytes{};
    std::size_t encoded_bytes{};
    std::span<const std::uint8_t> value{};
};

class BerSpanReader final {
public:
    // Read one definite-length BER TLV without allocating or copying its value.
    // The input offset is advanced only on success.
    [[nodiscard]] static bool try_read_tlv(
        std::span<const std::uint8_t> source,
        std::size_t& offset,
        BerTlvView& tlv) noexcept;

    [[nodiscard]] static bool try_read_exact(
        std::span<const std::uint8_t> source,
        BerTlvView& tlv) noexcept;

    [[nodiscard]] static std::optional<std::uint64_t> read_unsigned_integer(
        const BerTlvView& tlv) noexcept;

    [[nodiscard]] static std::optional<std::int64_t> read_signed_integer(
        const BerTlvView& tlv) noexcept;

    [[nodiscard]] static std::optional<std::uint32_t> read_uint32(
        const BerTlvView& tlv) noexcept;
};

} // namespace ar::iec61850::asn1
