// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"
#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/scl_association.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/tpkt.hpp"
#include "ariec61850/scl/parser.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
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

void queue_compatible_handshake(ScriptedTransport& transport) {
    const auto confirm = osi::CotpFrameCodec::encode_connection_confirm(
        0x0001U, 0x2345U, 0x0AU);
    transport.push_receive(osi::TpktFrameCodec::encode(confirm));

    // The runtime only needs a valid accepted association response. The exact
    // request values under test are decoded from the bytes that runtime sends.
    const auto default_request =
        acse::AcseAssociationCodec::decode_association_request(
            acse::AcseAssociationCodec::build_default_association_request());
    const auto response =
        acse::AcseAssociationCodec::build_accept_response(default_request);
    transport.push_receive(wrap_application(response.payload));
}

[[nodiscard]] bool bytes_equal(
    const std::span<const std::uint8_t> actual,
    const std::initializer_list<std::uint8_t> expected) {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

void parser_and_resolver_preserve_connected_ap_context() {
    constexpr std::string_view xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Communication>
    <SubNetwork name="StationBus" type="8-MMS">
      <ConnectedAP iedName="IED1" apName="P1">
        <Address>
          <P type="IP">192.0.2.10</P>
          <P type="OSI-AP-Title">1,3,9999,23</P>
          <P type="OSI-AE-Qualifier">23</P>
          <P type="OSI-PSEL">00 00 00 23</P>
          <P type="OSI-SSEL">00 23</P>
          <P type="OSI-TSEL">00 23</P>
        </Address>
      </ConnectedAP>
    </SubNetwork>
  </Communication>
  <IED name="IED1"><AccessPoint name="P1"><Server>
    <LDevice inst="LD0"><LN0 lnClass="LLN0"/></LDevice>
  </Server></AccessPoint></IED>
</SCL>)xml";

    const auto document = scl::SclParser{}.parse(xml, "p4-association.scd");
    CHECK(document.mms_access_points.size() == 1U);
    const auto& ap = document.mms_access_points.front();
    CHECK(ap.ied_name == "IED1");
    CHECK(ap.access_point_name == "P1");
    CHECK(ap.ip_address == "192.0.2.10");
    CHECK(ap.tcp_port == 102U);
    CHECK(ap.association_parameters_present);
    CHECK(ap.association_parameters_valid);
    CHECK(ap.ap_title == std::vector<std::uint32_t>({1U, 3U, 9999U, 23U}));
    CHECK(ap.ae_qualifier == std::optional<std::uint32_t>{23U});
    CHECK(ap.p_selector == ByteVector({0x00U, 0x00U, 0x00U, 0x23U}));
    CHECK(ap.s_selector == ByteVector({0x00U, 0x23U}));
    CHECK(ap.t_selector == ByteVector({0x00U, 0x23U}));

    const auto context =
        mms::resolve_scl_association_context(document, "192.0.2.10");
    CHECK(context.selected());
    CHECK(context.ied_name == "IED1");
    CHECK(context.access_point_name == "P1");
    CHECK(context.scl_host == "192.0.2.10");
    CHECK(context.scl_port == 102U);
    CHECK(context.uses_engineering_addressing());
    CHECK(context.addressing->called_ap_title == ap.ap_title);
    CHECK(context.addressing->called_ae_qualifier == ap.ae_qualifier);
    CHECK(context.addressing->called_p_selector == ap.p_selector);
    CHECK(context.addressing->called_s_selector == ap.s_selector);
    CHECK(context.addressing->called_t_selector == ap.t_selector);
}

