// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kInt32Type{0x85U, 0x01U, 0x20U};

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

constexpr std::array<std::uint8_t, 3U> kLd0{0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 2U> kR1{0x52U, 0x31U};
constexpr std::array<std::uint8_t, 2U> kM1{0x4DU, 0x31U};
constexpr std::array<std::uint8_t, 3U> kBooleanData{0x83U, 0x01U, 0xFFU};
constexpr std::array<std::uint8_t, 3U> kIntData{0x85U, 0x01U, 0x2AU};

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
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
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    const auto value = *static_cast<const std::uint8_t*>(context);
    if (value > 0x7FU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x85U;
    destination[1] = 0x01U;
    destination[2] = value;
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace

int main() {
    const bool relay_state = true;
    const std::uint8_t meter_value = 42U;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType, read_boolean, &relay_state, false},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kInt32Type, read_small_int32, &meter_value, false}};
    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid()) {
        return 1;
    }

    mms::MmsVariableAccessAttributesRequestView attributes_request;
    if (!mms::MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
            kAttributesRequest, attributes_request) ||
        attributes_request.invoke_id != 11U ||
        attributes_request.name.kind != mms::MmsObjectNameViewKind::domain_specific ||
        !matches(attributes_request.name.domain, kLd0) ||
        !matches(attributes_request.name.item, kR1)) {
        return 2;
    }
    const auto* relay = table.find(attributes_request.name);
    if (relay == nullptr || relay->type_specification.data() != kBooleanType.data()) {
        return 3;
    }

    std::array<std::uint8_t, 256U> buffer{};
    const auto attributes_encoded =
        mms::MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
            attributes_request.invoke_id,
            relay->mms_deletable,
            relay->type_specification,
            buffer);
    if (!attributes_encoded.success() ||
        attributes_encoded.bytes_written != kAttributesResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(attributes_encoded.bytes_written),
            kAttributesResponse)) {
        return 4;
    }

    mms::MmsVariableAccessAttributesResponseView attributes_response;
    if (!mms::MmsServiceSpanCodec::try_decode_variable_access_attributes_response(
            kAttributesResponse, attributes_response) ||
        attributes_response.invoke_id != 11U || attributes_response.mms_deletable ||
        !matches(attributes_response.type_specification, kBooleanType)) {
        return 5;
    }

    mms::MmsReadRequestView read_request;
    if (!mms::MmsServiceSpanCodec::try_decode_read_request(kReadRequest, read_request) ||
        read_request.invoke_id != 12U || read_request.specification_with_result ||
        read_request.variable_count != 2U) {
        return 6;
    }

    std::array<const mms::MmsStaticObjectEntry*, 2U> resolved{};
    std::size_t resolved_count = 0U;
    if (!table.try_resolve_read_request(read_request, resolved, resolved_count) ||
        resolved_count != 2U || resolved[0] != &objects[0] || resolved[1] != &objects[1]) {
        return 7;
    }

    mms::MmsObjectNameView variable;
    if (!read_request.try_variable(0U, variable) || !matches(variable.item, kR1) ||
        !read_request.try_variable(1U, variable) || !matches(variable.item, kM1) ||
        read_request.try_variable(2U, variable)) {
        return 8;
    }

    std::array<std::uint8_t, 8U> relay_data{};
    std::array<std::uint8_t, 8U> meter_data{};
    const auto relay_read = resolved[0]->read(resolved[0]->context, relay_data);
    const auto meter_read = resolved[1]->read(resolved[1]->context, meter_data);
    if (!relay_read.success() || !meter_read.success() ||
        !matches(
            std::span<const std::uint8_t>{relay_data}.first(relay_read.bytes_written),
            kBooleanData) ||
        !matches(
            std::span<const std::uint8_t>{meter_data}.first(meter_read.bytes_written),
            kIntData)) {
        return 9;
    }

    const std::array<mms::MmsReadAccessResultInput, 2U> results{
        mms::MmsReadAccessResultInput{
            true,
            std::span<const std::uint8_t>{relay_data}.first(relay_read.bytes_written),
            0U},
        mms::MmsReadAccessResultInput{
            true,
            std::span<const std::uint8_t>{meter_data}.first(meter_read.bytes_written),
            0U}};
    const auto read_encoded = mms::MmsServiceSpanCodec::encode_read_response_into(
        read_request.invoke_id, results, buffer);
    if (!read_encoded.success() || read_encoded.bytes_written != kReadResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(read_encoded.bytes_written),
            kReadResponse)) {
        return 10;
    }

    mms::MmsReadResponseView read_response;
    if (!mms::MmsServiceSpanCodec::try_decode_read_response(
            kReadResponse, read_response) ||
        read_response.invoke_id != 12U || read_response.result_count != 2U) {
        return 11;
    }
    mms::MmsReadAccessResultView access;
    if (!read_response.try_result(0U, access) || !access.success ||
        !matches(access.encoded_data, kBooleanData) ||
        !read_response.try_result(1U, access) || !access.success ||
        !matches(access.encoded_data, kIntData) ||
        read_response.try_result(2U, access)) {
        return 12;
    }

    // Failure results remain bounded and explicit; no exception or owning object
    // graph is required to return an MMS DataAccessError.
    const std::array<mms::MmsReadAccessResultInput, 1U> failed{
        mms::MmsReadAccessResultInput{false, {}, 10U}};
    const auto failed_encoded = mms::MmsServiceSpanCodec::encode_read_response_into(
        13U, failed, buffer);
    if (!failed_encoded.success() ||
        !mms::MmsServiceSpanCodec::try_decode_read_response(
            std::span<const std::uint8_t>{buffer}.first(failed_encoded.bytes_written),
            read_response) ||
        read_response.result_count != 1U ||
        !read_response.try_result(0U, access) || access.success || access.failure_code != 10U) {
        return 13;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto invoke = iteration & 0x7FFFU;
        const auto encoded = mms::MmsServiceSpanCodec::encode_read_response_into(
            invoke, results, buffer);
        mms::MmsReadResponseView decoded;
        if (!encoded.success() ||
            !mms::MmsServiceSpanCodec::try_decode_read_response(
                std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
                decoded) ||
            decoded.invoke_id != invoke || decoded.result_count != 2U) {
            return 14;
        }
    }

    const std::array<mms::MmsStaticObjectEntry, 2U> duplicates{
        objects[0], objects[0]};
    if (mms::MmsStaticObjectTable{duplicates}.valid()) {
        return 15;
    }

    return 0;
}
