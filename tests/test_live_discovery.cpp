// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
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

void csharp_control_block_inventory_cases_are_preserved() {
    {
        mms::MmsLiveDiscoveryResult result;
        result.endpoint = {"192.0.2.10", 102U};
        result.names.domain_variables.emplace(
            "LD0",
            std::vector<std::string>{
                "LLN0$GO$gcbA01$GoEna",
                "LLN0$GO$gcbA01$DatSet",
                "LLN0$MS$msvcb01$SvEna",
                "LLN0$SG$SGCB$ActSG",
                "LLN0$LG$lcbA01$LogEna"});

        const auto model = mms::MmsLiveModelBuilder::build(result);
        CHECK(model.goose_control_blocks.size() == 1U);
        CHECK(model.sampled_value_control_blocks.size() == 1U);
        CHECK(model.setting_group_controls.size() == 1U);
        CHECK(model.log_controls.size() == 1U);
        CHECK(model.goose_control_blocks.front().name == "gcbA01");
        CHECK(std::find(
                  model.goose_control_blocks.front().attributes.begin(),
                  model.goose_control_blocks.front().attributes.end(),
                  "DatSet") != model.goose_control_blocks.front().attributes.end());
        CHECK(model.goose_control_blocks.front().data_set_reference_status ==
              "AttributePresentValueNotRead");
        CHECK(model.goose_control_blocks.front().discovery_status ==
              "AttributeInventoryOnly");
        CHECK(model.coverage.goose_control_block_count == 1U);
        CHECK(model.coverage.sampled_value_control_block_count == 1U);
        CHECK(model.coverage.setting_group_control_count == 1U);
        CHECK(model.coverage.log_control_count == 1U);
        CHECK(model.summary.find("GoCB=1, SVCB=1, SGCB=1, LCB=1") !=
              std::string::npos);
        const auto json = model.to_json();
        CHECK(json.find("\"gooseControlBlocks\":[") != std::string::npos);
        CHECK(json.find("\"sampledValueControlBlocks\":[") != std::string::npos);
        CHECK(json.find("\"CONTROL_BLOCK_VALUE_READ_PENDING\"") !=
              std::string::npos);
    }

    {
        mms::MmsLiveDiscoveryResult result;
        result.endpoint = {"192.0.2.10", 102U};
        result.names.domain_variables.emplace(
            "LD0",
            std::vector<std::string>{
                "LLN0$SP$SGCB$ActSG",
                "LLN0$SP$SGCB$NumOfSG",
                "LLN0$SP$NotSettingGroup$setVal"});

        const auto model = mms::MmsLiveModelBuilder::build(result);
        CHECK(model.setting_group_controls.size() == 1U);
        CHECK(model.setting_group_controls.front().name == "SGCB");
        CHECK(model.setting_group_controls.front().functional_constraint == "SP");
        CHECK(model.coverage.setting_group_control_count == 1U);
    }
}

[[nodiscard]] mms::MmsReportControlCandidate make_rcb_candidate(
    const std::string& name) {
    mms::MmsReportControlCandidate candidate;
    candidate.domain = "IEDLD0";
    candidate.logical_node = "LLN0";
    candidate.functional_constraint = "BR";
    candidate.name = name;
    candidate.reference = "IEDLD0/LLN0.BR." + name;
    candidate.buffered = true;
    candidate.attributes = {"DatSet", "RptEna"};
    return candidate;
}

