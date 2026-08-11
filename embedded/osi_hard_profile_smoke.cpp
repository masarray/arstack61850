// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850::osi;

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] bool parameter_matches(
    const CotpTpduView& tpdu,
    const std::uint8_t code,
    const std::span<const std::uint8_t> expected) noexcept {
    std::span<const std::uint8_t> value;
    return tpdu.try_parameter(code, value) &&
        value.size() == expected.size() &&
        std::equal(value.begin(), value.end(), expected.begin());
}

} // namespace

int main() {
    // Existing desktop golden vector: TPKT wrapping a COTP Data TPDU prefix.
    constexpr std::array<std::uint8_t, 6U> tpkt_payload{
        0x02U, 0xF0U, 0x80U, 0xA8U, 0x01U, 0x00U};
    constexpr std::array<std::uint8_t, 10U> tpkt_golden{
        0x03U, 0x00U, 0x00U, 0x0AU,
        0x02U, 0xF0U, 0x80U, 0xA8U, 0x01U, 0x00U};

    std::array<std::uint8_t, 64U> tpkt_buffer{};
    const auto tpkt_result = TpktSpanCodec::encode_into(tpkt_payload, tpkt_buffer);
    if (!tpkt_result.success() || tpkt_result.bytes_written != tpkt_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{tpkt_buffer}.first(tpkt_result.bytes_written),
            tpkt_golden)) {
        return 1;
    }

    TpktFrameView tpkt_view;
    if (!TpktSpanCodec::try_decode_view(tpkt_golden, tpkt_view) ||
        tpkt_view.version != TpktSpanCodec::supported_version ||
        tpkt_view.declared_length != tpkt_golden.size() ||
        !matches(tpkt_view.payload, tpkt_payload)) {
        return 2;
    }

    if (TpktSpanCodec::peek_frame(
            std::span<const std::uint8_t>{tpkt_golden}.first(2U)).status !=
            TpktPeekStatus::need_more) {
        return 3;
    }

    std::array<std::uint8_t, 11U> coalesced{};
    std::copy(tpkt_golden.begin(), tpkt_golden.end(), coalesced.begin());
    coalesced.back() = 0xFFU;
    const auto peek = TpktSpanCodec::peek_frame(coalesced);
    if (!peek.ready() || peek.frame_bytes != tpkt_golden.size()) {
        return 4;
    }

    constexpr std::array<std::uint8_t, 4U> invalid_tpkt{
        0x02U, 0x00U, 0x00U, 0x04U};
    if (TpktSpanCodec::peek_frame(invalid_tpkt).status != TpktPeekStatus::invalid ||
        TpktSpanCodec::try_decode_view(invalid_tpkt, tpkt_view)) {
        return 5;
    }

    // Existing desktop golden vector: default COTP Connection Request.
    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<CotpParameterView, 3U> default_parameters{
        CotpParameterView{CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        CotpParameterView{CotpSpanCodec::source_tsap_parameter, source_tsap},
        CotpParameterView{CotpSpanCodec::destination_tsap_parameter, destination_tsap},
    };
    constexpr std::array<std::uint8_t, 18U> cr_golden{
        0x11U, 0xE0U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
        0xC0U, 0x01U, 0x0AU,
        0xC1U, 0x02U, 0x00U, 0x01U,
        0xC2U, 0x02U, 0x00U, 0x01U};

    std::array<std::uint8_t, 64U> cr_buffer{};
    const auto cr_result = CotpSpanCodec::encode_connection_request_into(
        0x0001U, default_parameters, cr_buffer);
    if (!cr_result.success() || cr_result.bytes_written != cr_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{cr_buffer}.first(cr_result.bytes_written),
            cr_golden)) {
        return 6;
    }

    CotpTpduView cr_view;
    if (!CotpSpanCodec::try_decode_view(cr_golden, cr_view) ||
        cr_view.kind != CotpWireKind::connection_request ||
        cr_view.destination_reference != 0x0000U ||
        cr_view.source_reference != 0x0001U ||
        !parameter_matches(cr_view, CotpSpanCodec::tpdu_size_parameter, tpdu_size) ||
        !parameter_matches(cr_view, CotpSpanCodec::source_tsap_parameter, source_tsap) ||
        !parameter_matches(
            cr_view, CotpSpanCodec::destination_tsap_parameter, destination_tsap)) {
        return 7;
    }

    // Server-side CC must negotiate TPDU size and mirror TSAP values without
    // constructing an owning parameter list.
    constexpr std::array<std::uint8_t, 1U> offered_size{0x09U};
    constexpr std::array<std::uint8_t, 2U> offered_source{0x11U, 0x22U};
    constexpr std::array<std::uint8_t, 3U> offered_destination{0x33U, 0x44U, 0x55U};
    const std::array<CotpParameterView, 3U> offered_parameters{
        CotpParameterView{CotpSpanCodec::tpdu_size_parameter, offered_size},
        CotpParameterView{CotpSpanCodec::source_tsap_parameter, offered_source},
        CotpParameterView{CotpSpanCodec::destination_tsap_parameter, offered_destination},
    };

    std::array<std::uint8_t, 64U> request_buffer{};
    const auto request_result = CotpSpanCodec::encode_connection_request_into(
        0x0001U, offered_parameters, request_buffer);
    CotpTpduView request_view;
    if (!request_result.success() ||
        !CotpSpanCodec::try_decode_view(
            std::span<const std::uint8_t>{request_buffer}.first(request_result.bytes_written),
            request_view)) {
        return 8;
    }

    constexpr std::array<std::uint8_t, 19U> cc_golden{
        0x12U, 0xD0U, 0x00U, 0x01U, 0x10U, 0x01U, 0x00U,
        0xC0U, 0x01U, 0x09U,
        0xC2U, 0x02U, 0x11U, 0x22U,
        0xC1U, 0x03U, 0x33U, 0x44U, 0x55U};
    std::array<std::uint8_t, 64U> cc_buffer{};
    const auto cc_result = CotpSpanCodec::encode_connection_confirm_from_request_into(
        request_view, 0x1001U, cc_buffer, 0x0AU);
    if (!cc_result.success() || cc_result.bytes_written != cc_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{cc_buffer}.first(cc_result.bytes_written),
            cc_golden)) {
        return 9;
    }

    CotpTpduView cc_view;
    if (!CotpSpanCodec::try_decode_view(cc_golden, cc_view) ||
        cc_view.kind != CotpWireKind::connection_confirm ||
        cc_view.destination_reference != 0x0001U ||
        cc_view.source_reference != 0x1001U ||
        !parameter_matches(cc_view, CotpSpanCodec::tpdu_size_parameter, offered_size) ||
        !parameter_matches(cc_view, CotpSpanCodec::source_tsap_parameter, offered_destination) ||
        !parameter_matches(cc_view, CotpSpanCodec::destination_tsap_parameter, offered_source)) {
        return 10;
    }

    constexpr std::array<std::uint8_t, 4U> cotp_user_data{
        0x0DU, 0x01U, 0x02U, 0x03U};
    constexpr std::array<std::uint8_t, 7U> cotp_data_golden{
        0x02U, 0xF0U, 0x80U, 0x0DU, 0x01U, 0x02U, 0x03U};
    std::array<std::uint8_t, 64U> cotp_data_buffer{};
    const auto data_result = CotpSpanCodec::encode_data_into(
        cotp_user_data, cotp_data_buffer);
    if (!data_result.success() || data_result.bytes_written != cotp_data_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{cotp_data_buffer}.first(data_result.bytes_written),
            cotp_data_golden)) {
        return 11;
    }

    CotpTpduView data_view;
    if (!CotpSpanCodec::try_decode_view(cotp_data_golden, data_view) ||
        data_view.kind != CotpWireKind::data ||
        !data_view.end_of_transmission || data_view.tpdu_number != 0U ||
        !matches(data_view.user_data, cotp_user_data)) {
        return 12;
    }

    // Complete zero-copy transport stack: COTP Data -> TPKT -> view decode ->
    // COTP view decode, retaining the same caller-owned storage throughout.
    std::array<std::uint8_t, 64U> transport_buffer{};
    const auto transport_result = TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{cotp_data_buffer}.first(data_result.bytes_written),
        transport_buffer);
    TpktFrameView transport_view;
    CotpTpduView nested_data_view;
    if (!transport_result.success() ||
        !TpktSpanCodec::try_decode_view(
            std::span<const std::uint8_t>{transport_buffer}.first(
                transport_result.bytes_written),
            transport_view) ||
        !CotpSpanCodec::try_decode_view(transport_view.payload, nested_data_view) ||
        !matches(nested_data_view.user_data, cotp_user_data)) {
        return 13;
    }

    // Exercise the steady-state bounded path repeatedly. No owning container is
    // constructed by either codec during these encode/decode iterations.
    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto encoded = CotpSpanCodec::encode_data_into(
            cotp_user_data, cotp_data_buffer, true,
            static_cast<std::uint8_t>(iteration & 0x7FU));
        if (!encoded.success()) {
            return 14;
        }
        CotpTpduView decoded;
        if (!CotpSpanCodec::try_decode_view(
                std::span<const std::uint8_t>{cotp_data_buffer}.first(
                    encoded.bytes_written),
                decoded) ||
            decoded.tpdu_number != static_cast<std::uint8_t>(iteration & 0x7FU)) {
            return 15;
        }
    }

    constexpr std::array<std::uint8_t, 10U> malformed_parameter{
        0x09U, 0xE0U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
        0xC0U, 0x02U, 0x0AU};
    if (CotpSpanCodec::try_decode_view(malformed_parameter, cr_view)) {
        return 16;
    }

    return 0;
}
