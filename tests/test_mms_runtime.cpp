// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/report_subscription_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
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

template <typename Exception, typename Callable>
void check_throws(Callable&& callable) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown.");
}

class ScriptedTransport final : public mms::MmsByteTransport {
public:
    void connect(
        const mms::MmsEndpoint& endpoint,
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("scripted connect cancelled");
        }
        endpoint_ = endpoint;
        connected_ = true;
        ++connect_count_;
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

    [[nodiscard]] ByteVector receive(
        Deadline,
        std::stop_token stop_token) override {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("scripted receive cancelled");
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

    void close() noexcept override {
        connected_ = false;
        ++close_count_;
    }

    [[nodiscard]] bool connected() const noexcept override { return connected_; }

    void push_receive(ByteVector bytes) {
        receive_queue_.push_back(std::move(bytes));
    }

    [[nodiscard]] const std::vector<ByteVector>& sent() const noexcept { return sent_; }
    [[nodiscard]] std::size_t connect_count() const noexcept { return connect_count_; }
    [[nodiscard]] std::size_t close_count() const noexcept { return close_count_; }

private:
    mms::MmsEndpoint endpoint_;
    bool connected_{};
    std::size_t connect_count_{};
    std::size_t close_count_{};
    std::deque<ByteVector> receive_queue_;
    std::vector<ByteVector> sent_;
};

[[nodiscard]] ByteVector wrap_application(
    const std::span<const std::uint8_t> application) {
    const auto cotp = osi::CotpFrameCodec::encode_data(application);
    return osi::TpktFrameCodec::encode(cotp);
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

[[nodiscard]] mms::MmsInformationReport make_report() {
    const ByteVector options{0x7FU, 0x80U};
    const ByteVector time{0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U};
    const ByteVector entry{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    const ByteVector inclusion{0x80U};
    const ByteVector reason{0x80U};
    mms::MmsInformationReport report;
    auto add = [&report](mms::MmsDataValue value) {
        report.items.push_back({report.items.size(), std::move(value), std::nullopt});
    };
    add(mms::MmsDataValue::visible_string("RPT-A"));
    add(mms::MmsDataValue::bit_string(6U, options));
    add(mms::MmsDataValue::unsigned_integer(1U));
    add(mms::MmsDataValue::binary_time(time));
    add(mms::MmsDataValue::visible_string("LD0/LLN0.DataSet"));
    add(mms::MmsDataValue::boolean(false));
    add(mms::MmsDataValue::octet_string(entry));
    add(mms::MmsDataValue::unsigned_integer(1U));
    add(mms::MmsDataValue::bit_string(7U, inclusion));
    add(mms::MmsDataValue::boolean(true));
    add(mms::MmsDataValue::visible_string("LD0/PTOC1.Str.stVal"));
    add(mms::MmsDataValue::bit_string(2U, reason));
    return report;
}

void association_lifecycle_routes_reports_and_confirmed_results() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime runtime{transport};
    runtime.connect({"127.0.0.1", 102U});
    CHECK(runtime.associated());
    CHECK(runtime.negotiated().maximum_mms_pdu_size == 65'000U);
    CHECK(transport.connect_count() == 1U);

    const auto report = mms::MmsInformationReportCodec::encode_p_data(make_report());
    transport.push_receive(wrap_application(report));

    const auto invoke_id = runtime.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(
        mms::MmsObjectName::domain_specific("LD0", "LLN0$ST$Mod$stVal"));
    const auto request_bytes = mms::MmsServiceCodec::encode_read_request_p_data(request);

    mms::MmsReadResponse response;
    response.invoke_id = invoke_id;
    response.results.push_back({mms::MmsDataValue::boolean(true), std::nullopt});
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(response)));

    const auto exchange = runtime.exchange_confirmed(request_bytes, invoke_id);
    CHECK(exchange.envelope.kind == mms::MmsPduKind::confirmed_response);
    CHECK(runtime.queued_information_report_count() == 1U);

    ByteVector queued_report;
    CHECK(runtime.try_pop_information_report(queued_report));
    CHECK(mms::MmsInformationReportCodec::is_information_report(queued_report));
    CHECK(!runtime.try_pop_information_report(queued_report));

    runtime.disconnect();
    CHECK(runtime.state() == mms::MmsAssociationRuntimeState::disconnected);
    CHECK(!transport.connected());
    CHECK(transport.close_count() >= 1U);
}

void association_rejects_cancelled_connect_and_can_reconnect() {
    ScriptedTransport transport;
    mms::MmsAssociationRuntime runtime{transport};
    std::stop_source source;
    source.request_stop();
    check_throws<mms::MmsAssociationRuntimeError>([&] {
        runtime.connect({"127.0.0.1", 102U}, source.get_token());
    });
    CHECK(runtime.state() == mms::MmsAssociationRuntimeState::faulted);

    queue_handshake(transport);
    runtime.connect({"127.0.0.1", 102U});
    CHECK(runtime.associated());
    CHECK(transport.connect_count() == 1U);
}


void confirmed_exchange_timeout_faults_association() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime runtime{transport};
    runtime.connect({"127.0.0.1", 102U});

