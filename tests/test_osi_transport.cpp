// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ar::iec61850::osi::CotpDataReassembler;
using ar::iec61850::osi::CotpFormatError;
using ar::iec61850::osi::CotpFrameCodec;
using ar::iec61850::osi::CotpParameter;
using ar::iec61850::osi::CotpTpdu;
using ar::iec61850::osi::CotpTpduKind;
using ar::iec61850::osi::TpktFormatError;
using ar::iec61850::osi::TpktFrame;
using ar::iec61850::osi::TpktFrameCodec;
using ar::iec61850::osi::TpktStreamDecoder;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

template <typename Exception, typename Callable>
void check_throws(Callable&& callable) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown.");
}

std::vector<std::uint8_t> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::uint8_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

std::vector<std::uint8_t> parameter_value(const CotpTpdu& tpdu, const std::uint8_t code) {
    const auto parameter = tpdu.parameter(code);
    CHECK(parameter.has_value());
    return {parameter->begin(), parameter->end()};
}

void tpkt_matches_csharp_golden_vector_and_rejects_invalid_headers() {
    const auto payload = bytes({0x02, 0xF0, 0x80, 0xA8, 0x01, 0x00});
    const auto encoded = TpktFrameCodec::encode(payload);
    CHECK(encoded == bytes({0x03, 0x00, 0x00, 0x0A, 0x02, 0xF0, 0x80, 0xA8, 0x01, 0x00}));

    const auto decoded = TpktFrameCodec::decode(encoded);
    CHECK(decoded.version == 0x03U);
    CHECK(decoded.declared_length == 10U);
    CHECK(decoded.payload == payload);

    TpktFrame ignored;
    std::string error;
    CHECK(!TpktFrameCodec::try_decode(bytes({0x03, 0x00, 0x00}), ignored, &error));
    CHECK(!error.empty());
    CHECK(!TpktFrameCodec::try_decode(
        bytes({0x02, 0x00, 0x00, 0x04}), ignored, &error));
    CHECK(!TpktFrameCodec::try_decode(
        bytes({0x03, 0x01, 0x00, 0x04}), ignored, &error));
    CHECK(!TpktFrameCodec::try_decode(
        bytes({0x03, 0x00, 0x00, 0x08, 0x02, 0xF0, 0x80}), ignored, &error));
    CHECK(!TpktFrameCodec::try_decode(
        bytes({0x03, 0x00, 0x00, 0x03}), ignored, &error));
}

void tpkt_stream_decoder_handles_fragmentation_and_coalescing() {
    const auto first_payload = bytes({0x01, 0x02, 0x03});
    const auto second_payload = bytes({0xA0, 0xB0});
    const auto first = TpktFrameCodec::encode(first_payload);
    const auto second = TpktFrameCodec::encode(second_payload);

    std::vector<std::uint8_t> wire;
    wire.insert(wire.end(), first.begin(), first.end());
    wire.insert(wire.end(), second.begin(), second.end());

    TpktStreamDecoder decoder;
    TpktFrame frame;
    decoder.append(std::span<const std::uint8_t>{wire}.first(2U));
    CHECK(!decoder.try_pop(frame));
    decoder.append(std::span<const std::uint8_t>{wire}.subspan(2U, 4U));
    CHECK(!decoder.try_pop(frame));
    decoder.append(std::span<const std::uint8_t>{wire}.subspan(6U));
    CHECK(decoder.try_pop(frame));
    CHECK(frame.payload == first_payload);
    CHECK(decoder.try_pop(frame));
    CHECK(frame.payload == second_payload);
    CHECK(!decoder.try_pop(frame));
    CHECK(decoder.buffered_bytes() == 0U);
    CHECK(decoder.decoded_frames() == 2U);

    TpktStreamDecoder bounded{8U};
    bounded.append(bytes({0x03, 0x00, 0x00, 0x08}));
    check_throws<TpktFormatError>([&bounded] {
        bounded.append(bytes({0x00, 0x01, 0x02, 0x03, 0x04}));
    });
}

void cotp_default_connection_request_matches_csharp_vector() {
    const auto encoded = CotpFrameCodec::encode_default_connection_request();
    CHECK(encoded == bytes({
        0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
        0xC0, 0x01, 0x0A,
        0xC1, 0x02, 0x00, 0x01,
        0xC2, 0x02, 0x00, 0x01}));

    const auto decoded = CotpFrameCodec::decode(encoded);
    CHECK(decoded.kind == CotpTpduKind::connection_request);
    CHECK(decoded.destination_reference == 0x0000U);
    CHECK(decoded.source_reference == 0x0001U);
    CHECK(decoded.class_or_reason == 0U);
    CHECK(parameter_value(decoded, CotpFrameCodec::tpdu_size_parameter) == bytes({0x0A}));
    CHECK(parameter_value(decoded, CotpFrameCodec::source_tsap_parameter) == bytes({0x00, 0x01}));
    CHECK(parameter_value(decoded, CotpFrameCodec::destination_tsap_parameter) == bytes({0x00, 0x01}));
}

