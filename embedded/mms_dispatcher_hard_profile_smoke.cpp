// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_dispatcher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kInt32Type{0x85U, 0x01U, 0x20U};

constexpr std::array<std::uint8_t, 16U> kDomainListRequest{
    0xA0U, 0x0EU, 0x02U, 0x01U, 0x07U,
    0xA1U, 0x09U,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x09U,
    0xA1U, 0x02U, 0x80U, 0x00U};

constexpr std::array<std::uint8_t, 21U> kDomainContinueRequest{
    0xA0U, 0x13U, 0x02U, 0x01U, 0x08U,
    0xA1U, 0x0EU,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x09U,
    0xA1U, 0x02U, 0x80U, 0x00U,
    0x82U, 0x03U, 0x4CU, 0x44U, 0x30U};

constexpr std::array<std::uint8_t, 19U> kNamedVariableDirectoryRequest{
    0xA0U, 0x11U, 0x02U, 0x01U, 0x16U,
    0xA1U, 0x0CU,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x00U,
    0xA1U, 0x05U, 0x81U, 0x03U, 0x4CU, 0x44U, 0x48U};

constexpr std::array<std::uint8_t, 17U> kDomainFirstResponse{
    0xA1U, 0x0FU, 0x02U, 0x01U, 0x07U,
    0xA1U, 0x0AU, 0xA0U, 0x05U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x81U, 0x01U, 0xFFU};

constexpr std::array<std::uint8_t, 17U> kDomainSecondResponse{
    0xA1U, 0x0FU, 0x02U, 0x01U, 0x08U,
    0xA1U, 0x0AU, 0xA0U, 0x05U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x31U,
    0x81U, 0x01U, 0x00U};

constexpr std::array<std::uint8_t, 20U> kAttributesRequest{
    0xA0U, 0x12U, 0x02U, 0x01U, 0x0BU,
    0xA6U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U};

constexpr std::array<std::uint8_t, 14U> kAttributesResponse{
    0xA1U, 0x0CU, 0x02U, 0x01U, 0x0BU,
    0xA6U, 0x07U, 0x80U, 0x01U, 0x00U,
    0xA2U, 0x02U, 0x83U, 0x00U};

constexpr std::array<std::uint8_t, 41U> kReadRequest{
    0xA0U, 0x27U, 0x02U, 0x01U, 0x0CU,
    0xA4U, 0x22U, 0xA1U, 0x20U, 0xA0U, 0x1EU,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x4DU, 0x31U};

constexpr std::array<std::uint8_t, 15U> kReadResponse{
    0xA1U, 0x0DU, 0x02U, 0x01U, 0x0CU,
    0xA4U, 0x08U, 0xA1U, 0x06U,
    0x83U, 0x01U, 0xFFU,
    0x85U, 0x01U, 0x2AU};

constexpr std::array<std::uint8_t, 29U> kWriteRequest{
    0xA0U, 0x1BU, 0x02U, 0x01U, 0x0EU,
    0xA5U, 0x16U,
    0xA0U, 0x0FU,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U,
    0xA0U, 0x03U, 0x83U, 0x01U, 0x00U};

constexpr std::array<std::uint8_t, 9U> kWriteSuccessResponse{
    0xA1U, 0x07U, 0x02U, 0x01U, 0x0EU,
    0xA5U, 0x02U, 0x81U, 0x00U};