void dynamic_rcb_binding_states_are_preserved() {
    mms::MmsLiveDiscoveryResult result;
    result.endpoint = {"192.0.2.10", 102U};
    result.names.domain_variables.emplace(
        "IEDLD0", std::vector<std::string>{"LLN0$ST$Mod$stVal"});

    const auto bound = make_rcb_candidate("brcbBound");
    const auto unbound = make_rcb_candidate("brcbUnbound");
    const auto attribute_failed = make_rcb_candidate("brcbDatSetFailed");
    const auto read_failed = make_rcb_candidate("brcbReadFailed");
    const auto not_read = make_rcb_candidate("brcbNotRead");
    result.report_inventory.report_controls = {
        bound, unbound, attribute_failed, read_failed, not_read};

    mms::MmsReportControlEvidence bound_evidence;
    bound_evidence.candidate = bound;
    bound_evidence.requested_attributes = {"DatSet", "RptEna"};
    mms::MmsReportControlState bound_state;
    bound_state.candidate = bound;
    bound_state.data_set_reference = "IEDLD0/LLN0$DataSetA";
    bound_state.report_enabled = false;
    bound_evidence.state = bound_state;

    mms::MmsReportControlEvidence unbound_evidence;
    unbound_evidence.candidate = unbound;
    unbound_evidence.requested_attributes = {"DatSet", "RptEna"};
    mms::MmsReportControlState unbound_state;
    unbound_state.candidate = unbound;
    unbound_state.data_set_reference.clear();
    unbound_state.report_enabled = false;
    unbound_evidence.state = unbound_state;

    mms::MmsReportControlEvidence attribute_failed_evidence;
    attribute_failed_evidence.candidate = attribute_failed;
    attribute_failed_evidence.requested_attributes = {"DatSet", "RptEna"};
    mms::MmsReportControlState attribute_failed_state;
    attribute_failed_state.candidate = attribute_failed;
    attribute_failed_state.diagnostics.push_back("DatSet read failed (3)");
    attribute_failed_evidence.state = attribute_failed_state;

    mms::MmsReportControlEvidence read_failed_evidence;
    read_failed_evidence.candidate = read_failed;
    read_failed_evidence.requested_attributes = {"DatSet", "RptEna"};
    read_failed_evidence.error = "scripted RCB read failure";

    result.report_controls = {
        bound_evidence,
        unbound_evidence,
        attribute_failed_evidence,
        read_failed_evidence};

    const auto model = mms::MmsLiveModelBuilder::build(result);
    CHECK(model.coverage.report_control_count == 5U);
    CHECK(model.coverage.report_control_bound_count == 1U);
    CHECK(model.coverage.report_control_unbound_count == 1U);
    CHECK(model.coverage.report_control_binding_read_failed_count == 2U);
    CHECK(model.coverage.report_control_binding_not_read_count == 1U);

    const auto find_control = [&model](const std::string& name)
        -> const mms::MmsLiveReportControl& {
        const auto found = std::find_if(
            model.report_controls.begin(), model.report_controls.end(),
            [&name](const auto& control) { return control.name == name; });
        if (found == model.report_controls.end()) {
            throw std::runtime_error("Expected scripted RCB was not found in live model.");
        }
        return *found;
    };

    CHECK(find_control("brcbBound").data_set_binding_status == "Bound");
    CHECK(find_control("brcbBound").data_set_reference ==
          "IEDLD0/LLN0.DataSetA");
    CHECK(find_control("brcbUnbound").data_set_binding_status == "Unbound");
    CHECK(find_control("brcbUnbound").data_set_reference.empty());
    CHECK(find_control("brcbDatSetFailed").data_set_binding_status ==
          "ReadFailed");
    CHECK(find_control("brcbReadFailed").data_set_binding_status ==
          "ReadFailed");
    CHECK(find_control("brcbNotRead").data_set_binding_status == "NotRead");
    CHECK(model.summary.find("RCBBound=1, RCBUnbound=1, RCBNotRead=1, RCBReadFailed=2") !=
          std::string::npos);

    const auto json = model.to_json();
    CHECK(json.find("\"dataSetBindingStatus\":\"Bound\"") != std::string::npos);
    CHECK(json.find("\"dataSetBindingStatus\":\"Unbound\"") != std::string::npos);

    auto rebound_result = result;
    rebound_result.report_controls.front().state->data_set_reference.clear();
    const auto rebound_model = mms::MmsLiveModelBuilder::build(rebound_result);
    CHECK(rebound_model.coverage.report_control_unbound_count == 2U);
    CHECK(model.canonical_fingerprint() == rebound_model.canonical_fingerprint());

    CHECK(std::none_of(
        model.warnings.begin(), model.warnings.end(), [](const auto& warning) {
            return warning.code.find("UNBOUND") != std::string::npos ||
                   warning.code.find("NO_DATASET") != std::string::npos;
        }));
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
    CHECK(model.coverage.report_control_bound_count == 1U);
    CHECK(model.coverage.report_control_unbound_count == 0U);
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
    CHECK(result.wire_budget.get_name_list == 3U);
    CHECK(result.wire_budget.get_variable_access_attributes == 1U);
    CHECK(result.wire_budget.get_named_variable_list_attributes == 1U);
    CHECK(result.wire_budget.read_report_controls == 1U);
    CHECK(result.wire_budget.total() == read_only_requests);
}