void cotp_connection_confirm_mirrors_tsap_and_negotiates_size() {
    const std::array<CotpParameter, 3> parameters{
        CotpParameter{0xC0U, {0x09U}},
        CotpParameter{0xC1U, {0x11U, 0x22U}},
        CotpParameter{0xC2U, {0x33U, 0x44U, 0x55U}},
    };
    const auto request_bytes = CotpFrameCodec::encode_connection_request(0x0001U, parameters);
    const auto request = CotpFrameCodec::decode(request_bytes);
    const auto confirm_bytes = CotpFrameCodec::encode_connection_confirm(request, 0x1001U, 0x0AU);
    const auto confirm = CotpFrameCodec::decode(confirm_bytes);

    CHECK(confirm.kind == CotpTpduKind::connection_confirm);
    CHECK(confirm.destination_reference == 0x0001U);
    CHECK(confirm.source_reference == 0x1001U);
    CHECK(parameter_value(confirm, 0xC0U) == bytes({0x09}));
    CHECK(parameter_value(confirm, 0xC1U) == bytes({0x33, 0x44, 0x55}));
    CHECK(parameter_value(confirm, 0xC2U) == bytes({0x11, 0x22}));

    const auto direct = CotpFrameCodec::decode(
        CotpFrameCodec::encode_connection_confirm(0x0001U, 0x1234U));
    CHECK(direct.destination_reference == 0x0001U);
    CHECK(direct.source_reference == 0x1234U);
    CHECK(parameter_value(direct, 0xC0U) == bytes({0x0A}));
}

void cotp_data_segmentation_and_reassembly_are_bounded() {
    std::vector<std::uint8_t> user_data(2500U);
    for (std::size_t index = 0U; index < user_data.size(); ++index) {
        user_data[index] = static_cast<std::uint8_t>(index % 251U);
    }

    const auto segments = CotpFrameCodec::encode_data_segments(user_data, 0x0AU);
    CHECK(segments.size() == 3U);
    CHECK(std::all_of(segments.begin(), segments.end(), [](const auto& segment) {
        return segment.size() <= 1024U;
    }));

    CotpDataReassembler reassembler{3000U, 8U, 2U};
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        const auto decoded = CotpFrameCodec::decode(segments[index]);
        CHECK(decoded.kind == CotpTpduKind::data);
        CHECK(decoded.tpdu_number == static_cast<std::uint8_t>(index));
        CHECK(decoded.end_of_transmission == (index + 1U == segments.size()));
        reassembler.append(decoded);
    }
    CHECK(reassembler.is_complete());
    CHECK(reassembler.fragment_count() == 3U);
    CHECK(reassembler.complete() == user_data);

    const auto empty = CotpFrameCodec::encode_data_segments({}, 0x07U);
    CHECK(empty.size() == 1U);
    CHECK(CotpFrameCodec::decode(empty.front()).end_of_transmission);
}

void cotp_reassembler_rejects_abuse_and_incomplete_sequences() {
    CotpDataReassembler bytes_limit{3U, 4U, 2U};
    bytes_limit.append_encoded(CotpFrameCodec::encode_data(bytes({1, 2}), false));
    check_throws<CotpFormatError>([&bytes_limit] {
        bytes_limit.append_encoded(CotpFrameCodec::encode_data(bytes({3, 4}), true));
    });

    CotpDataReassembler fragment_limit{100U, 1U, 1U};
    fragment_limit.append_encoded(CotpFrameCodec::encode_data(bytes({1}), false));
    check_throws<CotpFormatError>([&fragment_limit] {
        fragment_limit.append_encoded(CotpFrameCodec::encode_data(bytes({2}), true));
    });

    CotpDataReassembler empty_limit{100U, 4U, 1U};
    empty_limit.append_encoded(CotpFrameCodec::encode_data({}, false));
    check_throws<CotpFormatError>([&empty_limit] {
        empty_limit.append_encoded(CotpFrameCodec::encode_data({}, false));
    });

    CotpDataReassembler incomplete{100U, 4U, 1U};
    incomplete.append_encoded(CotpFrameCodec::encode_data(bytes({1}), false));
    check_throws<CotpFormatError>([&incomplete] {
        static_cast<void>(incomplete.complete());
    });

    CotpDataReassembler completed{100U, 4U, 1U};
    completed.append_encoded(CotpFrameCodec::encode_data(bytes({1}), true));
    check_throws<CotpFormatError>([&completed] {
        completed.append_encoded(CotpFrameCodec::encode_data(bytes({2}), true));
    });
}

