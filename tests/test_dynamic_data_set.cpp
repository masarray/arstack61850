// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/dynamic_data_set.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <stop_token>
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
using ar::iec61850::mms::MmsDynamicDataSetOptions;
using ar::iec61850::mms::MmsDynamicDataSetRuntime;
using ar::iec61850::mms::MmsEndpoint;
using ar::iec61850::mms::MmsNamedVariableListCodec;
using ar::iec61850::mms::MmsObjectName;
using ar::iec61850::mms::MmsPduCodec;
namespace acse = ar::iec61850::acse;
namespace osi = ar::iec61850::osi;

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

class ScriptedTransport final : public MmsByteTransport {
public:
    void connect(
        const MmsEndpoint& endpoint,
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("scripted connect cancelled");
        }
        endpoint_ = endpoint;
        connected_ = true;
    }

    void send(
        const std::span<const std::uint8_t> bytes,
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("scripted send cancelled");
        }
        if (!connected_) {
            throw std::runtime_error("scripted transport is disconnected");
        }
        sent_.emplace_back(bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<std::uint8_t> receive(
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("scripted receive cancelled");
        }
        if (!connected_) {
            throw std::runtime_error("scripted transport is disconnected");
        }
        if (receive_queue_.empty()) {
            throw ar::iec61850::mms::MmsTransportTimeoutError(
                "scripted receive timeout");
        }
        auto value = std::move(receive_queue_.front());
        receive_queue_.pop_front();
        return value;
    }

    void close() noexcept override { connected_ = false; }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }

    void push_receive(std::vector<std::uint8_t> bytes) {
        receive_queue_.push_back(std::move(bytes));
    }

    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& sent() const noexcept {
        return sent_;
    }

private:
    MmsEndpoint endpoint_;
    bool connected_{};
    std::deque<std::vector<std::uint8_t>> receive_queue_;
    std::vector<std::vector<std::uint8_t>> sent_;
};

[[nodiscard]] std::vector<std::uint8_t> wrap_application(
    const std::span<const std::uint8_t> application) {
    const auto cotp = osi::CotpFrameCodec::encode_data(application);
    return osi::TpktFrameCodec::encode(cotp);
}

void queue_handshake(ScriptedTransport& transport) {
    const auto request_bytes =
        acse::AcseAssociationCodec::build_default_association_request();
    const auto confirm = osi::CotpFrameCodec::encode_connection_confirm(
        0x0001U, 0x2345U, 0x0AU);
    transport.push_receive(osi::TpktFrameCodec::encode(confirm));

    const auto request = acse::AcseAssociationCodec::decode_association_request(
        request_bytes);
    const auto response = acse::AcseAssociationCodec::build_accept_response(
        request, acse::AcseAssociationCodec::default_accept_aare());
    transport.push_receive(wrap_application(response.payload));
}

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

void ownership_is_invalidated_across_same_endpoint_reconnect() {
    ScriptedTransport transport;
    MmsAssociationRuntime association{transport};
    const MmsEndpoint endpoint{"127.0.0.1", 102U};
    queue_handshake(transport);
    association.connect(endpoint);

    MmsDynamicDataSetOptions options;
    options.verify_after_create = false;
    MmsDynamicDataSetRuntime runtime{association, options};
    const auto request = oracle_define_request();

    transport.push_receive(wrap_application(
        MmsNamedVariableListCodec::encode_define_response_p_data({1U})));
    const auto created = runtime.create(
        "LD0/LLN0.AR_DYN_DS01", request.members);
    CHECK(created.data_set_reference == "LD0/LLN0.AR_DYN_DS01");
    CHECK(runtime.owns(created.data_set_reference));

    association.disconnect();
    queue_handshake(transport);
    association.connect(endpoint);

    CHECK(!runtime.owns(created.data_set_reference));
    const auto sent_before = transport.sent().size();
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(runtime.remove(created.data_set_reference));
    });
    CHECK(transport.sent().size() == sent_before);
    association.disconnect();
}

void oversized_define_is_rejected_before_network_send() {
    ScriptedTransport transport;
    MmsAssociationRuntime association{transport};
    const MmsEndpoint endpoint{"127.0.0.1", 102U};
    queue_handshake(transport);
    association.connect(endpoint);

    MmsDynamicDataSetOptions options;
    options.maximum_members = 512U;
    options.verify_after_create = false;
    MmsDynamicDataSetRuntime runtime{association, options};

    std::vector<MmsObjectName> members;
    members.reserve(300U);
    for (std::size_t index = 0U; index < 300U; ++index) {
        std::string item = "GGIO1$ST$";
        item.append(220U, 'A');
        item += std::to_string(index);
        members.push_back(MmsObjectName::domain_specific("LD0", std::move(item)));
    }

    const auto sent_before = transport.sent().size();
    check_throws<MmsDynamicDataSetError>([&] {
        static_cast<void>(runtime.create(
            "LD0/LLN0.AR_OVERSIZED", members));
    });
    CHECK(transport.sent().size() == sent_before);
    association.disconnect();
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
        ownership_is_invalidated_across_same_endpoint_reconnect();
        oversized_define_is_rejected_before_network_send();
        std::cout << "Dynamic DataSet tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Dynamic DataSet test failure: " << exception.what() << '\n';
        return 1;
    }
}
