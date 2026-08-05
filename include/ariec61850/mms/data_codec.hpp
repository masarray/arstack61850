// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_value.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsDataCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const MmsDataValue& value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_all(
        std::span<const MmsDataValue> values);

    [[nodiscard]] static MmsDataValue decode(const asn1::BerTlv& tlv);
    [[nodiscard]] static std::vector<MmsDataValue> decode_all(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::string to_display_string(const MmsDataValue& value);

private:
    [[nodiscard]] static std::vector<std::uint8_t> encode_content(const MmsDataValue& value);
    [[nodiscard]] static std::int32_t tag_for(const MmsDataValue& value);
    [[nodiscard]] static MmsDataValue decode_bit_string(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static float decode_floating_point(std::span<const std::uint8_t> bytes) noexcept;
};

} // namespace ar::iec61850::mms