void case_distinct_references_are_not_deduplicated() {
    // Two valid MMS leaf names differ only in case: both must survive the
    // canonical DA index instead of overwriting one another.
    mms::MmsLiveDiscoveryResult snapshot;
    snapshot.endpoint = {"192.0.2.10", 102U};
    snapshot.names.domain_variables["OLS501LD0"] = {
        "GGIO1$ST$Ind1$stVal", "GGIO1$ST$Ind1$StVal"};
    const auto model = mms::MmsLiveModelBuilder::build(snapshot);
    CHECK(model.coverage.data_attribute_count == 2U);
    CHECK(model.logical_devices.size() == 1U);
    CHECK(model.logical_devices.front().logical_nodes.size() == 1U);
    CHECK(model.logical_devices.front().logical_nodes.front().data_objects.size() == 1U);
    const auto& attributes =
        model.logical_devices.front().logical_nodes.front().data_objects.front().attributes;
    CHECK(attributes.size() == 2U);
    CHECK(attributes.front().attribute_path != attributes.back().attribute_path);
    auto reversed = model;
    auto& reversed_attributes =
        reversed.logical_devices.front().logical_nodes.front().data_objects.front().attributes;
    std::reverse(reversed_attributes.begin(), reversed_attributes.end());
    CHECK(model.structural_manifest() == reversed.structural_manifest());
    CHECK(model.structural_fingerprint() == reversed.structural_fingerprint());
}

void case_distinct_ln_type_probes_are_preserved() {
    ScriptedTransport transport;
    queue_handshake(transport);
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"OLS501LD0"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {2U, {"GGIO1$ST$Ind1$stVal", "ggio1$ST$Ind1$stVal"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {3U, {}, false})));
    for (std::uint32_t id = 4U; id <= 5U; ++id) {
        mms::MmsVariableAccessAttributesResponse type;
        type.invoke_id = id;
        type.type.kind = mms::MmsTypeKind::boolean;
        transport.push_receive(wrap_application(
            mms::MmsServiceCodec::encode_variable_access_attributes_response_p_data(type)));
    }
    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});
    mms::MmsLiveDiscoveryOptions options;
    options.maximum_variable_type_probes = 2U;
    options.read_data_set_directories = false;
    options.probe_report_controls = false;
    const auto result = mms::MmsLiveDiscoveryClient{association}.discover(options);
    CHECK(result.variable_types.size() == 2U);
    CHECK(result.variable_types[0].success());
    CHECK(result.variable_types[1].success());
    CHECK(result.variable_types[0].variable.item == "GGIO1");
    CHECK(result.variable_types[1].variable.item == "ggio1");
    CHECK(result.variable_types[0].variable.reference() !=
          result.variable_types[1].variable.reference());
}

