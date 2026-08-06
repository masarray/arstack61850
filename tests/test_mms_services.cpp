// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/invoke_router.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ar::iec61850::mms::MmsConfirmedError;
using ar::iec61850::mms::MmsConfirmedRequest;
using ar::iec61850::mms::MmsConfirmedResponse;
using ar::iec61850::mms::MmsDataKind;
using ar::iec61850::mms::MmsDataValue;
using ar::iec61850::mms::MmsFormatError;
using ar::iec61850::mms::MmsGetNameListObjectClass;
using ar::iec61850::mms::MmsGetNameListRequest;
using ar::iec61850::mms::MmsGetNameListResponse;
using ar::iec61850::mms::MmsInitiateRequest;
using ar::iec61850::mms::MmsInitiateResponse;
using ar::iec61850::mms::MmsInvokeRouteAction;
using ar::iec61850::mms::MmsInvokeRouter;
using ar::iec61850::mms::MmsObjectName;
using ar::iec61850::mms::MmsObjectScopeKind;
using ar::iec61850::mms::MmsPduCodec;
using ar::iec61850::mms::MmsPduEnvelope;
using ar::iec61850::mms::MmsPduKind;
using ar::iec61850::mms::MmsReadAccessResult;
using ar::iec61850::mms::MmsReadRequest;
using ar::iec61850::mms::MmsReadResponse;
using ar::iec61850::mms::MmsServiceCodec;
using ar::iec61850::mms::MmsTypeKind;
using ar::iec61850::mms::MmsTypeSpecification;
using ar::iec61850::mms::MmsVariableAccessAttributesRequest;
using ar::iec61850::mms::MmsVariableAccessAttributesResponse;
using ar::iec61850::mms::MmsWriteAccessResult;
using ar::iec61850::mms::MmsWriteRequest;
using ar::iec61850::mms::MmsWriteResponse;

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

void initiate_request_response_match_csharp_vectors() {
    const auto request_vector = from_hex(
        "A8 26 80 03 00 FD E8 81 01 0A 82 01 0A 83 01 05 "
        "A4 16 80 01 01 81 03 05 F1 00 82 0C 03 EE 1C 00 00 04 08 00 00 79 EF 18");
    const auto response_vector = from_hex(
        "A9 26 80 03 00 FD E8 81 01 0A 82 01 0A 83 01 05 "
        "A4 16 80 01 01 81 03 05 F1 00 82 0C 03 EE 1C 00 00 04 08 00 00 79 EF 18");

    const auto request = MmsPduCodec::default_initiate_request();
    const auto response = MmsPduCodec::default_initiate_response();
    CHECK(MmsPduCodec::encode_initiate_request(request) == request_vector);
    CHECK(MmsPduCodec::encode_initiate_response(response) == response_vector);
    CHECK(MmsPduCodec::decode_initiate_request(request_vector) == request);
    CHECK(MmsPduCodec::decode_initiate_response(response_vector) == response);
    CHECK(ar::iec61850::acse::AcseAssociationCodec::default_mms_initiate_request() == request_vector);
    CHECK(ar::iec61850::acse::AcseAssociationCodec::default_mms_initiate_response() == response_vector);
}

void initiate_and_confirmed_envelopes_are_strict() {
    auto request = MmsPduCodec::default_initiate_request();
    request.proposed_maximum_mms_pdu_size = 16'384U;
    request.proposed_maximum_outstanding_calling = 4U;
    const auto encoded = MmsPduCodec::encode_initiate_request(request);
    CHECK(MmsPduCodec::decode_initiate_request(encoded) == request);

    auto truncated = encoded;
    truncated.pop_back();
    check_throws<MmsFormatError>([&truncated] {
        static_cast<void>(MmsPduCodec::decode_initiate_request(truncated));
    });

    const MmsConfirmedRequest confirmed_request{17U, 4, true, from_hex("A1 00")};
    const auto request_pdu = MmsPduCodec::encode_confirmed_request(confirmed_request);
    CHECK(MmsPduCodec::decode_confirmed_request(request_pdu) == confirmed_request);

    const MmsConfirmedResponse confirmed_response{17U, 4, true, from_hex("A1 00")};
    const auto response_pdu = MmsPduCodec::encode_confirmed_response(confirmed_response);
    CHECK(MmsPduCodec::decode_confirmed_response(response_pdu) == confirmed_response);

    const MmsConfirmedError confirmed_error{17U, 7, 10U};
    const auto error_pdu = MmsPduCodec::encode_confirmed_error(confirmed_error);
    CHECK(MmsPduCodec::decode_confirmed_error(error_pdu) == confirmed_error);
    const auto envelope = MmsPduCodec::decode_envelope(
        MmsPduCodec::wrap_p_data(error_pdu, 5U));
    CHECK(envelope.kind == MmsPduKind::confirmed_error);
    CHECK(envelope.invoke_id == 17U);
}

