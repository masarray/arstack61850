// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/scl_assisted_connect.hpp"
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
        if (!connected_) throw std::runtime_error("scripted transport disconnected");
        sent_.emplace_back(bytes.begin(), bytes.end());
    }

    [[nodiscard]] ByteVector receive(
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw mms::MmsTransportCancelledError("scripted receive cancelled");
        }
        if (!connected_) throw std::runtime_error("scripted transport disconnected");
        if (receive_queue_.empty()) {
            throw mms::MmsTransportTimeoutError("scripted receive timeout");
        }
        auto value = std::move(receive_queue_.front());
        receive_queue_.pop_front();
        return value;
    }

    void close() noexcept override { connected_ = false; }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }

    void push_receive(ByteVector bytes) { receive_queue_.push_back(std::move(bytes)); }

    [[nodiscard]] const std::vector<ByteVector>& sent() const noexcept { return sent_; }

private:
    mms::MmsEndpoint endpoint_;
    bool connected_{};
    std::deque<ByteVector> receive_queue_;
    std::vector<ByteVector> sent_;
};

[[nodiscard]] ByteVector wrap_application(
    const std::span<const std::uint8_t> application) {
    return osi::TpktFrameCodec::encode(osi::CotpFrameCodec::encode_data(application));
}

void queue_handshake(ScriptedTransport& transport) {
    const auto confirm = osi::CotpFrameCodec::encode_connection_confirm(
        0x0001U, 0x2345U, 0x0AU);
    transport.push_receive(osi::TpktFrameCodec::encode(confirm));

    const auto request_bytes = acse::AcseAssociationCodec::build_default_association_request();
    const auto request = acse::AcseAssociationCodec::decode_association_request(request_bytes);
    const auto response = acse::AcseAssociationCodec::build_accept_response(request);
    transport.push_receive(wrap_application(response.payload));
}

[[nodiscard]] scl::SclDataSetEntry leaf(
    const std::string& ln_class,
    const std::string& ln_inst,
    const std::string& functional_constraint,
    const std::string& do_name,
    const std::string& da_name,
    const std::size_t index) {
    scl::SclDataSetEntry entry;
    entry.index = index;
    entry.signal_reference =
        "IED1LD0/" + ln_class + ln_inst + "." + do_name + "." + da_name;
    entry.ied_name = "IED1";
    entry.ld_inst = "LD0";
    entry.ln_class = ln_class;
    entry.ln_inst = ln_inst;
    entry.do_name = do_name;
    entry.da_name = da_name;
    entry.functional_constraint = functional_constraint;
    entry.basic_type = "BOOLEAN";
    return entry;
}

[[nodiscard]] scl::SclDocument make_document() {
    scl::SclDocument document;
    document.ieds.push_back(scl::SclIed{"IED1", "ARStack", "TEST", "1"});
    document.logical_nodes.push_back(
        scl::SclLogicalNode{"IED1", "LD0", "", "LLN0", "", "LLN0"});
    document.logical_nodes.push_back(
        scl::SclLogicalNode{"IED1", "LD0", "", "XCBR", "1", "XCBR1"});

    const std::vector<std::string> lln0_fc{
        "BR", "CF", "CO", "DC", "EX", "OR", "RP", "SE", "SG", "SP", "ST"};
    std::size_t entry_index = 0U;
    for (const auto& fc : lln0_fc) {
        document.model_entries.push_back(
            leaf("LLN0", "", fc, "Do" + fc, "stVal", entry_index++));
    }

    // Two expected leaves under one FC root. The scripted response intentionally
    // returns one DA child only under Pos, proving mapping fails closed with no
    // partial leaf update.
    document.model_entries.push_back(
        leaf("XCBR", "1", "ST", "Pos", "stVal", entry_index++));
    document.model_entries.push_back(
        leaf("XCBR", "1", "ST", "Pos", "q", entry_index++));
    return document;
}

[[nodiscard]] mms::MmsReadAccessResult successful_root(const bool value) {
    // FC root -> DataObject structure -> DataAttribute scalar.
    return {
        mms::MmsDataValue::structure({
            mms::MmsDataValue::structure({mms::MmsDataValue::boolean(value)})}),
        std::nullopt};
}

void queue_scl_assisted_responses(ScriptedTransport& transport) {
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_get_name_list_response_p_data(
            {1U, {"IED1LD0", "EXTRA_LD"}, false})));

    mms::MmsReadResponse first;
    first.invoke_id = 2U;
    for (std::size_t index = 0U; index < 10U; ++index) {
        first.results.push_back(successful_root(index % 2U == 0U));
    }
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(first)));

    mms::MmsReadResponse second;
    second.invoke_id = 3U;
    second.results.push_back(successful_root(true));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(second)));

    mms::MmsReadResponse third;
    third.invoke_id = 4U;
    // XCBR1$ST expects Pos.{stVal,q}; this contains only Pos.stVal.
    third.results.push_back(successful_root(true));
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(third)));
}

