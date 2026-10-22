// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_external_failover.hpp"
#include "ariec61850/time_sync/ptp_external_detection.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using namespace ar::iec61850::time_sync;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); std::abort(); } } while(false)
static PtpExternalCandidate candidate(const std::uint8_t suffix=1U) {
    PtpExternalCandidate result{};
    result.port_clock={2U,0U,0U,255U,254U,0U,0U,suffix};
    result.grandmaster_clock=result.port_clock;
    result.port_number=1U;
    result.clock_class=6U;
    result.steps_removed=0U;
    return result;
}
static void inspect_frame_admission() {
    PtpExternalListenProfile expected{};
    expected.domain=0U;
    expected.transport_specific=1U;
    expected.vlan_present=true;
    expected.vlan_id=100U;
    expected.local_clock={2U,0U,0U,255U,254U,0U,0U,9U};
    PtpFrame frame{};
    frame.header.message_type=PtpMessageType::announce;
    frame.header.domain_number=0U;
    frame.header.transport_specific=1U;
    frame.header.source_port_identity={candidate().port_clock,1U};
    frame.vlan_id=100U;
    frame.announce.emplace();
    frame.announce->clock_class=6U;
    frame.announce->grandmaster_identity=candidate().grandmaster_clock;
    CHECK(ptp_external_candidate_from_frame(frame,expected).has_value());
    auto mismatch=frame;
    mismatch.header.domain_number=1U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.header.transport_specific=0U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.vlan_id=200U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.vlan_id.reset();
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.outer_vlan_id=100U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.header.source_port_identity.clock_identity=expected.local_clock;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.header.message_type=PtpMessageType::sync;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.announce.reset();
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.announce->clock_class=255U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    mismatch=frame;
    mismatch.header.source_port_identity.port_number=0U;
    CHECK(!ptp_external_candidate_from_frame(mismatch,expected).has_value());
    expected.vlan_present=false;
    CHECK(!ptp_external_candidate_from_frame(frame,expected).has_value());
    frame.vlan_id.reset();
    CHECK(ptp_external_candidate_from_frame(frame,expected).has_value());
}

int main() {
    inspect_frame_admission();
    using H=PtpExternalHandover;
    PtpExternalFailoverPolicy g;
    CHECK(g.evaluate(true,false,false,1000U)==H::none);
    g.set_enabled(true,1000U);
    g.observe_announce(candidate(),1100U);
    g.observe_announce(candidate(),1100U);
    CHECK(g.stable_announces()==1U);
    g.observe_announce(candidate(),2100U);
    CHECK(g.evaluate(true,false,false,2100U)==H::none);
    g.observe_announce(candidate(),3100U);
    CHECK(g.qualified_candidate(3100U));
    CHECK(g.evaluate(true,false,false,3100U)==H::enter_receiver);
    CHECK(g.evaluate(true,true,false,3100U)==H::none);
    g.switched_to_receiver(3300U);
    CHECK(g.evaluate(false,true,false,3400U)==H::none);
    CHECK(g.evaluate(false,true,false,15000U)==H::none);
    CHECK(g.evaluate(false,true,false,15300U)==H::return_source);
    g.switched_to_source(15400U);
    g.observe_announce(candidate(),16000U);
    g.observe_announce(candidate(),17000U);
    g.observe_announce(candidate(),18000U);
    CHECK(g.evaluate(true,false,false,18000U)==H::none);
    CHECK(g.evaluate(true,false,false,35400U)==H::none);
    g.observe_announce(candidate(),35500U);
    g.observe_announce(candidate(),36500U);
    g.observe_announce(candidate(),37500U);
    CHECK(g.evaluate(true,false,false,37500U)==H::enter_receiver);
    g.switched_to_receiver(38000U);
    CHECK(g.evaluate(false,true,true,39000U)==H::none);
    CHECK(g.evaluate(false,true,false,40000U)==H::none);
    CHECK(g.evaluate(false,true,false,44999U)==H::none);
    CHECK(g.evaluate(false,true,false,45000U)==H::return_source);
    g.switched_to_receiver(50000U);
    CHECK(g.evaluate(false,true,true,51000U)==H::none);
    CHECK(g.evaluate(false,true,false,52000U)==H::none);
    CHECK(g.evaluate(false,true,true,53000U)==H::none);
    CHECK(g.evaluate(false,true,false,54000U)==H::none);
    CHECK(g.evaluate(false,true,false,58999U)==H::none);
    CHECK(g.evaluate(false,true,false,59000U)==H::return_source);
    g.set_enabled(true,60000U);
    g.observe_announce(candidate(),60000U);
    g.observe_announce(candidate(2U),61000U);
    CHECK(g.stable_announces()==1U);
    g.observe_announce(candidate(2U),65010U);
    CHECK(g.stable_announces()==1U);
    auto bad=candidate();bad.clock_class=255U;g.observe_announce(bad,66000U);
    bad=candidate();bad.port_clock.fill(0U);g.observe_announce(bad,67000U);
    bad=candidate();bad.grandmaster_clock.fill(0U);g.observe_announce(bad,68000U);
    bad=candidate();bad.steps_removed=255U;g.observe_announce(bad,69000U);
    CHECK(g.stable_announces()==1U);
    g.observe_announce(candidate(),70000U);
    g.observe_announce(candidate(),71000U);
    g.observe_announce(candidate(),72000U);
    CHECK(g.qualified_candidate(72000U));
    g.set_enabled(false,73000U);
    CHECK(!g.qualified_candidate(73000U));
    CHECK(g.evaluate(true,false,false,73000U)==H::none);
    std::puts("PTP external-GM failover state machine PASS");
    return 0;
}
