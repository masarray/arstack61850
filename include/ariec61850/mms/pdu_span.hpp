// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsWirePduKind : std::uint8_t {
    unknown,
    confirmed_request,
    confirmed_response,
    initiate_request,
    initiate_response,
    confirmed_error,
    conclude_request,
    conclude_response,
};

enum class MmsWireConfirmedService : std::int32_t {
    unknown = -1,
    get_name_list = 1,
    identify = 2,
    read = 4,
    write = 5,
    get_variable_access_attributes = 6,
    get_named_variable_list_attributes = 12,
};

struct MmsInitiateDetailView final {
    std::uint32_t version_number{};
    std::span<const std::uint8_t> parameter_support_options{};
    std::span<const std::uint8_t> services_supported_calling{};
};

struct MmsInitiateView final {
    MmsWirePduKind kind{MmsWirePduKind::unknown};
    std::uint32_t maximum_mms_pdu_size{};
    std::uint32_t maximum_outstanding_calling{};
    std::uint32_t maximum_outstanding_called{};
    std::uint32_t data_structure_nesting_level{};
    MmsInitiateDetailView detail{};
};

struct MmsConfirmedPduView final {
    MmsWirePduKind kind{MmsWirePduKind::unknown};
    std::uint32_t invoke_id{};
    std::int32_t service_tag{-1};
    bool service_constructed{};
    std::span<const std::uint8_t> service_value{};

    [[nodiscard]] MmsWireConfirmedService service() const noexcept;
};

class MmsPduSpanCodec final {
public:
    static constexpr std::size_t maximum_pdu_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_bit_string_bytes = 256U;
    static constexpr std::uint32_t maximum_invoke_id = 0x7FFF'FFFFU;

    [[nodiscard]] static bool try_decode_initiate_request_view(
        std::span<const std::uint8_t> bytes,
        MmsInitiateView& request) noexcept;

    [[nodiscard]] static bool try_decode_initiate_response_view(
        std::span<const std::uint8_t> bytes,
        MmsInitiateView& response) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_default_initiate_response_into(
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static bool try_decode_confirmed_request_view(
        std::span<const std::uint8_t> bytes,
        MmsConfirmedPduView& request) noexcept;

    [[nodiscard]] static bool try_decode_confirmed_response_view(
        std::span<const std::uint8_t> bytes,
        MmsConfirmedPduView& response) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_confirmed_request_into(
        std::uint32_t invoke_id,
        std::int32_t service_tag,
        bool service_constructed,
        std::span<const std::uint8_t> service_value,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_confirmed_response_into(
        std::uint32_t invoke_id,
        std::int32_t service_tag,
        bool service_constructed,
        std::span<const std::uint8_t> service_value,
        std::span<std::uint8_t> destination) noexcept;

    // Engineering-client compatibility primitives ported from the proven
    // ARIEC61850 simulator/server behavior. These remain bounded/no-throw and
    // are consumed by the server connection runtime rather than weakening the
    // strict application dispatcher contract.
    [[nodiscard]] static wire::EncodeResult encode_identify_response_into(
        std::uint32_t invoke_id,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_confirmed_error_into(
        std::uint32_t invoke_id,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static bool is_conclude_request(
        std::span<const std::uint8_t> bytes) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_conclude_response_into(
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
