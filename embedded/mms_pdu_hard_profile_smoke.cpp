// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/pdu_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 40U> kInitiateRequest{
    0xA8U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
    0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

constexpr std::array<std::uint8_t, 40U> kInitiateResponse{
    0xA9U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0x4EU, 0x08U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

constexpr std::array<std::uint8_t, 3U> kParameterSupport{
    0x05U, 0xF1U, 0x00U};
constexpr std::array<std::uint8_t, 12U> kServicesSupportedRequest{
    0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
    0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};
constexpr std::array<std::uint8_t, 12U> kServicesSupportedResponse{
    0x03U, 0x4EU, 0x08U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] bool valid_initiate(
    const mms::MmsInitiateView& value,
    const mms::MmsWirePduKind expected_kind,
    const std::span<const std::uint8_t> expected_services) noexcept {
    return value.kind == expected_kind &&
        value.maximum_mms_pdu_size == 65'000U &&
        value.maximum_outstanding_calling == 10U &&
        value.maximum_outstanding_called == 10U &&
        value.data_structure_nesting_level == 5U &&
        value.detail.version_number == 1U &&
        matches(value.detail.parameter_support_options, kParameterSupport) &&
        value.detail.services_supported_calling.size() == expected_services.size() &&
        std::equal(
            value.detail.services_supported_calling.begin(),
            value.detail.services_supported_calling.end(),
            expected_services.begin());
}

} // namespace

