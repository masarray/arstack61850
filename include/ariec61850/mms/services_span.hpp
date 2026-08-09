// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/pdu_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

enum class MmsNameListObjectClass : std::uint32_t {
    named_variable = 0U,
    named_variable_list = 2U,
    domain = 9U,
};

enum class MmsNameScopeKind : std::uint8_t {
    vmd_specific,
    domain_specific,
    aa_specific,
};

struct MmsGetNameListRequestView final {
    std::uint32_t invoke_id{};
    MmsNameListObjectClass object_class{MmsNameListObjectClass::domain};
    MmsNameScopeKind scope{MmsNameScopeKind::vmd_specific};
    std::span<const std::uint8_t> domain_id{};
    std::span<const std::uint8_t> continue_after{};
};

struct MmsGetNameListResponseView final {
    std::uint32_t invoke_id{};
    std::span<const std::uint8_t> identifier_list{};
    std::size_t identifier_count{};
    bool more_follows{};

    [[nodiscard]] bool try_identifier(
        std::size_t index,
        std::span<const std::uint8_t>& identifier) const noexcept;
};

class MmsServiceSpanCodec final {
public:
    static constexpr std::size_t maximum_identifier_bytes = 1'024U;
    static constexpr std::size_t maximum_identifiers = 128U;

    [[nodiscard]] static bool try_decode_get_name_list_request(
        const MmsConfirmedPduView& confirmed,
        MmsGetNameListRequestView& request) noexcept;

    [[nodiscard]] static bool try_decode_get_name_list_request(
        std::span<const std::uint8_t> mms_pdu,
        MmsGetNameListRequestView& request) noexcept;

    [[nodiscard]] static bool try_decode_get_name_list_response(
        const MmsConfirmedPduView& confirmed,
        MmsGetNameListResponseView& response) noexcept;

    [[nodiscard]] static bool try_decode_get_name_list_response(
        std::span<const std::uint8_t> mms_pdu,
        MmsGetNameListResponseView& response) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_get_name_list_response_into(
        std::uint32_t invoke_id,
        std::span<const std::string_view> names,
        bool more_follows,
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
