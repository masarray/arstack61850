// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_ptp_clock_lease.hpp"
#include "ariec61850/time_sync/sv_ptp_phase_authority.hpp"
#include <cstdio>
#include <cstdlib>
using namespace ar::iec61850::time_sync;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

int main() {
    SvPtpClockLease src{};
    src.role = PtpAuthorityRole::source;
    src.generation = 7U;
    src.clock_usable = true;
    CHECK(valid_sv_ptp_clock_lease(src));
    CHECK(same_sv_ptp_clock_lease(src,src));
    auto swapped = src;
    swapped.role = PtpAuthorityRole::receiver;
    swapped.source_selected = true;
    swapped.source_port = 1U;
    swapped.source_clock_identity = {1U,2U,3U,4U,5U,6U,7U,8U};
    CHECK(valid_sv_ptp_clock_lease(swapped));
    CHECK(!same_sv_ptp_clock_lease(src,swapped)); // same generation, wrong role
    CHECK(!same_sv_ptp_clock_lease(swapped,src));
    auto gm = swapped;
    gm.source_clock_identity[7] = 9U;
    CHECK(!same_sv_ptp_clock_lease(swapped,gm)); // GM handoff
    gm = swapped;
    gm.source_port = 2U;
    CHECK(!same_sv_ptp_clock_lease(swapped,gm)); // same GM, new port
    gm = swapped;
    ++gm.generation;
    CHECK(!same_sv_ptp_clock_lease(swapped,gm)); // clock step / relock
    gm = swapped;
    gm.clock_usable = false;
    CHECK(!same_sv_ptp_clock_lease(swapped,gm)); // holdover/timeout
    gm = swapped;
    gm.source_selected = false;
    CHECK(!valid_sv_ptp_clock_lease(gm));
    gm = swapped;
    gm.source_port = 0U;
    CHECK(!valid_sv_ptp_clock_lease(gm));
    gm = swapped;
    gm.source_clock_identity = {};
    CHECK(!valid_sv_ptp_clock_lease(gm));
    gm = swapped;
    gm.generation = 0U;
    CHECK(!valid_sv_ptp_clock_lease(gm));
    src.clock_usable = false;
    CHECK(!valid_sv_ptp_clock_lease(src));
    src.clock_usable = true;
    CHECK(same_sv_ptp_clock_lease(src,src));

    // Even a previously qualified phase controller must cold-acquire after
    // a role/GM/relock fence violation; no stale PI steering crosses it.
    SvPtpPhaseAuthority phase;
    SvPtpPhaseObservation bad{100000U,100008U,0U,1U,0U,100000000U};
    CHECK(!phase.observe(bad).accepted); // invalid PTP timestamp
    SvPtpPhaseObservation a{1100000U,1100008U,1000000U,1U,101U,100000000U};
    CHECK(phase.observe(a).accepted);
    a = {2100000U,2100008U,2000000U,2U,102U,100000000U};
    CHECK(phase.observe(a).accepted);
    CHECK(!same_sv_ptp_clock_lease(swapped,gm));
    CHECK(phase.invalidate().slew_ticks_next_epoch == 0);
    a.last_sv_epoch_generation = 3U;
    a.timer_before_ticks += 1000000U;
    a.timer_after_ticks += 1000000U;
    a.last_sv_epoch_ticks += 1000000U;
    ++a.ptp_seconds;
    const auto cold = phase.observe(a);
    CHECK(cold.accepted && !cold.epoch_second_continuous &&
          cold.slew_ticks_next_epoch == 0);
    std::puts("SV receiver PTP lease fence: PASS");
    return 0;
}