constexpr std::array<std::uint8_t, 10U> kWriteDeniedResponse{
    0xA1U, 0x08U, 0x02U, 0x01U, 0x0FU,
    0xA5U, 0x03U, 0x80U, 0x01U, 0x03U};

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] bool identifier_equals(
    const std::span<const std::uint8_t> identifier,
    const std::string_view expected) noexcept {
    if (identifier.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < identifier.size(); ++index) {
        if (identifier[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = *static_cast<const bool*>(context);
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult read_small_int32(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr || destination.size() < required) {
        return context == nullptr
            ? wire::EncodeResult{wire::EncodeStatus::value_out_of_range, 0U, required}
            : wire::EncodeResult{wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = *static_cast<const std::uint8_t*>(context);
    if (value > 0x7FU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    destination[0] = 0x85U;
    destination[1] = 0x01U;
    destination[2] = value;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] mms::MmsStaticWriteResult write_boolean(
    void* context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    if (context == nullptr || encoded_data.size() != 3U ||
        encoded_data[0] != 0x83U || encoded_data[1] != 0x01U ||
        (encoded_data[2] != 0x00U && encoded_data[2] != 0xFFU)) {
        return {false, 7U};
    }
    *static_cast<bool*>(context) = encoded_data[2] != 0x00U;
    return {true, 0U};
}

} // namespace

int main() {
    bool relay_state = true;
    const std::uint8_t meter_value = 42U;
    const bool remote_state = false;
    const std::array<mms::MmsStaticObjectEntry, 3U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType,
            read_boolean, &relay_state, false,
            write_boolean, &relay_state},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kInt32Type,
            read_small_int32, &meter_value, false},
        mms::MmsStaticObjectEntry{
            "LD1", "X1", kBooleanType,
            read_boolean, &remote_state, false}};
    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid()) {
        return 1;
    }

    const mms::MmsStaticDispatchPolicy policy{
        1U,
        1U,
        10U,
        3U,
        10U};
    const mms::MmsStaticApplicationDispatcher dispatcher{table, policy};
    std::array<std::uint8_t, 512U> response{};
    std::array<std::uint8_t, 512U> workspace{};

    auto dispatched = dispatcher.dispatch(kDomainListRequest, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_name_list ||
        dispatched.invoke_id != 7U ||
        dispatched.bytes_written != kDomainFirstResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kDomainFirstResponse)) {
        return 2;
    }

    dispatched = dispatcher.dispatch(kDomainContinueRequest, response, workspace);
    if (!dispatched.success() || dispatched.invoke_id != 8U ||
        dispatched.bytes_written != kDomainSecondResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kDomainSecondResponse)) {
        return 3;
    }

    mms::MmsConfirmedPduView attributes_view;
    if (!mms::MmsPduSpanCodec::try_decode_confirmed_request_view(
            kAttributesRequest, attributes_view)) {
        return 4;
    }
    dispatched = dispatcher.dispatch(attributes_view, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_variable_access_attributes ||
        dispatched.bytes_written != kAttributesResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kAttributesResponse)) {
        return 5;
    }

    dispatched = dispatcher.dispatch(kReadRequest, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::read ||
        dispatched.bytes_written != kReadResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kReadResponse)) {
        return 6;
    }

    std::array<std::uint8_t, 2U> tiny_workspace{};
    dispatched = dispatcher.dispatch(kReadRequest, response, tiny_workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::workspace_too_small ||
        dispatched.required_bytes < 3U) {
        return 7;
    }

    auto missing_read = kReadRequest;
    missing_read[39] = 0x5AU;
    dispatched = dispatcher.dispatch(missing_read, response, workspace);
    if (!dispatched.success()) {
        return 8;
    }
    mms::MmsReadResponseView read_response;
    mms::MmsReadAccessResultView read_result;
    if (!mms::MmsServiceSpanCodec::try_decode_read_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            read_response) ||
        read_response.result_count != 2U ||
        !read_response.try_result(0U, read_result) || !read_result.success ||
        !read_response.try_result(1U, read_result) || read_result.success ||
        read_result.failure_code != 10U) {
        return 9;
    }

    dispatched = dispatcher.dispatch(kWriteRequest, response, workspace);
    if (!dispatched.success() || relay_state ||
        dispatched.service != mms::MmsWireConfirmedService::write ||
        dispatched.bytes_written != kWriteSuccessResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kWriteSuccessResponse)) {
        return 10;
    }

    auto read_only_write = kWriteRequest;
    read_only_write[4] = 0x0FU;
    read_only_write[22] = 0x4DU;
    dispatched = dispatcher.dispatch(read_only_write, response, workspace);
    if (!dispatched.success() ||
        dispatched.bytes_written != kWriteDeniedResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            kWriteDeniedResponse) ||
        meter_value != 42U) {
        return 11;
    }

    std::array<std::uint8_t, 8U> tiny_response{};
    dispatched = dispatcher.dispatch(kDomainListRequest, tiny_response, workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::response_buffer_too_small ||
        dispatched.required_bytes != kDomainFirstResponse.size()) {
        return 12;
    }

    auto unknown_continuation = kDomainContinueRequest;
    unknown_continuation[18] = 0x5AU;
    unknown_continuation[19] = 0x5AU;
    unknown_continuation[20] = 0x5AU;
    dispatched = dispatcher.dispatch(unknown_continuation, response, workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::object_not_found) {
        return 13;
    }

    std::array<std::uint8_t, 32U> identify_request{};
    const auto identify_encoded = mms::MmsPduSpanCodec::encode_confirmed_request_into(
        21U,
        static_cast<std::int32_t>(mms::MmsWireConfirmedService::identify),
        false,
        {},
        identify_request);
    if (!identify_encoded.success()) {
        return 14;
    }
    dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{identify_request}.first(identify_encoded.bytes_written),
        response,
        workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::unsupported_service ||
        dispatched.invoke_id != 21U) {
        return 15;
    }

    constexpr std::array<std::uint8_t, 1U> malformed{0x00U};
    dispatched = dispatcher.dispatch(malformed, response, workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::malformed_request ||
        dispatched.service != mms::MmsWireConfirmedService::unknown) {
        return 16;
    }

    // ARIEC61850 IED simulator parity: when a root Logical Node object exists,
    // GetNameList(NamedVariable) advertises the root and leaves its flattened
    // FC/DO/DA aliases for Read/Write lookup and TypeSpecification traversal.
    // A flat-only generic MMS item remains discoverable for compatibility.
    bool hierarchy_value = true;
    const std::array<mms::MmsStaticObjectEntry, 5U> hierarchy_objects{
        mms::MmsStaticObjectEntry{
            "LDH", "LLN0", kBooleanType,
            read_boolean, &hierarchy_value, false},
        mms::MmsStaticObjectEntry{
            "LDH", "LLN0$ST$Mod$stVal", kBooleanType,
            read_boolean, &hierarchy_value, false},
        mms::MmsStaticObjectEntry{
            "LDH", "GGIO1", kBooleanType,
            read_boolean, &hierarchy_value, false},
        mms::MmsStaticObjectEntry{
            "LDH", "GGIO1$ST$Ind1$stVal", kBooleanType,
            read_boolean, &hierarchy_value, false},
        mms::MmsStaticObjectEntry{
            "LDH", "Orphan$ST$stVal", kBooleanType,
            read_boolean, &hierarchy_value, false}};
    const mms::MmsStaticObjectTable hierarchy_table{hierarchy_objects};
    const mms::MmsStaticDispatchPolicy hierarchy_policy{
        8U,
        1U,
        10U,
        3U,
        10U};
    const mms::MmsStaticApplicationDispatcher hierarchy_dispatcher{
        hierarchy_table,
        hierarchy_policy};
    if (!hierarchy_table.valid()) {
        return 17;
    }

    dispatched = hierarchy_dispatcher.dispatch(
        kNamedVariableDirectoryRequest,
        response,
        workspace);
    mms::MmsGetNameListResponseView hierarchy_directory;
    if (!dispatched.success() ||
        !mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            hierarchy_directory) ||
        hierarchy_directory.invoke_id != 22U ||
        hierarchy_directory.identifier_count != 3U ||
        hierarchy_directory.more_follows) {
        return 18;
    }

    std::span<const std::uint8_t> identifier;
    if (!hierarchy_directory.try_identifier(0U, identifier) ||
        !identifier_equals(identifier, "LLN0") ||
        !hierarchy_directory.try_identifier(1U, identifier) ||
        !identifier_equals(identifier, "GGIO1") ||
        !hierarchy_directory.try_identifier(2U, identifier) ||
        !identifier_equals(identifier, "Orphan$ST$stVal")) {
        return 19;
    }

    for (std::uint32_t iteration = 0U; iteration < 20'000U; ++iteration) {
        relay_state = true;
        const auto read = dispatcher.dispatch(kReadRequest, response, workspace);
        const auto write = dispatcher.dispatch(kWriteRequest, response, workspace);
        if (!read.success() || !write.success() || relay_state) {
            return 20;
        }
    }

    return 0;
}