void object_names_round_trip_all_choices() {
    const std::vector<MmsObjectName> names{
        MmsObjectName::vmd("Temperature"),
        MmsObjectName::domain_specific("IED1LD0", "LLN0$ST$Mod$stVal"),
        MmsObjectName::aa("AssociationObject")};

    for (const auto& name : names) {
        const auto encoded = MmsServiceCodec::encode_object_name(name);
        CHECK(MmsServiceCodec::decode_object_name(encoded) == name);
    }
    CHECK(names[1].reference() == "IED1LD0/LLN0$ST$Mod$stVal");
    check_throws<MmsFormatError>([] {
        static_cast<void>(MmsServiceCodec::encode_object_name(
            MmsObjectName::domain_specific("", "item")));
    });
}

void get_name_list_request_response_round_trip() {
    const MmsGetNameListRequest request{
        21U,
        MmsGetNameListObjectClass::named_variable,
        MmsObjectScopeKind::domain_specific,
        "IED1LD0",
        "LLN0"};
    const auto p_data = MmsServiceCodec::encode_get_name_list_request_p_data(request, 7U);
    CHECK(MmsServiceCodec::decode_get_name_list_request(p_data) == request);
    const auto envelope = MmsPduCodec::decode_envelope(p_data);
    CHECK(envelope.kind == MmsPduKind::confirmed_request);
    CHECK(envelope.invoke_id == 21U);
    CHECK(envelope.service_tag == 1);

    const MmsGetNameListResponse response{21U, {"LLN0", "PTOC1", "XCBR1"}, true};
    const auto response_p_data = MmsServiceCodec::encode_get_name_list_response_p_data(response, 7U);
    CHECK(MmsServiceCodec::decode_get_name_list_response(response_p_data, 21U) == response);
    check_throws<MmsFormatError>([&response_p_data] {
        static_cast<void>(MmsServiceCodec::decode_get_name_list_response(response_p_data, 22U));
    });
}

void type_specification_and_attributes_round_trip() {
    MmsTypeSpecification structure;
    structure.kind = MmsTypeKind::structure;
    MmsTypeSpecification status;
    status.kind = MmsTypeKind::boolean;
    status.name = "stVal";
    MmsTypeSpecification measurement;
    measurement.kind = MmsTypeKind::floating_point;
    measurement.name = "mag";
    measurement.size = 32U;
    measurement.exponent_width = 8U;
    structure.children = {status, measurement};

    const auto encoded_type = MmsServiceCodec::encode_type_specification(structure);
    const auto decoded_type = MmsServiceCodec::decode_type_specification(encoded_type);
    CHECK(decoded_type == structure);
    CHECK(decoded_type.signature() == "structure(stVal:boolean,mag:floating-point)");
    CHECK(decoded_type.scl_basic_type() == "Struct");
    CHECK(decoded_type.children[1].scl_basic_type() == "FLOAT32");

    const MmsVariableAccessAttributesRequest request{
        31U, MmsObjectName::domain_specific("IED1LD0", "PTOC1$ST$Op")};
    const auto request_p_data =
        MmsServiceCodec::encode_variable_access_attributes_request_p_data(request);
    CHECK(MmsServiceCodec::decode_variable_access_attributes_request(request_p_data) == request);

    const MmsVariableAccessAttributesResponse response{31U, false, structure};
    const auto response_p_data =
        MmsServiceCodec::encode_variable_access_attributes_response_p_data(response);
    CHECK(MmsServiceCodec::decode_variable_access_attributes_response(
        response_p_data, 31U) == response);
}

