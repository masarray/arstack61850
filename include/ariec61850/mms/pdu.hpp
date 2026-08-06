// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MmsPduKind : std::uint8_t {
    unknown,
    confirmed_request,
    confirmed_response,
    confirmed_error,
    unconfirmed,
    reject,
    cancel_request,
    cancel_response,
    cancel_error,
    initiate_request,
    initiate_response,
    initiate_error,
    conclude_request,
    conclude_response,
    conclude_error,
};

enum class MmsConfirmedService : std::int32_t {
    unknown = -1,
    get_name_list = 1,
    identify = 2,
    read = 4,
    write = 5,
    get_variable_access_attributes = 6,
    get_named_variable_list_attributes = 12,
};

struct MmsInitiateDetail final {
    std::uint32_t version_number{1U};
    std::vector<std::uint8_t> parameter_support_options{0x05U, 0xF1U, 0x00U};
    std::vector<std::uint8_t> services_supported_calling{
        0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
        0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

    friend bool operator==(const MmsInitiateDetail&, const MmsInitiateDetail&) = default;
};

struct MmsInitiateRequest final {
    std::uint32_t proposed_maximum_mms_pdu_size{65'000U};
    std::uint32_t proposed_maximum_outstanding_calling{10U};
    std::uint32_t proposed_maximum_outstanding_called{10U};
    std::uint32_t proposed_data_structure_nesting_level{5U};
    MmsInitiateDetail detail;

    friend bool operator==(const MmsInitiateRequest&, const MmsInitiateRequest&) = default;
};

struct MmsInitiateResponse final {
    std::uint32_t negotiated_maximum_mms_pdu_size{65'000U};
    std::uint32_t negotiated_maximum_outstanding_calling{10U};
    std::uint32_t negotiated_maximum_outstanding_called{10U};
    std::uint32_t negotiated_data_structure_nesting_level{5U};
    MmsInitiateDetail detail;

    friend bool operator==(const MmsInitiateResponse&, const MmsInitiateResponse&) = default;
};

struct MmsConfirmedRequest final {
    std::uint32_t invoke_id{};
    std::int32_t service_tag{-1};
    bool service_constructed{true};
    std::vector<std::uint8_t> service_value;

    [[nodiscard]] MmsConfirmedService service() const noexcept;
    friend bool operator==(const MmsConfirmedRequest&, const MmsConfirmedRequest&) = default;
};

struct MmsConfirmedResponse final {
    std::uint32_t invoke_id{};
    std::int32_t service_tag{-1};
    bool service_constructed{true};
    std::vector<std::uint8_t> service_value;

    [[nodiscard]] MmsConfirmedService service() const noexcept;
    friend bool operator==(const MmsConfirmedResponse&, const MmsConfirmedResponse&) = default;
};

struct MmsConfirmedError final {
    std::uint32_t invoke_id{};
    std::int32_t error_class_tag{};
    std::uint32_t error_value{};

    friend bool operator==(const MmsConfirmedError&, const MmsConfirmedError&) = default;
};

struct MmsPduEnvelope final {
    MmsPduKind kind{MmsPduKind::unknown};
    std::optional<std::uint32_t> invoke_id;
    std::optional<std::int32_t> service_tag;
    bool information_report{};
    std::vector<std::uint8_t> mms_payload;

    [[nodiscard]] bool confirmed_result() const noexcept;
    [[nodiscard]] bool matches_invoke(std::uint32_t expected) const noexcept;
};

class MmsPduCodec final {
public:
    static constexpr std::size_t maximum_pdu_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_bit_string_bytes = 256U;
    static constexpr std::uint32_t maximum_invoke_id = 0x7FFF'FFFFU;

    [[nodiscard]] static MmsInitiateRequest default_initiate_request();
    [[nodiscard]] static MmsInitiateResponse default_initiate_response();

    [[nodiscard]] static std::vector<std::uint8_t> encode_initiate_request(
        const MmsInitiateRequest& request);
    [[nodiscard]] static MmsInitiateRequest decode_initiate_request(
        std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool try_decode_initiate_request(
        std::span<const std::uint8_t> bytes,
        MmsInitiateRequest& request,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static std::vector<std::uint8_t> encode_initiate_response(
        const MmsInitiateResponse& response);
    [[nodiscard]] static MmsInitiateResponse decode_initiate_response(
        std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool try_decode_initiate_response(
        std::span<const std::uint8_t> bytes,
        MmsInitiateResponse& response,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static std::vector<std::uint8_t> encode_confirmed_request(
        const MmsConfirmedRequest& request);
    [[nodiscard]] static MmsConfirmedRequest decode_confirmed_request(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_confirmed_response(
        const MmsConfirmedResponse& response);
    [[nodiscard]] static MmsConfirmedResponse decode_confirmed_response(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_confirmed_error(
        const MmsConfirmedError& error);
    [[nodiscard]] static MmsConfirmedError decode_confirmed_error(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static MmsPduEnvelope decode_envelope(
        std::span<const std::uint8_t> presentation_or_mms_payload);
    [[nodiscard]] static bool try_decode_envelope(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        MmsPduEnvelope& envelope,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static std::vector<std::uint8_t> extract_mms_payload(
        std::span<const std::uint8_t> presentation_or_mms_payload);
    [[nodiscard]] static std::vector<std::uint8_t> wrap_p_data(
        std::span<const std::uint8_t> mms_payload,
        std::uint32_t presentation_context_id = 3U);
};

} // namespace ar::iec61850::mms