int main() {
    mms::MmsInitiateView initiate;
    if (!mms::MmsPduSpanCodec::try_decode_initiate_request_view(
            kInitiateRequest, initiate) ||
        !valid_initiate(
            initiate,
            mms::MmsWirePduKind::initiate_request,
            kServicesSupportedRequest)) {
        return 1;
    }

    if (!mms::MmsPduSpanCodec::try_decode_initiate_response_view(
            kInitiateResponse, initiate) ||
        !valid_initiate(
            initiate,
            mms::MmsWirePduKind::initiate_response,
            kServicesSupportedResponse)) {
        return 2;
    }

    std::array<std::uint8_t, 128U> buffer{};
    const auto initiate_result =
        mms::MmsPduSpanCodec::encode_default_initiate_response_into(buffer);
    if (!initiate_result.success() ||
        initiate_result.bytes_written != kInitiateResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(
                initiate_result.bytes_written),
            kInitiateResponse)) {
        return 3;
    }

    const auto dynamic_default = mms::MmsPduSpanCodec::encode_initiate_response_into(
        65'000U, 10U, 10U, 5U, buffer);
    if (!dynamic_default.success() ||
        dynamic_default.bytes_written != kInitiateResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(dynamic_default.bytes_written),
            kInitiateResponse)) {
        return 30;
    }

    const auto negotiated = mms::MmsPduSpanCodec::encode_initiate_response_into(
        1'024U, 2U, 3U, 1U, buffer);
    if (!negotiated.success() ||
        !mms::MmsPduSpanCodec::try_decode_initiate_response_view(
            std::span<const std::uint8_t>{buffer}.first(negotiated.bytes_written),
            initiate) ||
        initiate.maximum_mms_pdu_size != 1'024U ||
        initiate.maximum_outstanding_calling != 2U ||
        initiate.maximum_outstanding_called != 3U ||
        initiate.data_structure_nesting_level != 1U) {
        return 31;
    }

    constexpr std::array<std::uint8_t, 7U> confirmed_request_golden{
        0xA0U, 0x05U, 0x02U, 0x01U, 0x07U, 0xA4U, 0x00U};
    const auto confirmed_request_result =
        mms::MmsPduSpanCodec::encode_confirmed_request_into(
            7U, 4, true, {}, buffer);
    if (!confirmed_request_result.success() ||
        confirmed_request_result.bytes_written != confirmed_request_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(
                confirmed_request_result.bytes_written),
            confirmed_request_golden)) {
        return 4;
    }

    mms::MmsConfirmedPduView confirmed;
    if (!mms::MmsPduSpanCodec::try_decode_confirmed_request_view(
            confirmed_request_golden, confirmed) ||
        confirmed.kind != mms::MmsWirePduKind::confirmed_request ||
        confirmed.invoke_id != 7U || confirmed.service_tag != 4 ||
        confirmed.service() != mms::MmsWireConfirmedService::read ||
        !confirmed.service_constructed || !confirmed.service_value.empty()) {
        return 5;
    }

    constexpr std::array<std::uint8_t, 7U> confirmed_response_golden{
        0xA1U, 0x05U, 0x02U, 0x01U, 0x07U, 0xA4U, 0x00U};
    const auto confirmed_response_result =
        mms::MmsPduSpanCodec::encode_confirmed_response_into(
            7U, 4, true, {}, buffer);
    if (!confirmed_response_result.success() ||
        confirmed_response_result.bytes_written != confirmed_response_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(
                confirmed_response_result.bytes_written),
            confirmed_response_golden) ||
        !mms::MmsPduSpanCodec::try_decode_confirmed_response_view(
            confirmed_response_golden, confirmed) ||
        confirmed.kind != mms::MmsWirePduKind::confirmed_response ||
        confirmed.invoke_id != 7U || confirmed.service_tag != 4) {
        return 6;
    }

    constexpr std::array<std::uint8_t, 8U> reject_golden{
        0xA4U, 0x06U, 0x80U, 0x01U, 0x07U, 0x81U, 0x01U, 0x01U};
    const auto reject_result =
        mms::MmsPduSpanCodec::encode_confirmed_request_reject_into(
            7U,
            mms::MmsConfirmedRequestRejectReason::unrecognized_service,
            buffer);
    if (!reject_result.success() ||
        reject_result.bytes_written != reject_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(reject_result.bytes_written),
            reject_golden)) {
        return 7;
    }

    constexpr std::array<std::uint8_t, 10U> maximum_invoke_golden{
        0xA0U, 0x08U, 0x02U, 0x04U,
        0x7FU, 0xFFU, 0xFFU, 0xFFU,
        0xA4U, 0x00U};
    const auto maximum_result = mms::MmsPduSpanCodec::encode_confirmed_request_into(
        mms::MmsPduSpanCodec::maximum_invoke_id, 4, true, {}, buffer);
    if (!maximum_result.success() ||
        maximum_result.bytes_written != maximum_invoke_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(maximum_result.bytes_written),
            maximum_invoke_golden) ||
        !mms::MmsPduSpanCodec::try_decode_confirmed_request_view(
            maximum_invoke_golden, confirmed) ||
        confirmed.invoke_id != mms::MmsPduSpanCodec::maximum_invoke_id) {
        return 8;
    }

    constexpr std::array<std::uint8_t, 8U> invoke_128_golden{
        0xA0U, 0x06U, 0x02U, 0x02U, 0x00U, 0x80U, 0xA4U, 0x00U};
    const auto invoke_128 = mms::MmsPduSpanCodec::encode_confirmed_request_into(
        128U, 4, true, {}, buffer);
    if (!invoke_128.success() || invoke_128.bytes_written != invoke_128_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(invoke_128.bytes_written),
            invoke_128_golden)) {
        return 9;
    }

    std::array<std::uint8_t, 256U> p_data{};
    const auto p_data_result = osi::PresentationSpanCodec::encode_p_data_into(
        confirmed_request_golden, p_data, 3U, true);
    osi::PresentationPdvView pdv;
    if (!p_data_result.success() ||
        !osi::PresentationSpanCodec::try_decode_p_data_view(
            std::span<const std::uint8_t>{p_data}.first(p_data_result.bytes_written),
            pdv) ||
        pdv.context_id != 3U ||
        !mms::MmsPduSpanCodec::try_decode_confirmed_request_view(
            pdv.single_asn1_type, confirmed) ||
        confirmed.invoke_id != 7U ||
        confirmed.service() != mms::MmsWireConfirmedService::read) {
        return 10;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto invoke = iteration & 0x7FFFU;
        const auto encoded = mms::MmsPduSpanCodec::encode_confirmed_response_into(
            invoke, 4, true, {}, buffer);
        mms::MmsConfirmedPduView decoded;
        if (!encoded.success() ||
            !mms::MmsPduSpanCodec::try_decode_confirmed_response_view(
                std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
                decoded) ||
            decoded.invoke_id != invoke || decoded.service_tag != 4) {
            return 11;
        }
    }

    constexpr std::array<std::uint8_t, 5U> malformed_initiate{
        0xA8U, 0x03U, 0x80U, 0x01U, 0x01U};
    if (mms::MmsPduSpanCodec::try_decode_initiate_request_view(
            malformed_initiate, initiate)) {
        return 12;
    }

    return 0;
}