void array_type_specification_round_trips() {
    MmsTypeSpecification element;
    element.kind = MmsTypeKind::visible_string;
    element.name = "element";
    element.size = 64U;
    MmsTypeSpecification array;
    array.kind = MmsTypeKind::array;
    array.size = 8U;
    array.children = {element};

    const auto decoded = MmsServiceCodec::decode_type_specification(
        MmsServiceCodec::encode_type_specification(array));
    CHECK(decoded == array);
    CHECK(decoded.signature() == "array(element:visible-string)");
}

void read_request_and_multi_access_response_round_trip() {
    const MmsReadRequest request{
        41U,
        true,
        {MmsObjectName::domain_specific("IED1LD0", "XCBR1$ST$Pos$stVal"),
         MmsObjectName::domain_specific("IED1LD0", "XCBR1$ST$Pos$q")}};
    const auto request_p_data = MmsServiceCodec::encode_read_request_p_data(request, 3U);
    const auto decoded_request = MmsServiceCodec::decode_read_request(request_p_data);
    CHECK(decoded_request == request);

    MmsReadResponse response;
    response.invoke_id = 41U;
    response.results.push_back({MmsDataValue::integer(2), std::nullopt});
    response.results.push_back({std::nullopt, 10U});
    const auto response_p_data = MmsServiceCodec::encode_read_response_p_data(response);
    const auto decoded_response = MmsServiceCodec::decode_read_response(response_p_data, 41U);
    CHECK(decoded_response.invoke_id == 41U);
    CHECK(decoded_response.results.size() == 2U);
    CHECK(decoded_response.results[0].success());
    CHECK(decoded_response.results[0].value->kind() == MmsDataKind::integer);
    CHECK(std::get<std::int64_t>(decoded_response.results[0].value->value()) == 2);
    CHECK(!decoded_response.results[1].success());
    CHECK(decoded_response.results[1].failure_code == 10U);
}

void read_decoder_accepts_direct_primitive_data() {
    MmsReadResponse response;
    response.invoke_id = 42U;
    response.results.push_back({MmsDataValue::boolean(false), std::nullopt});
    const auto decoded = MmsServiceCodec::decode_read_response(
        MmsServiceCodec::encode_read_response_pdu(response), 42U);
    CHECK(decoded.results.size() == 1U);
    CHECK(decoded.results[0].value->kind() == MmsDataKind::boolean);
    CHECK(!std::get<bool>(decoded.results[0].value->value()));
}

void write_request_and_access_results_round_trip() {
    MmsWriteRequest request;
    request.invoke_id = 51U;
    request.variables = {
        MmsObjectName::domain_specific("IED1LD0", "LLN0$CF$Mod$setVal"),
        MmsObjectName::domain_specific("IED1LD0", "XCBR1$CO$Pos$ctlVal")};
    request.values = {MmsDataValue::integer(1), MmsDataValue::boolean(true)};

    const auto request_p_data = MmsServiceCodec::encode_write_request_p_data(request, 9U);
    const auto decoded_request = MmsServiceCodec::decode_write_request(request_p_data);
    CHECK(decoded_request.invoke_id == request.invoke_id);
    CHECK(decoded_request.variables == request.variables);
    CHECK(decoded_request.values.size() == 2U);
    CHECK(decoded_request.values[0].kind() == MmsDataKind::integer);
    CHECK(decoded_request.values[1].kind() == MmsDataKind::boolean);

    const MmsWriteResponse response{
        51U,
        {MmsWriteAccessResult{true, std::nullopt},
         MmsWriteAccessResult{false, 3U}}};
    const auto decoded_response = MmsServiceCodec::decode_write_response(
        MmsServiceCodec::encode_write_response_p_data(response), 51U);
    CHECK(decoded_response.results == response.results);
    CHECK(!decoded_response.all_success());
}

