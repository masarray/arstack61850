// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/osi/presentation.hpp"
#include "ariec61850/osi/session.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ar::iec61850::acse::AcseAare;
using ar::iec61850::acse::AcseAssociationCodec;
using ar::iec61850::acse::AcseFormatError;
using ar::iec61850::osi::PresentationCodec;
using ar::iec61850::osi::PresentationContextDefinition;
using ar::iec61850::osi::PresentationFormatError;
using ar::iec61850::osi::PresentationPdv;
using ar::iec61850::osi::SessionCodec;
using ar::iec61850::osi::SessionFormatError;
using ar::iec61850::osi::SessionSpdu;
using ar::iec61850::osi::SessionSpduKind;

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

std::vector<std::uint8_t> from_hex(const std::string& text) {
    std::vector<std::uint8_t> result;
    int high = -1;
    for (const auto ch : text) {
        int value = -1;
        if (ch >= '0' && ch <= '9') {
            value = ch - '0';
        } else if (ch >= 'A' && ch <= 'F') {
            value = 10 + ch - 'A';
        } else if (ch >= 'a' && ch <= 'f') {
            value = 10 + ch - 'a';
        } else {
            continue;
        }
        if (high < 0) {
            high = value;
        } else {
            result.push_back(static_cast<std::uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0) {
        throw std::invalid_argument("Odd-length hexadecimal test vector.");
    }
    return result;
}

std::vector<std::uint8_t> csharp_default_request() {
    return from_hex(
        "0D B6 05 06 13 01 00 16 01 02 14 02 00 02 33 02 00 01 34 02 00 01 C1 A0 "
        "31 81 9D A0 03 80 01 01 A2 81 95 81 04 00 00 00 01 82 04 00 00 00 01 "
        "A4 23 30 0F 02 01 01 06 04 52 01 00 01 30 04 06 02 51 01 30 10 02 01 03 "
        "06 05 28 CA 22 02 01 30 04 06 02 51 01 61 62 30 60 02 01 01 A0 5B 60 59 "
        "A1 07 06 05 28 CA 22 02 03 A2 07 06 05 29 01 87 67 01 A3 03 02 01 0C "
        "A6 06 06 04 29 01 87 67 A7 03 02 01 0C BE 33 28 31 06 02 51 01 02 01 03 "
        "A0 28 A8 26 80 03 00 FD E8 81 01 0A 82 01 0A 83 01 05 A4 16 80 01 01 "
        "81 03 05 F1 00 82 0C 03 EE 1C 00 00 04 08 00 00 79 EF 18");
}

std::vector<std::uint8_t> csharp_deterministic_response() {
    return from_hex(
        "0E 88 05 06 13 01 00 16 01 02 14 02 00 02 33 02 00 01 34 02 00 01 C1 72 "
        "31 70 A0 03 80 01 01 A2 69 A5 12 30 07 80 01 00 81 02 51 01 30 07 80 01 00 "
        "81 02 51 01 61 53 30 51 02 01 01 A0 4C 61 4A A1 07 06 05 28 CA 22 02 03 "
        "A2 03 02 01 00 A3 05 A1 03 02 01 00 BE 33 28 31 06 02 51 01 02 01 03 "
        "A0 28 A9 26 80 03 00 FD E8 81 01 0A 82 01 0A 83 01 05 A4 16 80 01 01 "
        "81 03 05 F1 00 82 0C 03 EE 1C 00 00 04 08 00 00 79 EF 18");
}

std::size_t find_sequence(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<std::uint8_t>& sequence) {
    const auto found = std::search(
        bytes.begin(), bytes.end(), sequence.begin(), sequence.end());
    return found == bytes.end()
        ? bytes.size()
        : static_cast<std::size_t>(std::distance(bytes.begin(), found));
}

void session_connect_accept_and_data_transfer_are_strict() {
    const auto request = csharp_default_request();
    const auto session = SessionCodec::decode(request);
    CHECK(session.kind == SessionSpduKind::connect);
    CHECK(session.parameters == SessionCodec::default_parameters());
    CHECK(session.user_data.size() == 160U);
    CHECK(SessionCodec::encode_connect(session.user_data) == request);

    auto malformed = request;
    malformed[1] = static_cast<std::uint8_t>(malformed[1] - 1U);
    check_throws<SessionFormatError>([&malformed] {
        static_cast<void>(SessionCodec::decode(malformed));
    });

    const auto transfer = SessionCodec::encode_data_transfer(
        from_hex("61 05 30 03 02 01 03"));
    const auto decoded_transfer = SessionCodec::decode_data_transfer(transfer);
    CHECK(decoded_transfer.has_give_tokens_prefix);
    CHECK(decoded_transfer.presentation_payload == from_hex("61 05 30 03 02 01 03"));
}

void presentation_cp_decodes_csharp_contexts_and_round_trips() {
    const auto session = SessionCodec::decode(csharp_default_request());
    const auto cp = PresentationCodec::decode_cp(session.user_data);
    CHECK(cp.mode_selector == 1U);
    CHECK(cp.contexts.size() == 2U);
    CHECK(cp.user_data.context_id == 1U);
    CHECK(cp.context_id_for_abstract_syntax(
        PresentationCodec::acse_abstract_syntax_name()) == 1U);
    CHECK(cp.context_id_for_abstract_syntax(
        PresentationCodec::mms_abstract_syntax_name()) == 3U);
    CHECK(cp.calling_selector == from_hex("00 00 00 01"));
    CHECK(cp.called_selector == from_hex("00 00 00 01"));

    const auto rebuilt = PresentationCodec::encode_cp(
        cp.contexts,
        cp.user_data.context_id,
        cp.user_data.single_asn1_type,
        cp.calling_selector,
        cp.called_selector,
        cp.mode_selector);
    CHECK(rebuilt == session.user_data);
}

void balanced_aarq_matches_csharp_and_decodes_structurally() {
    const auto encoded = AcseAssociationCodec::build_default_association_request();
    CHECK(encoded == csharp_default_request());

    const auto request = AcseAssociationCodec::decode_association_request(encoded);
    CHECK(request.acse_presentation_context_id == 1U);
    CHECK(request.mms_presentation_context_id == 3U);
    CHECK(request.aarq.application_context_name ==
          AcseAssociationCodec::mms_application_context_name());
    CHECK(request.aarq.called_ap_title ==
          AcseAssociationCodec::balanced_called_ap_title());
    CHECK(request.aarq.called_ae_qualifier == 12U);
    CHECK(request.aarq.calling_ap_title ==
          AcseAssociationCodec::balanced_calling_ap_title());
    CHECK(request.aarq.calling_ae_qualifier == 12U);
    CHECK(request.aarq.user_information.has_value());
    CHECK(request.aarq.user_information->single_asn1_type.front() == 0xA8U);
}

void deterministic_aare_response_matches_csharp_and_decodes() {
    const auto response = AcseAssociationCodec::build_accept_response(
        csharp_default_request());
    CHECK(response.payload == csharp_deterministic_response());
    CHECK(response.mms_presentation_context_id == 3U);

    const auto decoded = AcseAssociationCodec::decode_association_response(
        response.payload);
    CHECK(decoded.session.kind == SessionSpduKind::accept);
    CHECK(decoded.presentation.context_results.size() == 2U);
    CHECK(std::all_of(
        decoded.presentation.context_results.begin(),
        decoded.presentation.context_results.end(),
        [](const auto& result) {
            return result.result ==
                ar::iec61850::osi::PresentationContextResultCode::acceptance;
        }));
    CHECK(decoded.presentation.user_data.context_id == 1U);
    CHECK(decoded.aare.accepted());
    CHECK(decoded.aare.user_information.has_value());
    CHECK(decoded.aare.user_information->single_asn1_type.front() == 0xA9U);
}

void response_mirrors_session_parameters_and_negotiated_context_ids() {
    auto request = csharp_default_request();
    request[17] = 0x7EU;
    const auto context_signature = from_hex(
        "02 01 03 06 05 28 CA 22 02 01");
    const auto offset = find_sequence(request, context_signature);
    CHECK(offset < request.size());
    request[offset + 2U] = 0x05U;

    const auto decoded_request = AcseAssociationCodec::decode_association_request(request);
    CHECK(decoded_request.mms_presentation_context_id == 5U);
    const auto response = AcseAssociationCodec::build_accept_response(decoded_request);
    CHECK(response.mms_presentation_context_id == 5U);
    CHECK(response.payload[17] == 0x7EU);

    const auto decoded_response = AcseAssociationCodec::decode_association_response(
        response.payload);
    CHECK(decoded_response.presentation.context_results.size() == 2U);
}

void presentation_p_data_round_trips_mms_payload_and_context() {
    const auto mms = from_hex("A0 08 02 01 07 A4 03 80 01 00");
    const auto encoded = PresentationCodec::encode_p_data(mms, 5U);
    const auto decoded = PresentationCodec::decode_p_data(encoded);
    CHECK(decoded.context_id == 5U);
    CHECK(decoded.single_asn1_type == mms);

    const auto without_tokens = PresentationCodec::encode_p_data(mms, 7U, false);
    CHECK(PresentationCodec::decode_p_data(without_tokens).context_id == 7U);
    const auto direct = PresentationCodec::encode_fully_encoded_data(9U, mms);
    CHECK(PresentationCodec::decode_p_data(direct).context_id == 9U);
}

void malformed_session_presentation_and_acse_are_rejected() {
    const auto duplicate_user_data = from_hex("0D 06 C1 01 00 C1 01 00");
    SessionSpdu session;
    std::string error;
    CHECK(!SessionCodec::try_decode(duplicate_user_data, session, &error));
    CHECK(!error.empty());

    auto request = csharp_default_request();
    request.back() ^= 0xFFU;
    request.resize(request.size() - 1U);
    ar::iec61850::acse::AssociationRequestEnvelope envelope;
    CHECK(!AcseAssociationCodec::try_decode_association_request(
        request, envelope, &error));

    auto aare = AcseAssociationCodec::encode_aare(
        AcseAssociationCodec::default_accept_aare());
    aare.pop_back();
    AcseAare decoded_aare;
    CHECK(!AcseAssociationCodec::try_decode_aare(aare, decoded_aare, &error));

    auto undefined_context = csharp_default_request();
    const auto pdv_context = from_hex("61 62 30 60 02 01 01");
    const auto context_offset = find_sequence(undefined_context, pdv_context);
    CHECK(context_offset < undefined_context.size());
    undefined_context[context_offset + 6U] = 0x09U;
    CHECK(!AcseAssociationCodec::try_decode_association_request(
        undefined_context, envelope, &error));
}

void context_negotiation_rejects_duplicates_and_excessive_definitions() {
    auto contexts = PresentationCodec::default_contexts();
    contexts.push_back(contexts.front());
    check_throws<PresentationFormatError>([&contexts] {
        const auto aarq = AcseAssociationCodec::encode_aarq(
            AcseAssociationCodec::default_balanced_aarq());
        const auto cp = PresentationCodec::encode_cp(contexts, 1U, aarq);
        static_cast<void>(PresentationCodec::decode_cp(cp));
    });

    std::vector<PresentationContextDefinition> excessive;
    excessive.reserve(PresentationCodec::maximum_contexts + 1U);
    for (std::size_t index = 0U;
         index <= PresentationCodec::maximum_contexts; ++index) {
        excessive.push_back(PresentationContextDefinition{
            static_cast<std::uint32_t>(index + 1U),
            PresentationCodec::mms_abstract_syntax_name(),
            {PresentationCodec::ber_transfer_syntax_name()}});
    }
    check_throws<std::invalid_argument>([&excessive] {
        static_cast<void>(PresentationCodec::encode_cp(
            excessive, 1U, from_hex("60 00")));
    });
}

void deterministic_pdv_matrix_preserves_lengths_and_contexts() {
    const std::array<std::size_t, 7> lengths{0U, 1U, 2U, 127U, 128U, 255U, 4096U};
    const std::array<std::uint32_t, 5> contexts{1U, 3U, 5U, 127U, 128U};
    for (const auto length : lengths) {
        std::vector<std::uint8_t> payload(length);
        for (std::size_t index = 0U; index < payload.size(); ++index) {
            payload[index] = static_cast<std::uint8_t>((index * 37U) & 0xFFU);
        }
        for (const auto context : contexts) {
            const auto encoded = PresentationCodec::encode_fully_encoded_data(
                context, payload);
            const auto decoded = PresentationCodec::decode_fully_encoded_data(encoded);
            CHECK(decoded.context_id == context);
            CHECK(decoded.single_asn1_type == payload);
        }
    }
}

} // namespace

int main() {
    const std::array<std::pair<const char*, std::function<void()>>, 9> tests{{
        {"session connect/accept/data transfer", session_connect_accept_and_data_transfer_are_strict},
        {"presentation CP contexts", presentation_cp_decodes_csharp_contexts_and_round_trips},
        {"balanced AARQ golden vector", balanced_aarq_matches_csharp_and_decodes_structurally},
        {"deterministic AARE golden vector", deterministic_aare_response_matches_csharp_and_decodes},
        {"session/context mirroring", response_mirrors_session_parameters_and_negotiated_context_ids},
        {"presentation P-DATA", presentation_p_data_round_trips_mms_payload_and_context},
        {"malformed association rejection", malformed_session_presentation_and_acse_are_rejected},
        {"context negotiation limits", context_negotiation_rejects_duplicates_and_excessive_definitions},
        {"deterministic PDV matrix", deterministic_pdv_matrix_preserves_lengths_and_contexts},
    }};

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " association test groups passed.\n";
    return 0;
}
