// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::osi {

enum class CotpWireKind : std::uint8_t {
    unknown,
    connection_request,
    connection_confirm,
    data,
    disconnect_request,
    error,
};

struct CotpParameterView final {
    std::uint8_t code{};
    std::span<const std::uint8_t> value{};
};

struct CotpTpduView final {
    CotpWireKind kind{CotpWireKind::unknown};
    std::uint8_t code{};
    std::uint8_t length_indicator{};
    std::uint16_t destination_reference{};
    std::uint16_t source_reference{};
    std::uint8_t class_or_reason{};
    bool end_of_transmission{};
    std::uint8_t tpdu_number{};
    std::span<const std::uint8_t> parameter_bytes{};
    std::span<const std::uint8_t> user_data{};

    [[nodiscard]] bool try_parameter(
        std::uint8_t parameter_code,
        std::span<const std::uint8_t>& value) const noexcept;
};

class CotpSpanCodec final {
public:
    static constexpr std::uint8_t connection_request_code = 0xE0U;
    static constexpr std::uint8_t connection_confirm_code = 0xD0U;
    static constexpr std::uint8_t disconnect_request_code = 0x80U;
    static constexpr std::uint8_t data_code = 0xF0U;
    static constexpr std::uint8_t error_code = 0x70U;

    static constexpr std::uint8_t tpdu_size_parameter = 0xC0U;
    static constexpr std::uint8_t source_tsap_parameter = 0xC1U;
    static constexpr std::uint8_t destination_tsap_parameter = 0xC2U;

    [[nodiscard]] static CotpWireKind kind_from_code(std::uint8_t code) noexcept;

    // Zero-copy decode. parameter_bytes and user_data point into the supplied
    // TPDU storage, which must outlive the returned view.
    [[nodiscard]] static bool try_decode_view(
        std::span<const std::uint8_t> bytes,
        CotpTpduView& tpdu) noexcept;

    [[nodiscard]] static bool try_tpdu_size_bytes(
        std::uint8_t tpdu_size_code,
        std::size_t& bytes) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_data_into(
        std::span<const std::uint8_t> user_data,
        std::span<std::uint8_t> destination,
        bool end_of_transmission = true,
        std::uint8_t tpdu_number = 0U) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_connection_request_into(
        std::uint16_t source_reference,
        std::span<const CotpParameterView> parameters,
        std::span<std::uint8_t> destination,
        std::uint16_t destination_reference = 0U,
        std::uint8_t transport_class = 0U) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_connection_confirm_into(
        std::uint16_t destination_reference,
        std::uint16_t source_reference,
        std::span<std::uint8_t> destination,
        std::uint8_t maximum_tpdu_size_code = 0x0AU) noexcept;

    // Build a server-side CC directly from a zero-copy CR view. The TPDU-size
    // offer is negotiated and source/destination TSAP parameters are mirrored
    // using the same wire behavior as the host codec, without heap storage.
    [[nodiscard]] static wire::EncodeResult encode_connection_confirm_from_request_into(
        const CotpTpduView& connection_request,
        std::uint16_t source_reference,
        std::span<std::uint8_t> destination,
        std::uint8_t maximum_tpdu_size_code = 0x0AU) noexcept;
};

} // namespace ar::iec61850::osi
