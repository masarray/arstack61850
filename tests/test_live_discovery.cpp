// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <cstdint>
#include <deque>
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
            {1U, {"OLS501LD0"}, false})));

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
        mms::MmsObjectName::domain_specific(
            "OLS501LD0", "LLN0$ST$Mod$stVal"),
        "OLS501LD0/LLN0$ST$Mod$stVal",
        "OLS501LD0/LLN0.Mod.stVal",
        "ST",
        "LLN0",
        "Mod.stVal",
        100U});
    transport.push_receive(wrap_application(
        mms::MmsDataSetDirectoryCodec::encode_response_p_data(directory)));

    mms::MmsReadResponse rcb_response;
    rcb_response.invoke_id = 6U;
    rcb_response.results.push_back({
        mms::MmsDataValue::visible_string("OLS501LD0/LLN0.DataSetA"),
        std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::visible_string("RPT-1"), std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::unsigned_integer(1U), std::nullopt});
    rcb_response.results.push_back({
        mms::MmsDataValue::boolean(false), std::nullopt});
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(rcb_response)));
}

[[nodiscard]] mms::MmsLiveModelDocument build_identity_model(
    const std::vector<std::string>& domains,
    const std::string& host = "192.0.2.10") {
    mms::MmsLiveDiscoveryResult result;
    result.endpoint = {host, 102U};
    for (const auto& domain : domains) {
        result.names.domain_variables.emplace(domain, std::vector<std::string>{});
    }
    return mms::MmsLiveModelBuilder::build(result);
}

void csharp_identity_resolver_cases_are_preserved() {
    {
        const auto model = build_identity_model({"OLSF501LD0"});
        CHECK(model.identity.ied_name == "OLSF501");
        CHECK(model.identity.source == "MmsDomainKnownLogicalDeviceSuffix");
        CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::medium);
        CHECK(model.identity.logical_device_aliases.at("OLSF501LD0") == "LD0");
    }

    {
        const auto model = build_identity_model(
            {"OLSF501LD0", "OLSF501PROT", "OLSF501CTRL"});
        CHECK(model.identity.ied_name == "OLSF501");
        CHECK(model.identity.source == "MmsDomainKnownLogicalDeviceSuffix");
        CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::high);
        CHECK(!model.identity.ambiguous);
        CHECK(model.identity.logical_device_aliases.at("OLSF501PROT") == "PROT");
    }

    {
        const auto model = build_identity_model(
            {"OCR7SJ8Application",
             "OCR7SJ8CB1",
             "OCR7SJ8Dc1",
             "OCR7SJ8Mod2_MU1",
             "OCR7SJ8V3p1_5051OC3phase1"},
            "1.110.1.1");
        CHECK(model.identity.ied_name == "OCR7SJ8");
        CHECK(model.identity.source == "MmsDomainCommonPrefix");
        CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::high);
        CHECK(model.identity.logical_device_aliases.at("OCR7SJ8Mod2_MU1") == "Mod2_MU1");
        CHECK(std::find(
                  model.identity.candidate_names.begin(),
                  model.identity.candidate_names.end(),
                  "OCR7SJ8Mod2") == model.identity.candidate_names.end());
    }

    {
        const auto model = build_identity_model({"ALPHA_LD0", "BETA_LD0"});
        CHECK(model.identity.ied_name == "192.0.2.10");
        CHECK(model.identity.source == "MmsDomainAmbiguous");
        CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::low);
        CHECK(model.identity.ambiguous);
        CHECK(model.identity.candidate_names.size() == 2U);
        CHECK(model.identity.candidate_names[0] == "ALPHA");
        CHECK(model.identity.candidate_names[1] == "BETA");
    }

    {
        mms::MmsLiveDiscoveryResult result;
        result.endpoint = {"192.0.2.10", 102U};
        result.names.domain_variables.emplace(
            "OLSF501LD0", std::vector<std::string>{});
        mms::MmsLiveModelBuildOptions options;
        options.explicit_ied_name = "COMMISSIONED_IED";
        const auto model = mms::MmsLiveModelBuilder::build(result, options);
        CHECK(model.identity.ied_name == "COMMISSIONED_IED");
        CHECK(model.identity.source == "ExplicitOverride");
        CHECK(model.identity.confidence == mms::MmsLiveModelConfidence::exact);
    }
}

void live_discovery_builds_csharp_compatible_read_only_model() {
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

    CHECK(result.domain_count() == 1U);
    CHECK(result.variable_count() == 5U);
    CHECK(result.variable_types.size() == 1U);
    CHECK(result.variable_types.front().success());
    CHECK(result.variable_types.front().variable.item == "LLN0");
    CHECK(result.data_set_directories.size() == 1U);
    CHECK(result.report_controls.size() == 1U);
    CHECK(!result.partial());

    const auto model = mms::MmsLiveModelBuilder::build(result);
    CHECK(model.schema_version == "live-ied-model-v1");
    CHECK(model.identity.ied_name == "OLS501");
    CHECK(model.identity.source == "MmsDomainKnownLogicalDeviceSuffix");
    CHECK(model.identity.logical_device_aliases.at("OLS501LD0") == "LD0");
    CHECK(model.coverage.logical_device_count == 1U);
    CHECK(model.coverage.logical_node_count == 1U);
    CHECK(model.coverage.data_attribute_count >= 1U);
    CHECK(model.coverage.data_set_count == 1U);
    CHECK(model.coverage.report_control_count == 1U);
    CHECK(model.canonical_fingerprint() != 0U);
    CHECK(model.to_json().find("\"schemaVersion\":\"live-ied-model-v1\"") !=
          std::string::npos);

    const auto same = mms::MmsLiveModelParityComparer::compare(model, model);
    CHECK(same.compatible());
    CHECK(same.blocking_finding_count() == 0U);

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
        csharp_identity_resolver_cases_are_preserved();
        live_discovery_builds_csharp_compatible_read_only_model();
        pagination_without_forward_progress_is_rejected();
        std::cout << "Live MMS discovery and model parity tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
