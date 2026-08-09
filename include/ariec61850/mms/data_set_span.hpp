// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

struct MmsNamedVariableListAttributesRequestView final {
    std::uint32_t invoke_id{};
    MmsObjectNameView name{};
};

struct MmsNamedVariableListAttributesResponseView final {
    std::uint32_t invoke_id{};
    bool mms_deletable{};
    std::span<const std::uint8_t> variable_list{};
    std::size_t member_count{};

    [[nodiscard]] bool try_member(
        std::size_t index,
        MmsObjectNameView& name) const noexcept;
};

struct MmsNamedVariableListMemberInput final {
    std::string_view domain;
    std::string_view item;
};

class MmsDataSetSpanCodec final {
public:
    static constexpr std::size_t maximum_members = MmsServiceSpanCodec::maximum_variables;

    [[nodiscard]] static bool try_decode_get_named_variable_list_attributes_request(
        const MmsConfirmedPduView& confirmed,
        MmsNamedVariableListAttributesRequestView& request) noexcept;

    [[nodiscard]] static bool try_decode_get_named_variable_list_attributes_request(
        std::span<const std::uint8_t> mms_pdu,
        MmsNamedVariableListAttributesRequestView& request) noexcept;

    [[nodiscard]] static bool try_decode_get_named_variable_list_attributes_response(
        const MmsConfirmedPduView& confirmed,
        MmsNamedVariableListAttributesResponseView& response) noexcept;

    [[nodiscard]] static bool try_decode_get_named_variable_list_attributes_response(
        std::span<const std::uint8_t> mms_pdu,
        MmsNamedVariableListAttributesResponseView& response) noexcept;

    [[nodiscard]] static wire::EncodeResult
        encode_get_named_variable_list_attributes_response_into(
            std::uint32_t invoke_id,
            bool mms_deletable,
            std::span<const MmsNamedVariableListMemberInput> members,
            std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