void explicit_engineering_addressing_reaches_wire_without_fallback() {
    ScriptedTransport transport;
    queue_compatible_handshake(transport);

    mms::MmsAssociationAddressing addressing;
    addressing.called_ap_title = {1U, 3U, 9999U, 23U};
    addressing.called_ae_qualifier = 23U;
    addressing.called_p_selector = {0x00U, 0x00U, 0x00U, 0x23U};
    addressing.called_s_selector = {0x00U, 0x23U};
    addressing.called_t_selector = {0x00U, 0x23U};

    mms::MmsAssociationOptions options;
    options.addressing = addressing;
    mms::MmsAssociationRuntime association{transport, options};
    association.connect({"192.0.2.10", 102U});

    CHECK(association.associated());
    CHECK(association.active_association_profile() == "SclEngineering");
    CHECK(association.association_attempts().size() == 1U);
    CHECK(association.association_attempts().front().accepted);
    CHECK(transport.sent().size() == 2U);

    const auto cotp_frame = osi::TpktFrameCodec::decode(transport.sent()[0]);
    const auto cotp_request = osi::CotpFrameCodec::decode(cotp_frame.payload);
    CHECK(cotp_request.kind == osi::CotpTpduKind::connection_request);
    const auto source_tsap = cotp_request.parameter(osi::CotpFrameCodec::source_tsap_parameter);
    const auto destination_tsap =
        cotp_request.parameter(osi::CotpFrameCodec::destination_tsap_parameter);
    CHECK(source_tsap.has_value());
    CHECK(destination_tsap.has_value());
    CHECK(bytes_equal(*source_tsap, {0x00U, 0x01U}));
    CHECK(bytes_equal(*destination_tsap, {0x00U, 0x23U}));

    const auto association_frame = osi::TpktFrameCodec::decode(transport.sent()[1]);
    const auto association_cotp = osi::CotpFrameCodec::decode(association_frame.payload);
    CHECK(association_cotp.kind == osi::CotpTpduKind::data);
    const auto request = acse::AcseAssociationCodec::decode_association_request(
        association_cotp.user_data);

    const auto called_session_selector = request.session.parameter(0x34U);
    const auto calling_session_selector = request.session.parameter(0x33U);
    CHECK(called_session_selector.has_value());
    CHECK(calling_session_selector.has_value());
    CHECK(bytes_equal(*called_session_selector, {0x00U, 0x23U}));
    CHECK(bytes_equal(*calling_session_selector, {0x00U, 0x01U}));

    CHECK(bytes_equal(
        request.presentation.called_selector,
        {0x00U, 0x00U, 0x00U, 0x23U}));
    CHECK(bytes_equal(
        request.presentation.calling_selector,
        {0x00U, 0x00U, 0x00U, 0x01U}));

    // 1.3.9999.23 => 2B CE 0F 17 in BER OBJECT IDENTIFIER value form.
    CHECK(bytes_equal(request.aarq.called_ap_title, {0x2BU, 0xCEU, 0x0FU, 0x17U}));
    CHECK(request.aarq.called_ae_qualifier == std::optional<std::uint32_t>{23U});

    // Calling identity is deliberately not borrowed from the server SCL.
    CHECK(request.aarq.calling_ap_title ==
          acse::AcseAssociationCodec::balanced_calling_ap_title());
    CHECK(request.aarq.calling_ae_qualifier == std::optional<std::uint32_t>{12U});
}

void malformed_explicit_scl_addressing_fails_closed() {
    constexpr std::string_view xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
  <Communication><SubNetwork name="S"><ConnectedAP iedName="IED1" apName="P1">
    <Address><P type="IP">192.0.2.10</P><P type="OSI-TSEL">zzzz</P></Address>
  </ConnectedAP></SubNetwork></Communication>
  <IED name="IED1"/>
</SCL>)xml";
    const auto document = scl::SclParser{}.parse(xml, "invalid-p4.scd");
    CHECK(document.mms_access_points.size() == 1U);
    CHECK(!document.mms_access_points.front().association_parameters_valid);

    bool threw{};
    try {
        static_cast<void>(
            mms::resolve_scl_association_context(document, "192.0.2.10"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        parser_and_resolver_preserve_connected_ap_context();
        explicit_engineering_addressing_reaches_wire_without_fallback();
        malformed_explicit_scl_addressing_fails_closed();
        std::cout
            << "SCL_ASSOCIATION_ADDRESSING_PASS parser=pass resolver=pass "
               "tsel=exact ssel=exact psel=exact ap_title=exact ae_qualifier=exact "
               "calling_identity=default attempts=1 invalid=fail_closed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
