// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 16U> kDomainListRequest{
    0xA0U, 0x0EU, 0x02U, 0x01U, 0x07U,
    0xA1U, 0x09U,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x09U,
    0xA1U, 0x02U, 0x80U, 0x00U};

constexpr std::array<std::uint8_t, 22U> kDomainScopedRequest{
    0xA0U, 0x14U, 0x02U, 0x01U, 0x08U,
    0xA1U, 0x0FU,
    0xA0U, 0x03U, 0x80U, 0x01U, 0x00U,
    0xA1U, 0x05U, 0x81U, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x82U, 0x01U, 0x58U};

constexpr std::array<std::uint8_t, 22U> kResponse{
    0xA1U, 0x14U, 0x02U, 0x01U, 0x07U,
    0xA1U, 0x0FU,
    0xA0U, 0x0AU,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x31U,
    0x81U, 0x01U, 0x00U};

constexpr std::array<std::uint8_t, 3U> kLd0{0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 3U> kLd1{0x4CU, 0x44U, 0x31U};
constexpr std::array<std::uint8_t, 1U> kContinuation{0x58U};

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

} // namespace

int main() {
    mms::MmsGetNameListRequestView request;
    if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_request(
            kDomainListRequest, request) ||
        request.invoke_id != 7U ||
        request.object_class != mms::MmsNameListObjectClass::domain ||
        request.scope != mms::MmsNameScopeKind::vmd_specific ||
        !request.domain_id.empty() || !request.continue_after.empty()) {
        return 1;
    }

    if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_request(
            kDomainScopedRequest, request) ||
        request.invoke_id != 8U ||
        request.object_class != mms::MmsNameListObjectClass::named_variable ||
        request.scope != mms::MmsNameScopeKind::domain_specific ||
        !matches(request.domain_id, kLd0) ||
        !matches(request.continue_after, kContinuation)) {
        return 2;
    }

    const std::array<std::string_view, 2U> names{"LD0", "LD1"};
    std::array<std::uint8_t, 256U> buffer{};
    const auto encoded = mms::MmsServiceSpanCodec::encode_get_name_list_response_into(
        7U, names, false, buffer);
    if (!encoded.success() || encoded.bytes_written != kResponse.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
            kResponse)) {
        return 3;
    }

    mms::MmsGetNameListResponseView response;
    if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
            kResponse, response) ||
        response.invoke_id != 7U || response.identifier_count != 2U ||
        response.more_follows) {
        return 4;
    }

    std::span<const std::uint8_t> identifier;
    if (!response.try_identifier(0U, identifier) || !matches(identifier, kLd0) ||
        !response.try_identifier(1U, identifier) || !matches(identifier, kLd1) ||
        response.try_identifier(2U, identifier)) {
        return 5;
    }

    // Post-association application flow: response PDU -> Presentation P-DATA ->
    // zero-copy Presentation view -> zero-copy GetNameList response view.
    std::array<std::uint8_t, 512U> p_data{};
    const auto p_data_result = osi::PresentationSpanCodec::encode_p_data_into(
        kResponse, p_data, 3U, true);
    osi::PresentationPdvView pdv;
    if (!p_data_result.success() ||
        !osi::PresentationSpanCodec::try_decode_p_data_view(
            std::span<const std::uint8_t>{p_data}.first(p_data_result.bytes_written),
            pdv) ||
        pdv.context_id != 3U ||
        !mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
            pdv.single_asn1_type, response) ||
        response.identifier_count != 2U) {
        return 6;
    }

    // Empty lists are valid MMS responses; pagination is expressed separately
    // by moreFollows.
    const std::span<const std::string_view> empty_names;
    const auto empty = mms::MmsServiceSpanCodec::encode_get_name_list_response_into(
        9U, empty_names, true, buffer);
    if (!empty.success() ||
        !mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
            std::span<const std::uint8_t>{buffer}.first(empty.bytes_written),
            response) ||
        response.identifier_count != 0U || !response.more_follows) {
        return 7;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto invoke = iteration & 0x7FFFU;
        const auto repeated = mms::MmsServiceSpanCodec::encode_get_name_list_response_into(
            invoke, names, (iteration & 1U) != 0U, buffer);
        mms::MmsGetNameListResponseView decoded;
        if (!repeated.success() ||
            !mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
                std::span<const std::uint8_t>{buffer}.first(repeated.bytes_written),
                decoded) ||
            decoded.invoke_id != invoke || decoded.identifier_count != 2U ||
            decoded.more_follows != ((iteration & 1U) != 0U)) {
            return 8;
        }
    }

    constexpr std::array<std::uint8_t, 16U> malformed_scope{
        0xA0U, 0x0EU, 0x02U, 0x01U, 0x07U,
        0xA1U, 0x09U,
        0xA0U, 0x03U, 0x80U, 0x01U, 0x09U,
        0xA1U, 0x02U, 0x80U, 0x01U};
    if (mms::MmsServiceSpanCodec::try_decode_get_name_list_request(
            malformed_scope, request)) {
        return 9;
    }

    const std::array<std::string_view, 1U> invalid_name{
        std::string_view{"A\0B", 3U}};
    if (mms::MmsServiceSpanCodec::encode_get_name_list_response_into(
            1U, invalid_name, false, buffer).success()) {
        return 10;
    }

    return 0;
}