[[nodiscard]] std::vector<mms::MmsPduEnvelope> confirmed_requests(
    const ScriptedTransport& transport) {
    std::vector<mms::MmsPduEnvelope> requests;
    // send[0] = COTP CR, send[1] = association request; confirmed MMS starts at send[2].
    for (std::size_t index = 2U; index < transport.sent().size(); ++index) {
        const auto tpkt = osi::TpktFrameCodec::decode(transport.sent()[index]);
        const auto cotp = osi::CotpFrameCodec::decode(tpkt.payload);
        if (cotp.kind != osi::CotpTpduKind::data) continue;
        const auto envelope = mms::MmsPduCodec::decode_envelope(cotp.user_data);
        if (envelope.kind == mms::MmsPduKind::confirmed_request) {
            requests.push_back(envelope);
        }
    }
    return requests;
}

void planner_keeps_ln_batches_bounded_and_deterministic() {
    const auto document = make_document();
    const auto plan = mms::InitialFcReadPlanner::build(document, "IED1");
    CHECK(plan.ied_name == "IED1");
    CHECK(plan.expected_domains.size() == 1U);
    CHECK(plan.expected_domains.front() == "IED1LD0");
    CHECK(plan.reference_count() == 12U);
    CHECK(plan.batches.size() == 3U);
    CHECK(plan.batches[0].logical_node == "LLN0");
    CHECK(plan.batches[0].references.size() == 10U);
    CHECK(plan.batches[1].logical_node == "LLN0");
    CHECK(plan.batches[1].references.size() == 1U);
    CHECK(plan.batches[2].logical_node == "XCBR1");
    CHECK(plan.batches[2].references.size() == 1U);

    const std::vector<std::string> expected_first{
        "BR", "CF", "CO", "DC", "EX", "OR", "RP", "SE", "SG", "SP"};
    for (std::size_t index = 0U; index < expected_first.size(); ++index) {
        CHECK(plan.batches[0].references[index].functional_constraint == expected_first[index]);
    }
    CHECK(plan.batches[1].references.front().functional_constraint == "ST");
    CHECK(plan.batches[2].references.front().functional_constraint == "ST");
}

void domain_validator_never_silently_merges_identity() {
    const std::vector<std::string> expected{"IED1LD0", "IED1LD1"};
    const std::vector<std::string> online{"IED1LD0", "EXTRA_LD"};
    const auto validation = mms::MmsSclDomainValidator::compare(expected, online);
    CHECK(validation.missing.size() == 1U);
    CHECK(validation.missing.front() == "IED1LD1");
    CHECK(validation.extra.size() == 1U);
    CHECK(validation.extra.front() == "EXTRA_LD");
    CHECK(validation.expected == expected);
    CHECK(validation.online == online);
}

void scl_assisted_runtime_matches_reference_wire_shape() {
    ScriptedTransport transport;
    queue_handshake(transport);
    queue_scl_assisted_responses(transport);

    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});
    CHECK(association.associated());

    const auto document = make_document();
    mms::MmsSclAssistedConnectClient client{association};
    const auto result = client.synchronize(document, "IED1");

    CHECK(result.domain_request_count == 1U);
    CHECK(result.read_request_count == 3U);
    CHECK(result.confirmed_request_count() == 4U);
    CHECK(result.plan.reference_count() == 12U);
    CHECK(result.domains.missing.empty());
    CHECK(result.domains.extra.size() == 1U);
    CHECK(result.domains.extra.front() == "EXTRA_LD");
    CHECK(result.batches.size() == 3U);
    CHECK(result.batches[0].roots.size() == 10U);
    CHECK(result.batches[1].roots.size() == 1U);
    CHECK(result.batches[2].roots.size() == 1U);
    CHECK(result.successful_root_count == 11U);
    CHECK(result.failed_root_count == 1U);
    CHECK(result.mapped_leaf_count == 11U);
    CHECK(result.batches[2].roots.front().read_success());
    CHECK(!result.batches[2].roots.front().mapping_success());
    CHECK(result.batches[2].roots.front().mapped_leaves.empty());
    CHECK(!result.batches[2].roots.front().mapping_error.empty());
    CHECK(result.associated_after_snapshot);
    CHECK(association.associated());

    const auto requests = confirmed_requests(transport);
    CHECK(requests.size() == 4U);
    CHECK(requests[0].service_tag.has_value());
    CHECK(*requests[0].service_tag ==
          static_cast<std::int32_t>(mms::MmsConfirmedService::get_name_list));
    for (std::size_t index = 1U; index < requests.size(); ++index) {
        CHECK(requests[index].service_tag.has_value());
        CHECK(*requests[index].service_tag ==
              static_cast<std::int32_t>(mms::MmsConfirmedService::read));
    }

    // Strong negative assertion for the minimum trusted-SCL initial path.
    CHECK(std::none_of(
        requests.begin(), requests.end(), [](const auto& request) {
            return request.service_tag == static_cast<std::int32_t>(
                       mms::MmsConfirmedService::get_variable_access_attributes) ||
                request.service_tag == static_cast<std::int32_t>(
                       mms::MmsConfirmedService::get_named_variable_list_attributes) ||
                request.service_tag == static_cast<std::int32_t>(
                       mms::MmsConfirmedService::write);
        }));
}

} // namespace

int main() {
    try {
        planner_keeps_ln_batches_bounded_and_deterministic();
        domain_validator_never_silently_merges_identity();
        scl_assisted_runtime_matches_reference_wire_shape();
        std::cout
            << "SCL_ASSISTED_CONNECT_PASS domain_queries=1 reads=3 "
               "max_refs=10 outstanding=1 no_gvaa=true no_dataset_discovery=true "
               "mapping_mismatch=fail_closed association_kept=true\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