    const auto invoke_id = runtime.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(
        mms::MmsObjectName::domain_specific("LD0", "LLN0$ST$Mod$stVal"));
    const auto request_bytes = mms::MmsServiceCodec::encode_read_request_p_data(request);
    check_throws<mms::MmsTransportTimeoutError>([&] {
        static_cast<void>(runtime.exchange_confirmed(request_bytes, invoke_id));
    });
    CHECK(runtime.state() == mms::MmsAssociationRuntimeState::faulted);
    CHECK(std::any_of(runtime.events().begin(), runtime.events().end(), [](const auto& event) {
        return event.kind == mms::MmsAssociationEventKind::timed_out;
    }));
}

[[nodiscard]] mms::MmsReportControlCandidate make_urcb_candidate() {
    mms::MmsReportControlCandidate candidate;
    candidate.domain = "LD0";
    candidate.logical_node = "LLN0";
    candidate.functional_constraint = "RP";
    candidate.name = "urcbA01";
    candidate.reference = "LD0/LLN0.RP.urcbA01";
    candidate.buffered = false;
    candidate.attributes = {
        "RptID", "RptEna", "DatSet", "ConfRev", "OptFlds",
        "TrgOps", "GI", "Resv"};
    return candidate;
}

[[nodiscard]] mms::MmsDataSetDirectoryResponse make_directory() {
    mms::MmsDataSetDirectoryResponse directory;
    directory.invoke_id = 1U;
    directory.members.push_back({
        mms::MmsObjectName::domain_specific("LD0", "PTOC1$ST$Str$stVal"),
        "LD0/PTOC1$ST$Str$stVal",
        "LD0/PTOC1.Str.stVal",
        "ST",
        "PTOC1",
        "Str.stVal",
        100U});
    return directory;
}

void queue_probe_response(
    ScriptedTransport& transport,
    const std::uint32_t invoke_id) {
    mms::MmsReadResponse response;
    response.invoke_id = invoke_id;
    response.results = {
        {mms::MmsDataValue::visible_string("RPT-A"), std::nullopt},
        {mms::MmsDataValue::boolean(false), std::nullopt},
        {mms::MmsDataValue::visible_string("LD0/LLN0.DataSet"), std::nullopt},
        {mms::MmsDataValue::unsigned_integer(1U), std::nullopt},
        {mms::MmsDataValue::bit_string(0U, ByteVector{0x5AU, 0x00U}), std::nullopt},
        {mms::MmsDataValue::bit_string(0U, ByteVector{0x40U}), std::nullopt},
        {mms::MmsDataValue::boolean(false), std::nullopt},
        {mms::MmsDataValue::boolean(false), std::nullopt},
    };
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(response)));
}

void queue_write_success(
    ScriptedTransport& transport,
    const std::uint32_t invoke_id) {
    mms::MmsWriteResponse response;
    response.invoke_id = invoke_id;
    response.results.push_back({true, std::nullopt});
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_write_response_p_data(response)));
}