void verified_data_set_hints_restore_late_ln_type_schema() {
    ScriptedTransport transport;
    queue_handshake(transport);
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"OLS501LD0"}, false})));
    // NameList omits CSWI1, but an actual MMS DataSet directory includes
    // CSWI1$ST$Pos (FCD). No SCL/model guess is allowed in this probe.
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {2U, {"LLN0$ST$Mod$stVal"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {3U, {"LLN0$DataSetA"}, false})));
    mms::MmsDataSetDirectoryResponse directory;
    directory.invoke_id = 4U;
    directory.members.push_back({
        mms::MmsObjectName::domain_specific("OLS501LD0", "CSWI1$ST$Pos"),
        "OLS501LD0/CSWI1$ST$Pos", "OLS501LD0/CSWI1.Pos", "ST",
        "CSWI1", "Pos", 100U});
    // Invalid/wrong-domain and wrong-FC members must never schedule GVA.
    directory.members.push_back({
        mms::MmsObjectName::domain_specific("OTHERLD0", "CSWI9$ST$Pos"),
        "OTHERLD0/CSWI9$ST$Pos", "OTHERLD0/CSWI9.Pos", "ST",
        "CSWI9", "Pos", 100U});
    directory.members.push_back({
        mms::MmsObjectName::domain_specific("OLS501LD0", "CSWI8$CF$ctlModel"),
        "OLS501LD0/CSWI8$CF$ctlModel", "OLS501LD0/CSWI8.ctlModel", "CF",
        "CSWI8", "ctlModel", 100U});
    transport.push_receive(wrap_application(
        mms::MmsDataSetDirectoryCodec::encode_response_p_data(directory)));
    mms::MmsVariableAccessAttributesResponse first;
    first.invoke_id = 5U;
    first.type.kind = mms::MmsTypeKind::boolean;
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_variable_access_attributes_response_p_data(first)));

    mms::MmsVariableAccessAttributesResponse late;
    late.invoke_id = 6U;
    late.type.kind = mms::MmsTypeKind::structure;
    mms::MmsTypeSpecification fc;
    fc.kind = mms::MmsTypeKind::structure;
    fc.name = "ST";
    mms::MmsTypeSpecification doSpec;
    doSpec.kind = mms::MmsTypeKind::structure;
    doSpec.name = "Pos";
    mms::MmsTypeSpecification valueSpec;
    valueSpec.kind = mms::MmsTypeKind::boolean;
    valueSpec.name = "stVal";
    mms::MmsTypeSpecification qualitySpec;
    qualitySpec.kind = mms::MmsTypeKind::bit_string;
    qualitySpec.name = "q";
    doSpec.children.push_back(std::move(valueSpec));
    doSpec.children.push_back(std::move(qualitySpec));
    fc.children.push_back(std::move(doSpec));
    late.type.children.push_back(std::move(fc));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_variable_access_attributes_response_p_data(late)));

    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});
    mms::MmsLiveDiscoveryOptions options;
    options.maximum_variable_type_probes = 8U;
    options.maximum_data_set_type_hint_roots = 2U;
    options.use_verified_data_set_type_hints = true;
    options.probe_report_controls = false;
    const auto result = mms::MmsLiveDiscoveryClient{association}.discover(options);
    CHECK(result.variable_types.size() == 2U);
    CHECK(result.variable_types[0].variable.item == "LLN0");
    CHECK(result.variable_types[1].variable.item == "CSWI1");
    CHECK(result.variable_types[1].success());
    CHECK(!result.partial());
    const auto model = mms::MmsLiveModelBuilder::build(result);
    bool found_cswi = false;
    for (const auto& ld : model.logical_devices) {
        for (const auto& ln : ld.logical_nodes) {
            if (ln.name != "CSWI1") continue;
            found_cswi = true;
            for (const auto& object : ln.data_objects) {
                if (object.name != "Pos") continue;
                CHECK(object.attributes.size() >= 2U);
            }
        }
    }
    CHECK(found_cswi);
    CHECK(transport.sent().size() == 8U); // association (2) + 6 reads
    CHECK(result.wire_budget.get_name_list == 3U);
    CHECK(result.wire_budget.get_named_variable_list_attributes == 1U);
    CHECK(result.wire_budget.get_variable_access_attributes == 2U);
    CHECK(result.wire_budget.read_report_controls == 0U);
    CHECK(result.wire_budget.total() == 6U);
}

