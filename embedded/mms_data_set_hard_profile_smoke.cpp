// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_set_span.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
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
constexpr std::array<std::uint8_t, 27U> kAttributesRequest{
    0xA0U, 0x19U, 0x02U, 0x01U, 0x01U,
    0xACU, 0x14U, 0xA1U, 0x12U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x0BU, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U,
    0x45U, 0x76U, 0x65U, 0x6EU, 0x74U, 0x73U};
constexpr std::array<std::uint8_t, 42U> kAttributesResponse{
    0xA1U, 0x28U, 0x02U, 0x01U, 0x01U,
    0xACU, 0x23U, 0x80U, 0x01U, 0x00U,
    0xA1U, 0x1EU,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x4DU, 0x31U};
constexpr std::array<std::uint8_t, 19U> kDataSetNameListRequest{
    0xA0U, 0x11U, 0x02U, 0x01U, 0x02U,
    0xA1U, 0x0CU,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x02U,
    0xA1U, 0x05U, 0x81U, 0x03U, 0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 25U> kDataSetNameListResponse{
    0xA1U, 0x17U, 0x02U, 0x01U, 0x02U,
    0xA1U, 0x12U, 0xA0U, 0x0DU,
    0x1AU, 0x0BU, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U,
    0x45U, 0x76U, 0x65U, 0x6EU, 0x74U, 0x73U,
    0x81U, 0x01U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kLd0{0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 2U> kR1{0x52U, 0x31U};
constexpr std::array<std::uint8_t, 2U> kM1{0x4DU, 0x31U};
constexpr std::array<std::uint8_t, 11U> kEvents{
    0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U,
    0x45U, 0x76U, 0x65U, 0x6EU, 0x74U, 0x73U};

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
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace

int main() {
    mms::MmsNamedVariableListAttributesRequestView request;
    if (!mms::MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_request(
            kAttributesRequest, request) ||
        request.invoke_id != 1U ||
        request.name.kind != mms::MmsObjectNameViewKind::domain_specific ||
        !matches(request.name.domain, kLd0) || !matches(request.name.item, kEvents)) {
        return 1;
    }

    const std::array<mms::MmsNamedVariableListMemberInput, 2U> encoded_members{
        mms::MmsNamedVariableListMemberInput{"LD0", "R1"},
        mms::MmsNamedVariableListMemberInput{"LD0", "M1"}};
    std::array<std::uint8_t, 512U> buffer{};
    const auto encoded =
        mms::MmsDataSetSpanCodec::encode_get_named_variable_list_attributes_response_into(
            1U, false, encoded_members, buffer);
    if (!encoded.success() || encoded.bytes_written != kAttributesResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
            kAttributesResponse)) {
        return 2;
    }

    mms::MmsNamedVariableListAttributesResponseView response;
    if (!mms::MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_response(
            kAttributesResponse, response) ||
        response.invoke_id != 1U || response.mms_deletable || response.member_count != 2U) {
        return 3;
    }
    mms::MmsObjectNameView member;
    if (!response.try_member(0U, member) ||
        !matches(member.domain, kLd0) || !matches(member.item, kR1) ||
        !response.try_member(1U, member) ||
        !matches(member.domain, kLd0) || !matches(member.item, kM1) ||
        response.try_member(2U, member)) {
        return 4;
    }

    const bool relay_state = true;
    const bool meter_state = false;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType, read_boolean, &relay_state, false},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kBooleanType, read_boolean, &meter_state, false}};
    const mms::MmsStaticObjectTable object_table{objects};

    const std::array<mms::MmsStaticDataSetMember, 2U> event_members{
        mms::MmsStaticDataSetMember{"LD0", "R1"},
        mms::MmsStaticDataSetMember{"LD0", "M1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", event_members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid() || !data_set_table.valid_against(object_table)) {
        return 5;
    }

    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_set_table};
    std::array<std::uint8_t, 256U> workspace{};
    auto dispatched = dispatcher.dispatch(kAttributesRequest, buffer, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_named_variable_list_attributes ||
        dispatched.invoke_id != 1U || dispatched.bytes_written != kAttributesResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(dispatched.bytes_written),
            kAttributesResponse)) {
        return 6;
    }

    dispatched = dispatcher.dispatch(kDataSetNameListRequest, buffer, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_name_list ||
        dispatched.bytes_written != kDataSetNameListResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(dispatched.bytes_written),
            kDataSetNameListResponse)) {
        return 7;
    }

    mms::MmsGetNameListResponseView names;
    std::span<const std::uint8_t> identifier;
    if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
            std::span<const std::uint8_t>{buffer}.first(dispatched.bytes_written), names) ||
        names.identifier_count != 1U || names.more_follows ||
        !names.try_identifier(0U, identifier) || !matches(identifier, kEvents)) {
        return 8;
    }

    std::array<std::uint8_t, 8U> tiny{};
    dispatched = dispatcher.dispatch(kAttributesRequest, tiny, workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::response_buffer_too_small ||
        dispatched.required_bytes != kAttributesResponse.size()) {
        return 9;
    }

    auto missing_request = kAttributesRequest;
    missing_request[26] = 0x58U;
    dispatched = dispatcher.dispatch(missing_request, buffer, workspace);
    if (dispatched.status != mms::MmsStaticDispatchStatus::object_not_found) {
        return 10;
    }

    const std::array<mms::MmsStaticDataSetMember, 1U> invalid_members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> invalid_data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Broken", invalid_members, false}};
    const mms::MmsStaticDataSetTable invalid_table{invalid_data_sets};
    if (!invalid_table.valid() || invalid_table.valid_against(object_table)) {
        return 11;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto invoke = iteration & 0x7FFFU;
        const auto repeated =
            mms::MmsDataSetSpanCodec::encode_get_named_variable_list_attributes_response_into(
                invoke, (iteration & 1U) != 0U, encoded_members, buffer);
        mms::MmsNamedVariableListAttributesResponseView decoded;
        if (!repeated.success() ||
            !mms::MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_response(
                std::span<const std::uint8_t>{buffer}.first(repeated.bytes_written), decoded) ||
            decoded.invoke_id != invoke || decoded.member_count != 2U ||
            decoded.mms_deletable != ((iteration & 1U) != 0U)) {
            return 12;
        }
    }

    const std::array<mms::MmsNamedVariableListMemberInput, 1U> invalid_name{
        mms::MmsNamedVariableListMemberInput{"LD0", std::string_view{"A\0B", 3U}}};
    if (mms::MmsDataSetSpanCodec::encode_get_named_variable_list_attributes_response_into(
            1U, false, invalid_name, buffer).success()) {
        return 13;
    }

    return 0;
}