void subscription_runtime_reserves_enables_receives_and_cleans_up() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});

    queue_probe_response(transport, 1U);
    queue_write_success(transport, 2U); // Resv=true
    queue_write_success(transport, 3U); // RptEna=true
    queue_write_success(transport, 4U); // GI=true

    mms::MmsReportSubscriptionRuntime subscription{
        association, make_urcb_candidate(), make_directory()};
    subscription.start();
    CHECK(subscription.active());
    auto active = subscription.snapshot();
    CHECK(active.enabled_by_runtime);
    CHECK(active.reservation_touched);

    transport.push_receive(wrap_application(
        mms::MmsInformationReportCodec::encode_p_data(make_report())));
    CHECK(subscription.poll_once());
    const auto observed = subscription.snapshot();
    CHECK(observed.received_reports == 1U);
    CHECK(observed.decode_failures == 0U);
    CHECK(observed.streams.size() == 1U);

    queue_write_success(transport, 5U); // RptEna=false
    queue_write_success(transport, 6U); // Resv=false
    subscription.stop();
    const auto stopped = subscription.snapshot();
    CHECK(stopped.state == mms::MmsReportSubscriptionState::stopped);
    CHECK(!stopped.enabled_by_runtime);
    CHECK(!stopped.reservation_touched);
    CHECK(!stopped.cleanup_required);
}

void subscription_marks_cleanup_required_when_association_is_lost() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});

    queue_probe_response(transport, 1U);
    queue_write_success(transport, 2U);
    queue_write_success(transport, 3U);
    queue_write_success(transport, 4U);

    mms::MmsReportSubscriptionRuntime subscription{
        association, make_urcb_candidate(), make_directory()};
    subscription.start();
    association.disconnect();
    subscription.stop();
    const auto snapshot = subscription.snapshot();
    CHECK(snapshot.state == mms::MmsReportSubscriptionState::cleanup_required);
    CHECK(snapshot.cleanup_required);
    CHECK(snapshot.enabled_by_runtime);
    CHECK(snapshot.reservation_touched);
}

void subscription_does_not_take_over_an_enabled_rcb() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime association{transport};
    association.connect({"127.0.0.1", 102U});

    mms::MmsReadResponse response;
    response.invoke_id = 1U;
    response.results = {
        {mms::MmsDataValue::visible_string("RPT-A"), std::nullopt},
        {mms::MmsDataValue::boolean(true), std::nullopt},
        {mms::MmsDataValue::visible_string("LD0/LLN0.DataSet"), std::nullopt},
        {mms::MmsDataValue::unsigned_integer(1U), std::nullopt},
        {mms::MmsDataValue::bit_string(0U, ByteVector{0x5AU, 0x00U}), std::nullopt},
        {mms::MmsDataValue::bit_string(0U, ByteVector{0x40U}), std::nullopt},
        {mms::MmsDataValue::boolean(false), std::nullopt},
        {mms::MmsDataValue::boolean(false), std::nullopt},
    };
    transport.push_receive(wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(response)));

    mms::MmsReportSubscriptionRuntime subscription{
        association, make_urcb_candidate(), make_directory()};
    check_throws<mms::MmsReportSubscriptionError>([&] { subscription.start(); });
    CHECK(subscription.state() == mms::MmsReportSubscriptionState::faulted);
    CHECK(transport.sent().size() == 3U); // CR, AARQ, probe only
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"association lifecycle and routing", association_lifecycle_routes_reports_and_confirmed_results},
        {"association cancellation and reconnect", association_rejects_cancelled_connect_and_can_reconnect},
        {"confirmed exchange timeout", confirmed_exchange_timeout_faults_association},
        {"report subscription lifecycle", subscription_runtime_reserves_enables_receives_and_cleans_up},
        {"report cleanup after association loss", subscription_marks_cleanup_required_when_association_is_lost},
        {"report takeover protection", subscription_does_not_take_over_an_enabled_rcb},
    };

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
    std::cout << passed << " MMS runtime tests passed.\n";
    return 0;
}
