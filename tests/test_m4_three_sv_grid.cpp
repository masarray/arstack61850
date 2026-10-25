// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string{"M4 shared grid: "} + #condition + \
                             " line " + std::to_string(__LINE__)); \
} } while (false)

using namespace ar::iec61850::sampled_values;

void verify_synchronous_three_stream_planning() {
    M4ThreeSvGridPlanner scheduler;
    CHECK(scheduler.enabled(0U) && scheduler.enabled(1U) && scheduler.enabled(2U));
    scheduler.reset(123U);
    std::uint32_t generated = 0U;
    for (std::uint64_t tick = 0U; tick < 8000U; ++tick) {
        const auto batch = scheduler.plan({123U, tick, 2U, true});
        CHECK(!batch.timing_discontinuity);
        CHECK(batch.count == 3U);
        CHECK(batch.missed_ticks == 0U);
        for (std::size_t slot = 0U; slot < 3U; ++slot) {
            CHECK(batch.frames[slot].slot == slot);
            CHECK(batch.frames[slot].smp_cnt == (tick % 4000U));
            CHECK(batch.frames[slot].smp_synch == 2U);
        }
        generated += batch.count;
    }
    CHECK(generated == 24000U); // exactly 12000/s across 2 simulated seconds
}

void verify_per_stream_isolation_and_group_revocation() {
    M4ThreeSvGridPlanner planner;
    planner.reset(41U);
    auto batch = planner.plan({41U, 3998U, 1U, true});
    CHECK(batch.count == 3U && batch.frames[0].smp_cnt == 3998U);
    planner.set_enabled(1U, false);
    batch = planner.plan({41U, 3999U, 1U, true});
    CHECK(batch.count == 2U);
    CHECK(batch.frames[0].slot == 0U && batch.frames[1].slot == 2U);
    CHECK(batch.frames[0].smp_cnt == 3999U && batch.frames[1].smp_cnt == 3999U);

    planner.set_enabled(1U, true);
    batch = planner.plan({41U, 4000U, 1U, true});
    CHECK(batch.count == 3U);
    CHECK(batch.frames[1].slot == 1U && batch.frames[1].smp_cnt == 0U);
    CHECK(batch.frames[0].smp_cnt == batch.frames[2].smp_cnt);

    batch = planner.plan({41U, 4004U, 1U, true});
    CHECK(batch.count == 3U && batch.missed_ticks == 3U);
    CHECK(batch.frames[0].smp_cnt == 4U); // skip late samples, no stale replay

    batch = planner.plan({42U, 4005U, 2U, true});
    CHECK(batch.count == 0U && batch.timing_discontinuity);
    batch = planner.plan({42U, 4006U, 2U, true});
    CHECK(batch.count == 0U); // no unqualified automatic re-arm
    planner.reset(42U);
    batch = planner.plan({42U, 4007U, 2U, true});
    CHECK(batch.count == 3U && !batch.timing_discontinuity);

    batch = planner.plan({42U, 4008U, 2U, false});
    CHECK(batch.count == 0U && batch.timing_discontinuity);
    planner.reset(42U);
    batch = planner.plan({42U, 4009U, 0U, true});
    CHECK(batch.count == 3U && batch.frames[0].smp_synch == 0U);
    batch = planner.plan({42U, 4009U, 0U, true});
    CHECK(batch.count == 0U && batch.timing_discontinuity);
}
}

int main() {
    try {
        verify_synchronous_three_stream_planning();
        verify_per_stream_isolation_and_group_revocation();
        std::cout << "PASS M4 shared 4kHz sample grid: three independent slots, "
                     "one epoch/smpCnt/smpSynch, skipped slots and PTP revocation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
