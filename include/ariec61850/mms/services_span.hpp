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

enum class MmsObjectNameViewKind : std::uint8_t {
    vmd_specific,
    domain_specific,
    aa_specific,
};

struct MmsObjectNameView final {
    MmsObjectNameViewKind kind{MmsObjectNameViewKind::domain_specific};
    std::span<const std::uint8_t> domain{};
    std::span<const std::uint8_t> item{};
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

struct MmsVariableAccessAttributesRequestView final {
    std::uint32_t invoke_id{};
    MmsObjectNameView name{};
};

struct MmsVariableAccessAttributesResponseView final {
    std::uint32_t invoke_id{};
    bool mms_deletable{};
    std::span<const std::uint8_t> type_specification{};
};

struct MmsReadRequestView final {
    std::uint32_t invoke_id{};
    bool specification_with_result{};
    std::span<const std::uint8_t> variable_list{};
    std::size_t variable_count{};

    [[nodiscard]] bool try_variable(
        std::size_t index,
        MmsObjectNameView& name) const noexcept;
};

struct MmsReadAccessResultView final {
    bool success{};
    std::span<const std::uint8_t> encoded_data{};
    std::uint32_t failure_code{};
};

struct MmsReadResponseView final {
    std::uint32_t invoke_id{};
    std::span<const std::uint8_t> access_result_list{};
    std::size_t result_count{};

    [[nodiscard]] bool try_result(
        std::size_t index,
        MmsReadAccessResultView& result) const noexcept;
};

struct MmsReadAccessResultInput final {
    bool success{};
    std::span<const std::uint8_t> encoded_data{};
    std::uint32_t failure_code{};
};

struct MmsWriteRequestView final {
    std::uint32_t invoke_id{};
    std::span<const std::uint8_t> variable_list{};
    std::size_t variable_count{};
    std::span<const std::uint8_t> data_list{};
    std::size_t data_count{};

    [[nodiscard]] bool try_variable(
        std::size_t index,
        MmsObjectNameView& name) const noexcept;

    [[nodiscard]] bool try_value(
        std::size_t index,
        std::span<const std::uint8_t>& encoded_data) const noexcept;
};

struct MmsWriteAccessResultView final {
    bool success{};
    std::uint32_t failure_code{};
};

struct MmsWriteResponseView final {
    std::uint32_t invoke_id{};
    std::span<const std::uint8_t> result_list{};
    std::size_t result_count{};
    bool implicit_single_success{};

    [[nodiscard]] bool try_result(
        std::size_t index,
        MmsWriteAccessResultView& result) const noexcept;
};

struct MmsWriteAccessResultInput final {
    bool success{};
    std::uint32_t failure_code{};
};

class MmsServiceSpanCodec final {
public:
    static constexpr std::size_t maximum_identifier_bytes = 1'024U;
    static constexpr std::size_t maximum_identifiers = 128U;
    static constexpr std::size_t maximum_variables = 64U;

    [[nodiscard]] static bool try_decode_object_name_view(
        std::span<const std::uint8_t> encoded_object_name,
        MmsObjectNameView& name) noexcept;

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

    [[nodiscard]] static bool try_decode_variable_access_attributes_request(
        const MmsConfirmedPduView& confirmed,
        MmsVariableAccessAttributesRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_variable_access_attributes_request(
        std::span<const std::uint8_t> mms_pdu,
        MmsVariableAccessAttributesRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_variable_access_attributes_response(
        const MmsConfirmedPduView& confirmed,
        MmsVariableAccessAttributesResponseView& response) noexcept;
    [[nodiscard]] static bool try_decode_variable_access_attributes_response(
        std::span<const std::uint8_t> mms_pdu,
        MmsVariableAccessAttributesResponseView& response) noexcept;
    [[nodiscard]] static wire::EncodeResult encode_variable_access_attributes_response_into(
        std::uint32_t invoke_id,
        bool mms_deletable,
        std::span<const std::uint8_t> encoded_type_specification,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static bool try_decode_read_request(
        const MmsConfirmedPduView& confirmed,
        MmsReadRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_read_request(
        std::span<const std::uint8_t> mms_pdu,
        MmsReadRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_read_response(
        const MmsConfirmedPduView& confirmed,
        MmsReadResponseView& response) noexcept;
    [[nodiscard]] static bool try_decode_read_response(
        std::span<const std::uint8_t> mms_pdu,
        MmsReadResponseView& response) noexcept;
    [[nodiscard]] static wire::EncodeResult encode_read_response_into(
        std::uint32_t invoke_id,
        std::span<const MmsReadAccessResultInput> results,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static bool try_decode_write_request(
        const MmsConfirmedPduView& confirmed,
        MmsWriteRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_write_request(
        std::span<const std::uint8_t> mms_pdu,
        MmsWriteRequestView& request) noexcept;
    [[nodiscard]] static bool try_decode_write_response(
        const MmsConfirmedPduView& confirmed,
        MmsWriteResponseView& response) noexcept;
    [[nodiscard]] static bool try_decode_write_response(
        std::span<const std::uint8_t> mms_pdu,
        MmsWriteResponseView& response) noexcept;
    [[nodiscard]] static wire::EncodeResult encode_write_response_into(
        std::uint32_t invoke_id,
        std::span<const MmsWriteAccessResultInput> results,
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
