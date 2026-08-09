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

constexpr std::array<std::uint8_t, 10U> kWriteFailureResponse{
    0xA1U, 0x08U, 0x02U, 0x01U, 0x0FU,
    0xA5U, 0x03U, 0x80U, 0x01U, 0x03U};

constexpr std::array<std::uint8_t, 3U> kLd0{0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 2U> kR1{0x52U, 0x31U};
constexpr std::array<std::uint8_t, 2U> kM1{0x4DU, 0x31U};
constexpr std::array<std::uint8_t, 3U> kFalseData{0x83U, 0x01U, 0x00U};

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
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType,
            read_boolean, &relay_state, false,
            write_boolean, &relay_state},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kInt32Type,
            read_small_int32, &meter_value, false}};
    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid() || !objects[0].writable() || objects[1].writable()) {
        return 1;
    }

    mms::MmsWriteRequestView request;
    if (!mms::MmsServiceSpanCodec::try_decode_write_request(kWriteRequest, request) ||
        request.invoke_id != 14U || request.variable_count != 1U || request.data_count != 1U) {
        return 2;
    }
    mms::MmsObjectNameView name;
    std::span<const std::uint8_t> value;
    if (!request.try_variable(0U, name) ||
        name.kind != mms::MmsObjectNameViewKind::domain_specific ||
        !matches(name.domain, kLd0) || !matches(name.item, kR1) ||
        !request.try_value(0U, value) || !matches(value, kFalseData) ||
        request.try_variable(1U, name) || request.try_value(1U, value)) {
        return 3;
    }

    std::array<const mms::MmsStaticObjectEntry*, 1U> resolved{};
    std::size_t resolved_count = 0U;
    if (!table.try_resolve_write_request(request, resolved, resolved_count) ||
        resolved_count != 1U || resolved[0] != &objects[0] || !resolved[0]->writable()) {
        return 4;
    }
    if (!request.try_value(0U, value)) {
        return 5;
    }
    const auto write_result = resolved[0]->write(resolved[0]->write_context, value);
    if (!write_result.success || relay_state) {
        return 6;
    }

    std::array<std::uint8_t, 128U> buffer{};
    const std::array<mms::MmsWriteAccessResultInput, 1U> success{
        mms::MmsWriteAccessResultInput{true, 0U}};
    const auto success_encoded = mms::MmsServiceSpanCodec::encode_write_response_into(
        request.invoke_id, success, buffer);
    if (!success_encoded.success() ||
        success_encoded.bytes_written != kWriteSuccessResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(success_encoded.bytes_written),
            kWriteSuccessResponse)) {
        return 7;
    }

    mms::MmsWriteResponseView response;
    mms::MmsWriteAccessResultView access;
    if (!mms::MmsServiceSpanCodec::try_decode_write_response(
            kWriteSuccessResponse, response) ||
        response.invoke_id != 14U || response.result_count != 1U ||
        !response.try_result(0U, access) || !access.success ||
        response.try_result(1U, access)) {
        return 8;
    }

    auto read_only_request = kWriteRequest;
    read_only_request[4] = 0x0FU;
    read_only_request[22] = 0x4DU;
    if (!mms::MmsServiceSpanCodec::try_decode_write_request(
            read_only_request, request) ||
        !table.try_resolve_write_request(request, resolved, resolved_count) ||
        resolved_count != 1U || resolved[0] != &objects[1] || resolved[0]->writable()) {
        return 9;
    }

    const std::array<mms::MmsWriteAccessResultInput, 1U> denied{
        mms::MmsWriteAccessResultInput{false, 3U}};
    const auto denied_encoded = mms::MmsServiceSpanCodec::encode_write_response_into(
        request.invoke_id, denied, buffer);
    if (!denied_encoded.success() ||
        denied_encoded.bytes_written != kWriteFailureResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(denied_encoded.bytes_written),
            kWriteFailureResponse) ||
        !mms::MmsServiceSpanCodec::try_decode_write_response(
            kWriteFailureResponse, response) ||
        !response.try_result(0U, access) || access.success || access.failure_code != 3U) {
        return 10;
    }

    auto bad_type_request = kWriteRequest;
    bad_type_request[26] = 0x85U;
    if (!mms::MmsServiceSpanCodec::try_decode_write_request(
            bad_type_request, request) ||
        !table.try_resolve_write_request(request, resolved, resolved_count) ||
        resolved[0] == nullptr || !resolved[0]->writable() ||
        !request.try_value(0U, value)) {
        return 11;
    }
    const auto rejected = resolved[0]->write(resolved[0]->write_context, value);
    if (rejected.success || rejected.failure_code != 7U || relay_state) {
        return 12;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto invoke = iteration & 0x7FFFU;
        const auto encoded = mms::MmsServiceSpanCodec::encode_write_response_into(
            invoke, (iteration & 1U) == 0U ? success : denied, buffer);
        mms::MmsWriteResponseView decoded;
        if (!encoded.success() ||
            !mms::MmsServiceSpanCodec::try_decode_write_response(
                std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
                decoded) ||
            decoded.invoke_id != invoke || decoded.result_count != 1U ||
            !decoded.try_result(0U, access) ||
            access.success != ((iteration & 1U) == 0U)) {
            return 13;
        }
    }

    return 0;
}
