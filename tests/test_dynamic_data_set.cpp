// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/dynamic_data_set.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ar::iec61850::mms::MmsAssociationRuntime;
using ar::iec61850::mms::MmsByteTransport;
using ar::iec61850::mms::MmsDataSetDirectoryCodec;
using ar::iec61850::mms::MmsDefineNamedVariableListRequest;
using ar::iec61850::mms::MmsDefineNamedVariableListResponse;
using ar::iec61850::mms::MmsDeleteNamedVariableListRequest;
using ar::iec61850::mms::MmsDeleteNamedVariableListResponse;
using ar::iec61850::mms::MmsDynamicDataSetError;
using ar::iec61850::mms::MmsDynamicDataSetRuntime;
using ar::iec61850::mms::MmsEndpoint;
using ar::iec61850::mms::MmsNamedVariableListCodec;
using ar::iec61850::mms::MmsObjectName;
using ar::iec61850::mms::MmsPduCodec;

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

bool contains_ascii(
    const std::vector<std::uint8_t>& bytes,
    const std::string& text) {
    const std::vector<std::uint8_t> needle{text.begin(), text.end()};
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

class NullTransport final : public MmsByteTransport {
public:
    void connect(const MmsEndpoint&, Deadline, std::stop_token) override {}
    void send(std::span<const std::uint8_t>, Deadline, std::stop_token) override {}
    [[nodiscard]] std::vector<std::uint8_t> receive(Deadline, std::stop_token) override {
        return {};
    }
    void close() noexcept override {}
    [[nodiscard]] bool connected() const noexcept override { return false; }
};

MmsDefineNamedVariableListRequest oracle_define_request() {
    MmsDefineNamedVariableListRequest request;
    request.invoke_id = 8U;
    request.data_set_name = MmsDataSetDirectoryCodec::parse_data_set_reference(
        "LD0/LLN0.AR_DYN_DS01");
    request.members = {
        MmsObjectName::domain_specific("LD0", "PTOC1$ST$Str$stVal"),
        MmsObjectName::domain_specific("LD0", "MMXU1$MX$PhV$phsA$cVal$mag$f")};
    return request;
}

void define_request_matches_csharp_shape_and_round_trips() {
    const auto request = oracle_define_request();
    const auto encoded = MmsNamedVariableListCodec::encode_define_request_pdu(request);
    const auto generic = MmsPduCodec::decode_confirmed_request(encoded);

    CHECK(generic.invoke_id == 8U);
    CHECK(generic.service_tag == MmsNamedVariableListCodec::define_service_tag);
    CHECK(generic.service_constructed);
    CHECK(contains_ascii(encoded, "AR_DYN_DS01"));
    CHECK(contains_ascii(encoded, "PTOC1$ST$Str$stVal"));
    CHECK(contains_ascii(encoded, "MMXU1$MX$PhV$phsA$cVal$mag$f"));

    const auto decoded = MmsNamedVariableListCodec::decode_define_request(encoded);
    CHECK(decoded == request);
}

void define_response_accepts_and_emits_csharp_oracle_vector() {
    const auto oracle = from_hex("A10502010A8B00");
    const auto wrapped = MmsPduCodec::wrap_p_data(oracle);
    const auto decoded = MmsNamedVariableListCodec::decode_define_response(wrapped, 10U);
    CHECK(decoded.invoke_id == 10U);

    const auto encoded = MmsNamedVariableListCodec::encode_define_response_pdu({10U});
    CHECK(encoded == oracle);
}

void delete_request_matches_csharp_shape_and_round_trips() {
    const MmsDeleteNamedVariableListRequest request{
        11U,
        MmsDataSetDirectoryCodec::parse_data_set_reference(
            "LD0/GGIO1.AR_DYN_DS01")};
    const auto encoded = MmsNamedVariableListCodec::encode_delete_request_pdu(request);
    const auto generic = MmsPduCodec::decode_confirmed_request(encoded);

    CHECK(generic.invoke_id == 11U);
    CHECK(generic.service_tag == MmsNamedVariableListCodec::delete_service_tag);
    CHECK(generic.service_constructed);
    CHECK(contains_ascii(encoded, "GGIO1$AR_DYN_DS01"));

    const auto decoded = MmsNamedVariableListCodec::decode_delete_request(encoded);
    CHECK(decoded == request);
}

void delete_response_reads_and_emits_csharp_oracle_counts() {
    const auto oracle = from_hex("A10B02010BAD06800101810101");
    const auto wrapped = MmsPduCodec::wrap_p_data(oracle);
    const auto decoded = MmsNamedVariableListCodec::decode_delete_response(wrapped, 11U);
    CHECK(decoded.number_matched == 1U);
    CHECK(decoded.number_deleted == 1U);
    CHECK(decoded.deleted());

    const MmsDeleteNamedVariableListResponse response{
        11U, 1U, 1U};
    const auto encoded = MmsNamedVariableListCodec::encode_delete_response_pdu(response);
    CHECK(encoded == oracle);
}

void invalid_or_unsafe_requests_fail_closed() {
    auto empty = oracle_define_request();
    empty.members.clear();
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(MmsNamedVariableListCodec::encode_define_request_pdu(empty));
    });

    auto duplicate = oracle_define_request();
    duplicate.members.push_back(duplicate.members.front());
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(MmsNamedVariableListCodec::encode_define_request_pdu(duplicate));
    });

    auto vmd = oracle_define_request();
    vmd.data_set_name = MmsObjectName::vmd("AR_DYN_DS01");
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(MmsNamedVariableListCodec::encode_define_request_pdu(vmd));
    });

    const auto oracle = from_hex("A10502010A8B00");
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(MmsNamedVariableListCodec::decode_define_response(oracle, 99U));
    });
}

void runtime_requires_live_association_before_mutation() {
    NullTransport transport;
    MmsAssociationRuntime association{transport};
    MmsDynamicDataSetRuntime runtime{association};
    const auto request = oracle_define_request();
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(runtime.create(
            "LD0/LLN0.AR_DYN_DS01", request.members));
    });
}

} // namespace

int main() {
    try {
        define_request_matches_csharp_shape_and_round_trips();
        define_response_accepts_and_emits_csharp_oracle_vector();
        delete_request_matches_csharp_shape_and_round_trips();
        delete_response_reads_and_emits_csharp_oracle_counts();
        invalid_or_unsafe_requests_fail_closed();
        runtime_requires_live_association_before_mutation();
        std::cout << "Dynamic DataSet tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Dynamic DataSet test failure: " << exception.what() << '\n';
        return 1;
    }
}
