// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_control_cancellation.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
using ar::iec61850::time_sync::PtpControlCancellationGate;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

int main() {
    PtpControlCancellationGate gate;
    unsigned stopped = 0U;
    auto stop = [&]() noexcept { ++stopped; };
    CHECK(gate.try_acquire());
    CHECK(!gate.try_acquire());  // concurrent START rejected
    CHECK(!gate.request_stop_and_try_acquire()); // STOP during START
    CHECK(gate.stop_requested()); // status lease immediately revoked
    CHECK(!gate.finish(stop)); // START must acknowledge cancellation
    CHECK(stopped == 1U);
    CHECK(!gate.stop_requested());
    CHECK(gate.try_acquire());
    CHECK(gate.finish(stop));
    CHECK(stopped == 1U);
    CHECK(gate.request_stop_and_try_acquire()); // STOP while idle
    CHECK(!gate.try_acquire()); // blocked through cleanup
    CHECK(!gate.finish(stop));
    CHECK(stopped == 2U);

    CHECK(gate.try_acquire());
    CHECK(!gate.request_stop_and_try_acquire());
    CHECK(!gate.request_stop_and_try_acquire()); // duplicate STOP
    CHECK(!gate.finish([&]() noexcept {
        ++stopped;
        CHECK(!gate.request_stop_and_try_acquire());
        CHECK(gate.stop_requested()); // STOP concurrent with rollback
    }));
    CHECK(stopped == 3U); // one cleanup, all STOPs coalesced
    CHECK(gate.try_acquire());
    CHECK(gate.finish(stop));

    // Repro controlled cross-task ordering, not a timing/sleep heuristic.
    for (unsigned cycle = 0; cycle < 200U; ++cycle) {
        CHECK(gate.try_acquire());
        std::atomic_bool signal{false};
        std::atomic_bool delivered{false};
        std::thread requester([&]() {
            while (!signal.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            CHECK(!gate.request_stop_and_try_acquire());
            delivered.store(true, std::memory_order_release);
        });
        signal.store(true, std::memory_order_release);
        while (!delivered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        CHECK(gate.stop_requested());
        CHECK(!gate.finish(stop));
        requester.join();
        CHECK(!gate.stop_requested());
    }
    CHECK(stopped == 203U);
    std::puts("PTP control START/STOP cancellation handshake: PASS");
}