void invoke_router_routes_by_invoke_and_bounds_queues() {
    MmsInvokeRouter router{3U, 2U};
    const MmsGetNameListResponse response{61U, {"LD0"}, false};
    const auto encoded = MmsServiceCodec::encode_get_name_list_response_p_data(response);
    const auto routed = router.route(encoded);
    CHECK(routed.action == MmsInvokeRouteAction::queued_confirmed_result);
    CHECK(router.queued_confirmed_count() == 1U);

    MmsPduEnvelope envelope;
    CHECK(!router.try_dequeue(62U, envelope));
    CHECK(router.try_dequeue(61U, envelope));
    CHECK(envelope.kind == MmsPduKind::confirmed_response);
    CHECK(envelope.invoke_id == 61U);
    CHECK(router.queued_confirmed_count() == 0U);

    const auto initiate = MmsPduCodec::encode_initiate_response(
        MmsPduCodec::default_initiate_response());
    CHECK(router.route(initiate).action == MmsInvokeRouteAction::queued_unmatched);
    CHECK(router.try_dequeue_unmatched(envelope));
    CHECK(envelope.kind == MmsPduKind::initiate_response);

    static_cast<void>(router.route(encoded));
    static_cast<void>(router.route(encoded));
    check_throws<MmsFormatError>([&router, &encoded] {
        static_cast<void>(router.route(encoded));
    });
}

void malformed_and_limit_cases_are_rejected() {
    MmsWriteRequest mismatched;
    mismatched.invoke_id = 1U;
    mismatched.variables = {MmsObjectName::domain_specific("LD0", "item")};
    check_throws<MmsFormatError>([&mismatched] {
        static_cast<void>(MmsServiceCodec::encode_write_request_pdu(mismatched));
    });

    std::string long_name(MmsServiceCodec::maximum_identifier_bytes + 1U, 'A');
    check_throws<MmsFormatError>([&long_name] {
        static_cast<void>(MmsServiceCodec::encode_object_name(MmsObjectName::vmd(long_name)));
    });

    const auto request = MmsServiceCodec::encode_read_request_pdu(
        {71U, false, {MmsObjectName::domain_specific("LD0", "LLN0$ST$Mod$stVal")}});
    auto trailing = request;
    trailing.push_back(0U);
    check_throws<MmsFormatError>([&trailing] {
        static_cast<void>(MmsServiceCodec::decode_read_request(trailing));
    });

    MmsTypeSpecification nested;
    nested.kind = MmsTypeKind::visible_string;
    nested.name = "leaf";
    for (std::size_t index = 0; index < MmsServiceCodec::maximum_type_depth + 2U; ++index) {
        MmsTypeSpecification parent;
        parent.kind = MmsTypeKind::array;
        parent.size = 1U;
        parent.children = {nested};
        nested = std::move(parent);
    }
    check_throws<MmsFormatError>([&nested] {
        static_cast<void>(MmsServiceCodec::encode_type_specification(nested));
    });
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"initiate request/response golden vectors", initiate_request_response_match_csharp_vectors},
        {"strict initiate and confirmed envelopes", initiate_and_confirmed_envelopes_are_strict},
        {"ObjectName choices", object_names_round_trip_all_choices},
        {"GetNameList", get_name_list_request_response_round_trip},
        {"VariableAccessAttributes and structure type", type_specification_and_attributes_round_trip},
        {"array TypeSpecification", array_type_specification_round_trips},
        {"Read multi-access results", read_request_and_multi_access_response_round_trip},
        {"Read direct primitive", read_decoder_accepts_direct_primitive_data},
        {"Write", write_request_and_access_results_round_trip},
        {"invoke router", invoke_router_routes_by_invoke_and_bounds_queues},
        {"malformed and limits", malformed_and_limit_cases_are_rejected},
    };

    std::size_t failed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
        }
    }
    std::cout << (tests.size() - failed) << '/' << tests.size()
              << " Phase 3C test groups passed.\n";
    return failed == 0U ? 0 : 1;
}
