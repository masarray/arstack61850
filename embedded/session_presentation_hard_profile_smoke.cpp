// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] bool parameter_matches(
    const osi::SessionSpduView& spdu,
    const std::uint8_t code,
    const std::span<const std::uint8_t> expected) noexcept {
    std::span<const std::uint8_t> value;
    return spdu.try_parameter(code, value) &&
        value.size() == expected.size() &&
        std::equal(value.begin(), value.end(), expected.begin());
}

} // namespace

int main() {
    constexpr std::array<std::uint8_t, 10U> mms_payload{
        0xA0U, 0x08U, 0x02U, 0x01U, 0x07U,
        0xA4U, 0x03U, 0x80U, 0x01U, 0x00U};
    constexpr std::array<std::uint8_t, 19U> presentation_golden{
        0x61U, 0x11U, 0x30U, 0x0FU,
        0x02U, 0x01U, 0x03U,
        0xA0U, 0x0AU,
        0xA0U, 0x08U, 0x02U, 0x01U, 0x07U,
        0xA4U, 0x03U, 0x80U, 0x01U, 0x00U};

    // BER reader stays zero-copy and rejects indefinite-length BER.
    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(presentation_golden, outer) ||
        outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 1 || !outer.constructed) {
        return 1;
    }
    constexpr std::array<std::uint8_t, 4U> indefinite{
        0x61U, 0x80U, 0x00U, 0x00U};
    if (asn1::BerSpanReader::try_read_exact(indefinite, outer)) {
        return 2;
    }

    // Presentation fully-encoded-data must match the existing host encoding.
    std::array<std::uint8_t, 128U> presentation_buffer{};
    const auto presentation_result =
        osi::PresentationSpanCodec::encode_fully_encoded_data_into(
            3U, mms_payload, presentation_buffer);
    if (!presentation_result.success() ||
        presentation_result.bytes_written != presentation_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{presentation_buffer}.first(
                presentation_result.bytes_written),
            presentation_golden)) {
        return 3;
    }

    osi::PresentationPdvView pdv;
    if (!osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            presentation_golden, pdv) ||
        pdv.context_id != 3U || !matches(pdv.single_asn1_type, mms_payload)) {
        return 4;
    }

    // Default Session parameters are byte-identical to the established host
    // profile. The bounded encoder appends Presentation bytes as final C1 data.
    constexpr std::array<std::uint8_t, 6U> parameter05{
        0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U};
    constexpr std::array<std::uint8_t, 2U> parameter14{0x00U, 0x02U};
    constexpr std::array<std::uint8_t, 2U> parameter33{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> parameter34{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 43U> session_connect_golden{
        0x0DU, 0x29U,
        0x05U, 0x06U, 0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U,
        0x14U, 0x02U, 0x00U, 0x02U,
        0x33U, 0x02U, 0x00U, 0x01U,
        0x34U, 0x02U, 0x00U, 0x01U,
        0xC1U, 0x13U,
        0x61U, 0x11U, 0x30U, 0x0FU,
        0x02U, 0x01U, 0x03U,
        0xA0U, 0x0AU,
        0xA0U, 0x08U, 0x02U, 0x01U, 0x07U,
        0xA4U, 0x03U, 0x80U, 0x01U, 0x00U};

    std::array<std::uint8_t, 128U> session_buffer{};
    const auto connect_result = osi::SessionSpanCodec::encode_connect_into(
        presentation_golden, session_buffer);
    if (!connect_result.success() ||
        connect_result.bytes_written != session_connect_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{session_buffer}.first(
                connect_result.bytes_written),
            session_connect_golden)) {
        return 5;
    }

    osi::SessionSpduView connect_view;
    if (!osi::SessionSpanCodec::try_decode_view(
            session_connect_golden, connect_view) ||
        connect_view.kind != osi::SessionWireKind::connect ||
        !matches(connect_view.user_data, presentation_golden) ||
        !parameter_matches(connect_view, 0x05U, parameter05) ||
        !parameter_matches(connect_view, 0x14U, parameter14) ||
        !parameter_matches(connect_view, 0x33U, parameter33) ||
        !parameter_matches(connect_view, 0x34U, parameter34)) {
        return 6;
    }

    constexpr std::array<std::uint8_t, 43U> session_accept_golden{
        0x0EU, 0x29U,
        0x05U, 0x06U, 0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U,
        0x14U, 0x02U, 0x00U, 0x02U,
        0x33U, 0x02U, 0x00U, 0x01U,
        0x34U, 0x02U, 0x00U, 0x01U,
        0xC1U, 0x13U,
        0x61U, 0x11U, 0x30U, 0x0FU,
        0x02U, 0x01U, 0x03U,
        0xA0U, 0x0AU,
        0xA0U, 0x08U, 0x02U, 0x01U, 0x07U,
        0xA4U, 0x03U, 0x80U, 0x01U, 0x00U};
    const auto accept_result = osi::SessionSpanCodec::encode_accept_mirroring_into(
        connect_view, presentation_golden, session_buffer);
    if (!accept_result.success() ||
        accept_result.bytes_written != session_accept_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{session_buffer}.first(
                accept_result.bytes_written),
            session_accept_golden)) {
        return 7;
    }

    // Existing Session data-transfer profile: optional Give-Tokens prefix plus
    // the Data-Transfer SPDU, followed directly by Presentation bytes.
    constexpr std::array<std::uint8_t, 23U> p_data_golden{
        0x01U, 0x00U, 0x01U, 0x00U,
        0x61U, 0x11U, 0x30U, 0x0FU,
        0x02U, 0x01U, 0x03U,
        0xA0U, 0x0AU,
        0xA0U, 0x08U, 0x02U, 0x01U, 0x07U,
        0xA4U, 0x03U, 0x80U, 0x01U, 0x00U};

    const auto p_data_result = osi::PresentationSpanCodec::encode_p_data_into(
        mms_payload, presentation_buffer, 3U, true);
    if (!p_data_result.success() ||
        p_data_result.bytes_written != p_data_golden.size() ||
        !matches(
            std::span<const std::uint8_t>{presentation_buffer}.first(
                p_data_result.bytes_written),
            p_data_golden)) {
        return 8;
    }

    if (!osi::PresentationSpanCodec::try_decode_p_data_view(p_data_golden, pdv) ||
        pdv.context_id != 3U || !matches(pdv.single_asn1_type, mms_payload)) {
        return 9;
    }

    osi::SessionDataTransferView transfer;
    if (!osi::SessionSpanCodec::try_decode_data_transfer_view(
            p_data_golden, transfer) ||
        !transfer.has_give_tokens_prefix ||
        !matches(transfer.presentation_payload, presentation_golden)) {
        return 10;
    }

    // Direct Presentation decode remains accepted when the Session wrapper has
    // already been removed by the transport state machine.
    if (!osi::PresentationSpanCodec::try_decode_p_data_view(
            presentation_golden, pdv) ||
        pdv.context_id != 3U || !matches(pdv.single_asn1_type, mms_payload)) {
        return 11;
    }

    // Steady-state encode/decode regression: no owning container is created by
    // BER, Session, or Presentation bounded APIs.
    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        const auto context_id = (iteration & 1U) == 0U ? 3U : 5U;
        const auto encoded = osi::PresentationSpanCodec::encode_p_data_into(
            mms_payload, presentation_buffer, context_id, true);
        if (!encoded.success()) {
            return 12;
        }
        osi::PresentationPdvView decoded;
        if (!osi::PresentationSpanCodec::try_decode_p_data_view(
                std::span<const std::uint8_t>{presentation_buffer}.first(
                    encoded.bytes_written),
                decoded) ||
            decoded.context_id != context_id ||
            !matches(decoded.single_asn1_type, mms_payload)) {
            return 13;
        }
    }

    return 0;
}
