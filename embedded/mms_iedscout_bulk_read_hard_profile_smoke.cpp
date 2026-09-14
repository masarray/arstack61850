// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::size_t kIedScoutBulkVariables = 78U;
constexpr std::uint32_t kInvokeId = 33U;
constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kBooleanData{0x83U, 0x01U, 0xFFU};

// VariableSpecification for LD0/R1, matching the normal MMS domain-specific
// named-variable encoding used by the bounded server profile.
constexpr std::array<std::uint8_t, 15U> kVariableDefinition{
    0x30U, 0x0DU,
    0xA0U, 0x0BU,
    0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U};

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

[[nodiscard]] std::size_t build_bulk_read_request(
    const std::span<std::uint8_t> destination) noexcept {
    const auto list_content = kVariableDefinition.size() * kIedScoutBulkVariables;
    const auto list_tlv = asn1::BerSpanWriter::tlv_size(0, list_content);
    if (!list_tlv) {
        return 0U;
    }
    const auto access_spec_tlv = asn1::BerSpanWriter::tlv_size(1, *list_tlv);
    const auto specification_field_tlv = asn1::BerSpanWriter::tlv_size(0, 1U);
    if (!access_spec_tlv || !specification_field_tlv) {
        return 0U;
    }
    const auto read_content = *specification_field_tlv + *access_spec_tlv;
    const auto read_tlv = asn1::BerSpanWriter::tlv_size(4, read_content);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, 1U);
    if (!read_tlv || !invoke_tlv) {
        return 0U;
    }
    const auto confirmed_content = *invoke_tlv + *read_tlv;
    const auto confirmed_tlv = asn1::BerSpanWriter::tlv_size(0, confirmed_content);
    if (!confirmed_tlv || destination.size() < *confirmed_tlv) {
        return 0U;
    }

    asn1::BerSpanWriter writer{destination.first(*confirmed_tlv)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, confirmed_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, 1U) ||
        !writer.write_byte(static_cast<std::uint8_t>(kInvokeId)) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 4, read_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, 1U) ||
        !writer.write_byte(0xFFU) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *list_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, list_content)) {
        return 0U;
    }

    for (std::size_t index = 0U; index < kIedScoutBulkVariables; ++index) {
        if (!writer.write_bytes(kVariableDefinition)) {
            return 0U;
        }
    }

    return writer.good() && writer.size() == *confirmed_tlv
        ? writer.size()
        : 0U;
}

[[nodiscard]] bool boolean_result_matches(
    const mms::MmsReadAccessResultView& result) noexcept {
    return result.success &&
        result.encoded_data.size() == kBooleanData.size() &&
        result.encoded_data[0] == kBooleanData[0] &&
        result.encoded_data[1] == kBooleanData[1] &&
        result.encoded_data[2] == kBooleanData[2];
}

} // namespace

int main() {
    static_assert(
        mms::MmsServiceSpanCodec::maximum_variables >= kIedScoutBulkVariables,
        "IEDScout bulk discovery requires at least 78 variables per Read request.");

    const bool relay_state = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType,
            read_boolean, &relay_state, false}};
    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid()) {
        return 1;
    }

    const mms::MmsStaticDispatchPolicy policy{
        32U,
        1U,
        10U,
        3U,
        10U};
    const mms::MmsStaticApplicationDispatcher dispatcher{table, policy};

    std::array<std::uint8_t, 2'048U> request{};
    const auto request_bytes = build_bulk_read_request(request);
    if (request_bytes == 0U) {
        return 2;
    }

    mms::MmsReadRequestView decoded_request;
    if (!mms::MmsServiceSpanCodec::try_decode_read_request(
            std::span<const std::uint8_t>{request}.first(request_bytes),
            decoded_request) ||
        decoded_request.invoke_id != kInvokeId ||
        !decoded_request.specification_with_result ||
        decoded_request.variable_count != kIedScoutBulkVariables) {
        return 3;
    }

    mms::MmsObjectNameView first_name;
    mms::MmsObjectNameView last_name;
    if (!decoded_request.try_variable(0U, first_name) ||
        !decoded_request.try_variable(kIedScoutBulkVariables - 1U, last_name) ||
        first_name.item.size() != 2U || last_name.item.size() != 2U ||
        first_name.item[0] != 0x52U || first_name.item[1] != 0x31U ||
        last_name.item[0] != 0x52U || last_name.item[1] != 0x31U) {
        return 4;
    }

    std::array<std::uint8_t, 4'096U> response{};
    std::array<std::uint8_t, 4'096U> workspace{};
    const auto dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(request_bytes),
        response,
        workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::read ||
        dispatched.invoke_id != kInvokeId ||
        dispatched.bytes_written == 0U) {
        return 5;
    }

    mms::MmsReadResponseView read_response;
    if (!mms::MmsServiceSpanCodec::try_decode_read_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            read_response) ||
        read_response.invoke_id != kInvokeId ||
        read_response.result_count != kIedScoutBulkVariables) {
        return 6;
    }

    mms::MmsReadAccessResultView result;
    for (std::size_t index = 0U; index < kIedScoutBulkVariables; ++index) {
        if (!read_response.try_result(index, result) ||
            !boolean_result_matches(result)) {
            return 7;
        }
    }

    return 0;
}
