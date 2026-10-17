// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_receiver_clock_authority.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using ar::iec61850::time_sync::PtpReceiverClockAuthority;
using ar::iec61850::time_sync::PtpReceiverClockObservation;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::abort(); } } while (false)
int main() {
    PtpReceiverClockAuthority authority;
    PtpReceiverClockObservation sample{};
    CHECK(!authority.observe(sample).clock_usable);
    CHECK(authority.observe(sample).generation == 0U);
    sample.runtime_ready = true;
    auto pending = authority.observe(sample);
    CHECK(!pending.clock_usable && pending.generation != 0U);
    sample.source_selected = true;
    sample.source_clock_identity = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    sample.source_port_number = 1U;
    auto acquiring = authority.observe(sample);
    CHECK(!acquiring.clock_usable);
    CHECK(acquiring.generation != pending.generation);
    sample.discipline_locked = true;
    auto locked = authority.observe(sample);
    CHECK(locked.clock_usable);
    CHECK(locked.generation != acquiring.generation);
    CHECK(authority.observe(sample).generation == locked.generation);

    sample.clock_discontinuity = true;
    auto stepped = authority.observe(sample);
    CHECK(!stepped.clock_usable && stepped.generation != locked.generation);
    sample.clock_discontinuity = false;
    CHECK(!authority.observe(sample).clock_usable); // stale LOCKED
    sample.discipline_locked = false;
    auto reacquiring = authority.observe(sample);
    CHECK(!reacquiring.clock_usable);
    sample.discipline_locked = true;
    auto recovered = authority.observe(sample);
    CHECK(recovered.clock_usable && recovered.generation != reacquiring.generation);

    sample.source_clock_identity[7] = 9U;
    auto changed_gm = authority.observe(sample);
    CHECK(!changed_gm.clock_usable);
    CHECK(changed_gm.generation != recovered.generation);
    sample.discipline_locked = false;
    CHECK(!authority.observe(sample).clock_usable);
    sample.discipline_locked = true;
    CHECK(authority.observe(sample).clock_usable);

    sample.source_port_number = 2U;
    CHECK(!authority.observe(sample).clock_usable);
    sample.discipline_locked = false;
    CHECK(!authority.observe(sample).clock_usable);
    sample.discipline_locked = true;
    CHECK(authority.observe(sample).clock_usable);

    sample.clock_discontinuity = true; // failed frequency actuation
    CHECK(!authority.observe(sample).clock_usable);
    sample.clock_discontinuity = false;
    CHECK(!authority.observe(sample).clock_usable);
    sample.discipline_locked = false;
    static_cast<void>(authority.observe(sample));
    sample.discipline_locked = true;
    CHECK(authority.observe(sample).clock_usable);

    sample.discipline_locked = false; // HOLDOVER / unlock
    CHECK(!authority.observe(sample).clock_usable);
    sample.source_selected = false; // source timeout
    CHECK(!authority.observe(sample).clock_usable);
    sample.runtime_ready = false; // STOP
    const auto stopped = authority.observe(sample);
    CHECK(!stopped.clock_usable);
    sample.runtime_ready = true; // rapid restart
    sample.source_selected = true;
    const auto restarting = authority.observe(sample);
    CHECK(!restarting.clock_usable);
    CHECK(restarting.generation != stopped.generation);
    sample.discipline_locked = true;
    CHECK(authority.observe(sample).clock_usable);
    sample.runtime_ready = false; // MONITOR adapter never admits
    CHECK(!authority.observe(sample).clock_usable);
    std::puts("PTP receiver clock authority fencing: PASS");
    return 0;
}
