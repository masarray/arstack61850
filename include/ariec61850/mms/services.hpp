// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsObjectNameKind : std::uint8_t {
    vmd_specific,
    domain_specific,
    aa_specific,
};

struct MmsObjectName final {
    MmsObjectNameKind kind{MmsObjectNameKind::domain_specific};
    std::string domain;
    std::string item;

    [[nodiscard]] static MmsObjectName vmd(std::string name);
    [[nodiscard]] static MmsObjectName domain_specific(
        std::string domain, std::string item);
    [[nodiscard]] static MmsObjectName aa(std::string name);
    [[nodiscard]] std::string reference() const;

    friend bool operator==(const MmsObjectName&, const MmsObjectName&) = default;
};

enum class MmsGetNameListObjectClass : std::uint32_t {
    named_variable = 0U,
    named_variable_list = 2U,
    domain = 9U,
};

enum class MmsObjectScopeKind : std::uint8_t {
    vmd_specific,
    domain_specific,
    aa_specific,
};

struct MmsGetNameListRequest final {
    std::uint32_t invoke_id{};
    MmsGetNameListObjectClass object_class{MmsGetNameListObjectClass::domain};
    MmsObjectScopeKind scope{MmsObjectScopeKind::vmd_specific};
    std::string domain_id;
    std::string continue_after;

    friend bool operator==(const MmsGetNameListRequest&, const MmsGetNameListRequest&) = default;
};

struct MmsGetNameListResponse final {
    std::uint32_t invoke_id{};
    std::vector<std::string> names;
    bool more_follows{};

    friend bool operator==(const MmsGetNameListResponse&, const MmsGetNameListResponse&) = default;
};

enum class MmsTypeKind : std::uint8_t {
    array,
    structure,
    boolean,
    bit_string,
    integer,
    unsigned_integer,
    floating_point,
    octet_string,
    visible_string,
    binary_time,
    bcd,
    boolean_array,
    object_id,
    mms_string,
    utc_time,
    unknown,
};

struct MmsTypeSpecification final {
    MmsTypeKind kind{MmsTypeKind::unknown};
    std::string name;
    std::optional<std::uint32_t> size;
    std::optional<std::uint32_t> exponent_width;
    std::vector<MmsTypeSpecification> children;

    [[nodiscard]] std::string mms_type_name() const;
    [[nodiscard]] std::string scl_basic_type() const;
    [[nodiscard]] std::string signature() const;

    friend bool operator==(const MmsTypeSpecification&, const MmsTypeSpecification&) = default;
};

struct MmsVariableAccessAttributesRequest final {
    std::uint32_t invoke_id{};
    MmsObjectName name;

    friend bool operator==(const MmsVariableAccessAttributesRequest&,
                           const MmsVariableAccessAttributesRequest&) = default;
};

struct MmsVariableAccessAttributesResponse final {
    std::uint32_t invoke_id{};
    bool mms_deletable{};
    MmsTypeSpecification type;

    friend bool operator==(const MmsVariableAccessAttributesResponse&,
                           const MmsVariableAccessAttributesResponse&) = default;
};

struct MmsReadRequest final {
    std::uint32_t invoke_id{};
    bool specification_with_result{};
    std::vector<MmsObjectName> variables;

    friend bool operator==(const MmsReadRequest&, const MmsReadRequest&) = default;
};

struct MmsReadAccessResult final {
    std::optional<MmsDataValue> value;
    std::optional<std::uint32_t> failure_code;

    [[nodiscard]] bool success() const noexcept { return value.has_value(); }
};

struct MmsReadResponse final {
    std::uint32_t invoke_id{};
    std::vector<MmsReadAccessResult> results;
};

struct MmsWriteRequest final {
    std::uint32_t invoke_id{};
    std::vector<MmsObjectName> variables;
    std::vector<MmsDataValue> values;
};

struct MmsWriteAccessResult final {
    bool success{};
    std::optional<std::uint32_t> failure_code;

    friend bool operator==(const MmsWriteAccessResult&,
                           const MmsWriteAccessResult&) = default;
};

struct MmsWriteResponse final {
    std::uint32_t invoke_id{};
    std::vector<MmsWriteAccessResult> results;

    [[nodiscard]] bool all_success() const noexcept;
};

class MmsServiceCodec final {
public:
    static constexpr std::size_t maximum_identifier_bytes = 1'024U;
    static constexpr std::size_t maximum_identifiers = 4'096U;
    static constexpr std::size_t maximum_variables = 4'096U;
    static constexpr std::size_t maximum_type_depth = 24U;
    static constexpr std::size_t maximum_type_components = 4'096U;

    [[nodiscard]] static std::vector<std::uint8_t> encode_object_name(
        const MmsObjectName& name);
    [[nodiscard]] static MmsObjectName decode_object_name(
        std::span<const std::uint8_t> encoded_object_name);

    [[nodiscard]] static std::vector<std::uint8_t> encode_get_name_list_request_pdu(
        const MmsGetNameListRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_get_name_list_request_p_data(
        const MmsGetNameListRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsGetNameListRequest decode_get_name_list_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_get_name_list_response_pdu(
        const MmsGetNameListResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_get_name_list_response_p_data(
        const MmsGetNameListResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsGetNameListResponse decode_get_name_list_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);

    [[nodiscard]] static std::vector<std::uint8_t> encode_type_specification(
        const MmsTypeSpecification& type);
    [[nodiscard]] static MmsTypeSpecification decode_type_specification(
        std::span<const std::uint8_t> encoded_type);

    [[nodiscard]] static std::vector<std::uint8_t>
        encode_variable_access_attributes_request_pdu(
            const MmsVariableAccessAttributesRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t>
        encode_variable_access_attributes_request_p_data(
            const MmsVariableAccessAttributesRequest& request,
            std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsVariableAccessAttributesRequest
        decode_variable_access_attributes_request(
            std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t>
        encode_variable_access_attributes_response_pdu(
            const MmsVariableAccessAttributesResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t>
        encode_variable_access_attributes_response_p_data(
            const MmsVariableAccessAttributesResponse& response,
            std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsVariableAccessAttributesResponse
        decode_variable_access_attributes_response(
            std::span<const std::uint8_t> presentation_or_mms_payload,
            std::optional<std::uint32_t> expected_invoke_id = std::nullopt);

    [[nodiscard]] static std::vector<std::uint8_t> encode_read_request_pdu(
        const MmsReadRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_read_request_p_data(
        const MmsReadRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsReadRequest decode_read_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_read_response_pdu(
        const MmsReadResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_read_response_p_data(
        const MmsReadResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsReadResponse decode_read_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);

    [[nodiscard]] static std::vector<std::uint8_t> encode_write_request_pdu(
        const MmsWriteRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_write_request_p_data(
        const MmsWriteRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsWriteRequest decode_write_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_write_response_pdu(
        const MmsWriteResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_write_response_p_data(
        const MmsWriteResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsWriteResponse decode_write_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);
};

} // namespace ar::iec61850::mms