void disconnected_optional_probes_do_not_inflate_wire_budget() {
    ScriptedTransport transport;
    queue_handshake(transport);
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"OLS501LD0"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {2U, {"LLN0$ST$Mod$stVal", "LLN0$RP$urcbA01$RptEna"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {3U, {"LLN0$DataSetA"}, false})));
    // Do not enqueue an answer to the GVA request: the exchange sends one
    // confirmed request, then times out and closes this association.
    // DataSet and RCB optional phases continue but can no longer send.
    mms::MmsAssociationOptions transportOptions;
    transportOptions.maximum_events = 1U; // Bounded events must not affect accounting.
    mms::MmsAssociationRuntime association{transport, transportOptions};
    association.connect({"127.0.0.1", 102U});

    mms::MmsLiveDiscoveryOptions options;
    options.maximum_variable_type_probes = 1U;
    options.maximum_data_set_directories = 1U;
    options.maximum_report_control_probes = 1U;
    options.continue_on_optional_probe_error = true;
    const auto result = mms::MmsLiveDiscoveryClient{association}.discover(options);

    CHECK(!association.associated());
    CHECK(result.partial());
    CHECK(result.domain_count() == 1U);
    CHECK(result.wire_budget.get_name_list == 3U);
    CHECK(result.wire_budget.get_variable_access_attributes == 1U);
    CHECK(result.wire_budget.get_named_variable_list_attributes == 0U);
    CHECK(result.wire_budget.read_report_controls == 0U);
    CHECK(result.wire_budget.total() == 4U);
    CHECK(transport.sent().size() == 6U); // 2 handshake + 4 actual requests
    CHECK(result.variable_types.size() == 1U);
    CHECK(!result.variable_types.front().success());
    // Inventory still exposes the configured DataSet/RCB identity, while
    // unattempted enrichment remains absent, never synthetic failures.
    CHECK(result.report_inventory.data_sets.size() == 1U);
    CHECK(result.report_inventory.report_controls.size() == 1U);
    CHECK(result.data_set_directories.empty());
    CHECK(result.report_controls.empty());
}

void association_loss_preserves_completed_domain_evidence_without_dead_probes() {
    ScriptedTransport transport;
    queue_handshake(transport);
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"OLS501LD0", "OLS501LD1"}, false})));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {2U, {"LLN0$ST$Mod$stVal"}, false})));
    // Third GetNameList (DataSet names for LD0) is sent but times out.
    // Do not enumerate LD1 after the association is no longer usable.
    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});

    mms::MmsLiveDiscoveryOptions options;
    options.probe_variable_types = false;
    options.read_data_set_directories = false;
    options.probe_report_controls = false;
    const auto result = mms::MmsLiveDiscoveryClient{association}.discover(options);

    CHECK(!association.associated());
    CHECK(result.partial());
    CHECK(result.names.domain_variables.size() == 1U);
    CHECK(result.names.domain_variables.at("OLS501LD0").size() == 1U);
    CHECK(result.names.domain_variable_lists.size() == 1U);
    CHECK(result.names.domain_variable_lists.at("OLS501LD0").empty());
    CHECK(result.names.domain_variables.count("OLS501LD1") == 0U);
    CHECK(result.wire_budget.get_name_list == 3U);
    CHECK(result.wire_budget.get_variable_access_attributes == 0U);
    CHECK(result.wire_budget.get_named_variable_list_attributes == 0U);
    CHECK(result.wire_budget.read_report_controls == 0U);
    CHECK(transport.sent().size() == 5U); // 2 handshake + exactly 3 sends
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
        csharp_control_block_inventory_cases_are_preserved();
        dynamic_rcb_binding_states_are_preserved();
        live_discovery_builds_csharp_compatible_read_only_model();
        case_distinct_references_are_not_deduplicated();
        case_distinct_ln_type_probes_are_preserved();
        verified_data_set_hints_restore_late_ln_type_schema();
        disconnected_optional_probes_do_not_inflate_wire_budget();
        association_loss_preserves_completed_domain_evidence_without_dead_probes();
        pagination_without_forward_progress_is_rejected();
        std::cout << "Live MMS discovery and model parity tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
