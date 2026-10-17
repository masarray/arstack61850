// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_emac_role_ownership.hpp"
#include <cstdio>
#include <cstdlib>
using ar::iec61850::time_sync::PtpEmacRoleOwnership;
using ar::iec61850::time_sync::can_change_ptp_emac_role;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

int main() {
    // READY=0 is NOT proof a task has released the EMAC timebase.
    CHECK(can_change_ptp_emac_role({}));
    CHECK(!can_change_ptp_emac_role({true,false,false}));  // SOURCE starting
    CHECK(!can_change_ptp_emac_role({true,false,true}));   // SOURCE/STOP race
    CHECK(!can_change_ptp_emac_role({false,true,false}));  // RECEIVER acquiring
    CHECK(!can_change_ptp_emac_role({false,true,true}));   // RECEIVER cleanup
    CHECK(!can_change_ptp_emac_role({false,false,true}));  // STOP latch
    CHECK(!can_change_ptp_emac_role({true,true,false}));   // invalid coexistence
    CHECK(!can_change_ptp_emac_role({true,true,true}));
    // Only after both workers are finished and STOP latch cleared can
    // SOURCE -> RECEIVER and RECEIVER -> SOURCE proceed.
    CHECK(can_change_ptp_emac_role({false,false,false}));
    std::puts("PTP EMAC role ownership fence: PASS");
}
