// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::sampled_values {
namespace wire_field_detail {

constexpr std::uint16_t kSampledValuesEtherType = 0x88BAU;
constexpr std::uint16_t kVlanEtherType = 0x8100U;
constexpr std::uint16_t kQinQEtherType = 0x88A8U;
constexpr std::size_t kEthernetHeaderBytes = 14U;
constexpr std::size_t kVlanHeaderBytes = 4U;
constexpr std::size_t kProcessBusHeaderBytes = 8U;

struct BerTlvView final {
    std::uint8_t tag{};
    std::size_t value_offset{};
    std::size_t value_length{};
    std::size_t end_offset{};
};

[[nodiscard]] inline std::uint16_t read_u16_be(
    const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U) |
        static_cast<std::uint16_t>(bytes[1]));
}

[[nodiscard]] inline bool read_ber_tlv(
    const std::span<const std::uint8_t> bytes,
    std::size_t& cursor,
    const std::size_t limit,
    BerTlvView& tlv) noexcept {
    if (limit > bytes.size() || cursor >= limit) return false;

    const auto tag = bytes[cursor++];
    // The SV profile used here has single-octet BER identifiers. Reject the
    // high-tag-number form rather than attempting to skip it ambiguously.
    if ((tag & 0x1FU) == 0x1FU || cursor >= limit) return false;

    const auto first_length = bytes[cursor++];
    std::size_t value_length = 0U;
    if ((first_length & 0x80U) == 0U) {
        value_length = first_length;
    } else {
        const auto length_octets = static_cast<std::size_t>(first_length & 0x7FU);
        if (length_octets == 0U || length_octets > sizeof(std::size_t) ||
            length_octets > limit - cursor) {
            return false;
        }

        for (std::size_t index = 0U; index < length_octets; ++index) {
            if (value_length > (std::numeric_limits<std::size_t>::max() >> 8U)) {
                return false;
            }
            value_length = (value_length << 8U) | bytes[cursor++];
        }
    }

    if (value_length > limit - cursor) return false;
    tlv = {tag, cursor, value_length, cursor + value_length};
    cursor = tlv.end_offset;
    return true;
}

} // namespace wire_field_detail

/**
 * Locate the value octet of the first ASDU's smpSynch field in an IEC 61850-9-2
 * Sampled Values Ethernet frame.
 *
 * The locator walks the actual BER container hierarchy (savPdu -> seqASDU ->
 * ASDU -> context tag 5). It never searches for a raw byte pattern, so bytes
 * inside svID, dataSet, or sample payload cannot be mistaken for smpSynch.
 *
 * The returned offset points directly at the one-octet smpSynch value. Invalid,
 * truncated, non-SV, unsupported high-tag-number, or non-canonical field shapes
 * return std::nullopt without modifying the frame.
 */
[[nodiscard]] inline std::optional<std::size_t> find_smp_synch_value_offset(
    const std::span<const std::uint8_t> frame) noexcept {
    using namespace wire_field_detail;

    if (frame.size() < kEthernetHeaderBytes) return std::nullopt;

    std::size_t type_offset = 12U;
    auto ether_type = read_u16_be(frame.data() + type_offset);
    for (int tags = 0; tags < 2 &&
         (ether_type == kVlanEtherType || ether_type == kQinQEtherType); ++tags) {
        type_offset += kVlanHeaderBytes;
        if (type_offset + 2U > frame.size()) return std::nullopt;
        ether_type = read_u16_be(frame.data() + type_offset);
    }
    if (ether_type != kSampledValuesEtherType) return std::nullopt;

    const std::size_t process_bus_offset = type_offset + 2U;
    if (process_bus_offset + kProcessBusHeaderBytes > frame.size()) {
        return std::nullopt;
    }

    const auto declared_length = static_cast<std::size_t>(
        read_u16_be(frame.data() + process_bus_offset + 2U));
    if (declared_length < kProcessBusHeaderBytes ||
        declared_length > frame.size() - process_bus_offset) {
        return std::nullopt;
    }

    const std::size_t apdu_begin = process_bus_offset + kProcessBusHeaderBytes;
    const std::size_t apdu_end = process_bus_offset + declared_length;

    std::size_t cursor = apdu_begin;
    BerTlvView sav_pdu{};
    if (!read_ber_tlv(frame, cursor, apdu_end, sav_pdu) ||
        sav_pdu.tag != 0x60U || sav_pdu.end_offset != apdu_end) {
        return std::nullopt;
    }

    std::size_t pdu_cursor = sav_pdu.value_offset;
    while (pdu_cursor < sav_pdu.end_offset) {
        BerTlvView pdu_field{};
        if (!read_ber_tlv(frame, pdu_cursor, sav_pdu.end_offset, pdu_field)) {
            return std::nullopt;
        }
        if (pdu_field.tag != 0xA2U) continue;

        std::size_t sequence_cursor = pdu_field.value_offset;
        while (sequence_cursor < pdu_field.end_offset) {
            BerTlvView asdu{};
            if (!read_ber_tlv(frame, sequence_cursor, pdu_field.end_offset, asdu) ||
                asdu.tag != 0x30U) {
                return std::nullopt;
            }

            std::size_t field_cursor = asdu.value_offset;
            while (field_cursor < asdu.end_offset) {
                BerTlvView field{};
                if (!read_ber_tlv(frame, field_cursor, asdu.end_offset, field)) {
                    return std::nullopt;
                }
                if (field.tag == 0x85U) {
                    return field.value_length == 1U
                        ? std::optional<std::size_t>{field.value_offset}
                        : std::nullopt;
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace ar::iec61850::sampled_values