void cotp_disconnect_error_and_malformed_parameter_paths_are_deterministic() {
    const auto disconnect_bytes = CotpFrameCodec::encode_disconnect_request(
        0x0001U, 0x1234U, 0x80U);
    const auto disconnect = CotpFrameCodec::decode(disconnect_bytes);
    CHECK(disconnect.kind == CotpTpduKind::disconnect_request);
    CHECK(disconnect.destination_reference == 0x0001U);
    CHECK(disconnect.source_reference == 0x1234U);
    CHECK(disconnect.class_or_reason == 0x80U);

    const auto error_tpdu = CotpFrameCodec::decode(
        bytes({0x06, 0x70, 0x00, 0x01, 0x12, 0x34, 0x01}));
    CHECK(error_tpdu.kind == CotpTpduKind::error);
    CHECK(error_tpdu.class_or_reason == 0x01U);

    CotpTpdu ignored;
    std::string error;
    CHECK(!CotpFrameCodec::try_decode(bytes({0x01, 0xF0}), ignored, &error));
    CHECK(!CotpFrameCodec::try_decode(bytes({0x02, 0xF0}), ignored, &error));
    CHECK(!CotpFrameCodec::try_decode(
        bytes({0x09, 0xE0, 0, 0, 0, 1, 0, 0xC0, 0x02, 0x0A}), ignored, &error));
    CHECK(!CotpFrameCodec::try_decode(
        bytes({0x06, 0x99, 0, 0, 0, 1, 0}), ignored, &error));
    CHECK(!CotpFrameCodec::try_decode(
        bytes({0x06, 0xE0, 0, 0, 0, 1, 0, 0xAA}), ignored, &error));
}

void complete_tpkt_cotp_vectors_round_trip_byte_exact() {
    const auto cr = CotpFrameCodec::encode_default_connection_request();
    const auto tpkt_cr = TpktFrameCodec::encode(cr);
    CHECK(TpktFrameCodec::encode(TpktFrameCodec::decode(tpkt_cr).payload) == tpkt_cr);
    CHECK(CotpFrameCodec::encode_default_connection_request() == cr);

    const auto data = CotpFrameCodec::encode_data(bytes({0x0D, 0x01, 0x02, 0x03}));
    CHECK(data == bytes({0x02, 0xF0, 0x80, 0x0D, 0x01, 0x02, 0x03}));
    const auto decoded = CotpFrameCodec::decode(data);
    CHECK(decoded.end_of_transmission);
    CHECK(decoded.user_data == bytes({0x0D, 0x01, 0x02, 0x03}));
    CHECK(CotpFrameCodec::encode_data(
        decoded.user_data, decoded.end_of_transmission, decoded.tpdu_number) == data);
}

void deterministic_payload_matrix_round_trips() {
    std::uint32_t state = 0xC0FFEEU;
    for (std::size_t length = 0U; length <= 4096U; length += 127U) {
        std::vector<std::uint8_t> payload(length);
        for (auto& value : payload) {
            state = (state * 1664525U) + 1013904223U;
            value = static_cast<std::uint8_t>(state >> 24U);
        }
        const auto tpkt = TpktFrameCodec::encode(payload);
        CHECK(TpktFrameCodec::decode(tpkt).payload == payload);

        const auto segments = CotpFrameCodec::encode_data_segments(payload, 0x09U);
        CotpDataReassembler reassembler{5000U, 64U, 2U};
        for (const auto& segment : segments) {
            reassembler.append_encoded(segment);
        }
        CHECK(reassembler.complete() == payload);
    }
}

} // namespace

int main() {
    const std::array<std::pair<const char*, std::function<void()>>, 9> tests{{
        {"TPKT golden vectors and validation", tpkt_matches_csharp_golden_vector_and_rejects_invalid_headers},
        {"TPKT incremental stream framing", tpkt_stream_decoder_handles_fragmentation_and_coalescing},
        {"COTP default connection request", cotp_default_connection_request_matches_csharp_vector},
        {"COTP connection confirm negotiation", cotp_connection_confirm_mirrors_tsap_and_negotiates_size},
        {"COTP segmentation and reassembly", cotp_data_segmentation_and_reassembly_are_bounded},
        {"COTP reassembly abuse guards", cotp_reassembler_rejects_abuse_and_incomplete_sequences},
        {"COTP disconnect/error/malformed paths", cotp_disconnect_error_and_malformed_parameter_paths_are_deterministic},
        {"Complete TPKT/COTP byte-exact vectors", complete_tpkt_cotp_vectors_round_trip_byte_exact},
        {"Deterministic payload matrix", deterministic_payload_matrix_round_trips},
    }};

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << passed << " transport regression groups.\n";
    return 0;
}
