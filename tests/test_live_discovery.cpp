// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;
using ByteVector = std::vector<std::uint8_t>;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

class ScriptedTransport final : public mms::MmsByteTransport {
public:
    void connect(
        const mms::MmsEndpoint& endpoint,
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw mms::MmsTransportCancelledError("scripted connect cancelled");
        }
        endpoint_ = endpoint;
        connected_ = true;
    }

    void send(
        const std::span<const std::uint8_t> bytes,
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw mms::MmsTransportCancelledError("scripted send cancelled");
        }
        if (!connected_) {
            throw std::runtime_error("scripted transport is disconnected");
        }
        sent_.emplace_back(bytes.begin(), bytes.end());
    }

    [[nodiscard]] ByteVector receive(
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw mms::MmsTransportCancelledError("scripted receive cancelled");
        }
        if (!connected_) {
            throw std::runtime_error("scripted transport is disconnected");
        }
        if (receive_queue_.empty()) {
            throw mms::MmsTransportTimeoutError("scripted receive timeout");
        }
        auto value = std::move(receive_queue_.front());
        receive_queue_.pop_front();
        return value;
    }

    void close() noexcept override { connected_ = false; }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }

    void push_receive(ByteVector bytes) {
        receive_queue_.push_back(std::move(bytes));
    }

    [[nodiscard]] const std::vector<ByteVector>& sent() const noexcept {
        return sent_;
    }

private:
    mms::MmsEndpoint endpoint_;
    bool connected_{};
    std::deque<ByteVector> receive_queue_;
    std::vector<ByteVector> sent_;
};

[[nodiscard]] ByteVector wrap_application(
    const std::span<const std::uint8_t> application) {
    return osi::TpktFrameCodec::encode(
        osi::CotpFrameCodec::encode_data(application));
}

void queue_handshake(ScriptedTransport& transport) {
    const auto confirm = osi::CotpFrameCodec::encode_connection_confirm(
        0x0001U, 0x2345U, 0x0AU);
    transport.push_receive(osi::TpktFrameCodec::encode(confirm));

    const auto request_bytes =
        acse::AcseAssociationCodec::build_default_association_request();
    const auto request =
        acse::AcseAssociationCodec::decode_association_request(request_bytes);
    const auto response = acse::AcseAssociationCodec::build_accept_response(request);
    transport.push_receive(wrap_application(response.payload));
}

void queue_discovery_responses(ScriptedTransport& transport) {
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"LD0"}, false})));

    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {2U,
             {"LLN0$ST$Mod$stVal",
              "LLN0$RP$urcbA01$DatSet",
              "LLN0$RP$urcbA01$RptID",
              "LLN0$RP$urcbA01$ConfRev",
              "LLN0$RP$urcbA01$RptEna"},
             false})));

    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {3U, {"LLN0$DataSetA"}, false})));

    mms::MmsVariableAccessAttributesResponse type_response;
    type_response.invoke_id = 4U;
    type_response.mms_deletable = false;
    type_response.type.kind = mms::MmsTypeKind::boolean;
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_variable_access_attributes_response_p_data(
            type_response)));

    mms::MmsDataSetDirectoryResponse directory;
    directory.invoke_id = 5U;
    directory.deletable = false;
    directory.members.push_back({
        mms::MmsObjectName::domain_specific("LD0", "LLN0$ST$Mod$stVal"),
        "LD0/LLN0$ST$Mod$stVal",
        "LD0/LLN0.Mod.stVal",
        "ST",
        "LLN0",
        "Mod.stVal",
        100U});
    transport.push_receive(wrap_application(
        mms::MmsDataSetDirectoryCodec::encode_response_p_data(directory)));

    mms::MmsReadResponse rcb_response;
    rcb_response.invoke_id = 6U;
    rcb_response.results.push_back({
        mms::MmsDataValue::visible_string("LD0/LLN0.DataSetA"), std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::visible_string("RPT-1"), std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::unsigned_integer(1U), std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::boolean(false), std::nullopt});
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(rcb_response)));
}

void live_discovery_builds_bounded_read_only_snapshot() {
    ScriptedTransport transport;
    queue_handshake(transport);
    queue_discovery_responses(transport);

    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});

    mms::MmsLiveDiscoveryOptions options;
    options.maximum_variable_type_probes = 1U;
    options.maximum_data_set_directories = 1U;
    options.maximum_report_control_probes = 1U;

    mms::MmsLiveDiscoveryClient discovery{association};
    const auto result = discovery.discover(options);

    CHECK(result.endpoint.host == "127.0.0.1");
    CHECK(result.domain_count() == 1U);
    CHECK(result.variable_count() == 5U);
    CHECK(result.variable_list_count() == 1U);
    CHECK(result.report_inventory.data_sets.size() == 1U);
    CHECK(result.report_inventory.report_controls.size() == 1U);
    CHECK(result.variable_types.size() == 1U);
    CHECK(result.variable_types.front().success());
    CHECK(result.variable_types.front().attributes->type.kind ==
          mms::MmsTypeKind::boolean);
    CHECK(result.data_set_directories.size() == 1U);
    CHECK(result.data_set_directories.front().success());
    CHECK(result.data_set_directories.front().directory->members.size() == 1U);
    CHECK(result.report_controls.size() == 1U);
    CHECK(result.report_controls.front().success());
    CHECK(result.report_controls.front().state->report_id == "RPT-1");
    CHECK(!result.partial());

    std::size_t read_only_requests = 0U;
    for (std::size_t index = 2U; index < transport.sent().size(); ++index) {
        const auto tpkt = osi::TpktFrameCodec::decode(transport.sent()[index]);
        const auto cotp = osi::CotpFrameCodec::decode(tpkt.payload);
        CHECK(cotp.kind == osi::CotpTpduKind::data);
        const auto envelope = mms::MmsPduCodec::decode_envelope(cotp.user_data);
        CHECK(envelope.kind == mms::MmsPduKind::confirmed_request);
        CHECK(envelope.service_tag.has_value());
        CHECK(*envelope.service_tag !=
              static_cast<std::int32_t>(mms::MmsConfirmedService::write));
        CHECK(*envelope.service_tag ==
                  static_cast<std::int32_t>(mms::MmsConfirmedService::get_name_list) ||
              *envelope.service_tag ==
                  static_cast<std::int32_t>(
                      mms::MmsConfirmedService::get_variable_access_attributes) ||
              *envelope.service_tag ==
                  static_cast<std::int32_t>(
                      mms::MmsConfirmedService::get_named_variable_list_attributes) ||
              *envelope.service_tag ==
                  static_cast<std::int32_t>(mms::MmsConfirmedService::read));
        ++read_only_requests;
    }
    CHECK(read_only_requests == 6U);
}

void pagination_without_forward_progress_is_rejected() {
    ScriptedTransport transport;
    queue_handshake(transport);
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {}, true})));

    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});
    mms::MmsLiveDiscoveryClient discovery{association};

    try {
        static_cast<void>(discovery.discover());
    } catch (const mms::MmsLiveDiscoveryError&) {
        return;
    }
    throw std::runtime_error(
        "Expected pagination-without-progress discovery failure.");
}

} // namespace

int main() {
    try {
        live_discovery_builds_bounded_read_only_snapshot();
        pagination_without_forward_progress_is_rejected();
        std::cout << "Live MMS discovery tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
