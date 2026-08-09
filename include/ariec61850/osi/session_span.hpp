// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::osi {

enum class SessionWireKind : std::uint8_t {
    unknown,
    connect,
    accept,
    reject,
    refuse,
    abort,
    data_transfer,
};

struct SessionParameterView final {
    std::uint8_t code{};
    std::span<const std::uint8_t> value{};
};

struct SessionSpduView final {
    SessionWireKind kind{SessionWireKind::unknown};
    std::uint8_t code{};
    std::uint8_t length_indicator{};
    std::span<const std::uint8_t> parameter_bytes{};
    std::span<const std::uint8_t> user_data{};

    [[nodiscard]] bool try_parameter(
        std::uint8_t parameter_code,
        std::span<const std::uint8_t>& value) const noexcept;
};

struct SessionDataTransferView final {
    bool has_give_tokens_prefix{};
    std::span<const std::uint8_t> presentation_payload{};
};

class SessionSpanCodec final {
public:
    static constexpr std::uint8_t connect_code = 0x0DU;
    static constexpr std::uint8_t accept_code = 0x0EU;
    static constexpr std::uint8_t reject_code = 0x0AU;
    static constexpr std::uint8_t refuse_code = 0x0CU;
    static constexpr std::uint8_t abort_code = 0x19U;
    static constexpr std::uint8_t data_transfer_code = 0x01U;
    static constexpr std::uint8_t user_data_parameter = 0xC1U;
    static constexpr std::size_t maximum_spdu_bytes = 257U;
    static constexpr std::size_t maximum_parameter_count = 64U;

    [[nodiscard]] static SessionWireKind kind_from_code(std::uint8_t code) noexcept;
    [[nodiscard]] static std::span<const SessionParameterView> default_parameters() noexcept;

    [[nodiscard]] static bool try_decode_prefix_view(
        std::span<const std::uint8_t> bytes,
        SessionSpduView& spdu,
        std::size_t& consumed) noexcept;

    [[nodiscard]] static bool try_decode_view(
        std::span<const std::uint8_t> bytes,
        SessionSpduView& spdu) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        std::uint8_t code,
        std::span<const SessionParameterView> parameters,
        std::span<const std::uint8_t> user_data,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_connect_into(
        std::span<const std::uint8_t> presentation_payload,
        std::span<std::uint8_t> destination,
        std::span<const SessionParameterView> parameters = {}) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_accept_into(
        std::span<const std::uint8_t> presentation_payload,
        std::span<std::uint8_t> destination,
        std::span<const SessionParameterView> parameters = {}) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_accept_mirroring_into(
        const SessionSpduView& connect,
        std::span<const std::uint8_t> presentation_payload,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_data_transfer_into(
        std::span<const std::uint8_t> presentation_payload,
        std::span<std::uint8_t> destination,
        bool include_give_tokens_prefix = true) noexcept;

    [[nodiscard]] static bool try_decode_data_transfer_view(
        std::span<const std::uint8_t> bytes,
        SessionDataTransferView& transfer) noexcept;
};

} // namespace ar::iec61850::osi
