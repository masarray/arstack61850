// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/sv_tx_fault_burst.hpp"

#include <stdexcept>
#include <string>

using ar::iec61850::sampled_values::SvTxFaultBurst;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string{"failed: "} + #condition + " at line " + std::to_string(__LINE__)); \
} while (false)

int main() {
    SvTxFaultBurst recorder;
    auto initial = recorder.snapshot();
    CHECK(initial.current_streak == 0U);
    CHECK(initial.stop_sequence == 0U);

    // One failure revokes sync exactly once per contiguous burst; subsequent
    // 399 misses are still counted and must trigger the unchanged fail-closed
    // threshold in the firmware publisher.
    CHECK(recorder.failure(0x107, true));
    for (int i = 1; i < 399; ++i)
        CHECK(!recorder.failure(0x106, true));
    CHECK(!recorder.failure(0x106, false));
    auto failed = recorder.snapshot();
    CHECK(failed.current_streak == 400U);
    CHECK(failed.failure_bursts == 1U);
    CHECK(failed.failed_frames == 400U);
    CHECK(failed.first_error == 0x107);
    CHECK(failed.last_error == 0x106);
    CHECK(!failed.last_link_up);

    recorder.stopped(1308494U);
    auto stop = recorder.snapshot();
    CHECK(stop.stop_sequence == 1U);
    CHECK(stop.stop_streak == 400U);
    CHECK(stop.stop_first_error == 0x107);
    CHECK(stop.stop_last_error == 0x106);
    CHECK(stop.stop_uptime_ms == 1308494U);
    CHECK(!stop.stop_link_up);
    recorder.new_session();
    stop = recorder.snapshot();
    CHECK(stop.current_streak == 0U);
    CHECK(stop.stop_streak == 400U);
    CHECK(stop.stop_sequence == 1U);

    // Transient MAC error that recovers must not be rewritten as a fatal STOP.
    CHECK(recorder.failure(0x105, true));
    recorder.recovered();
    recorder.recovered();
    auto recovered = recorder.snapshot();
    CHECK(recovered.current_streak == 0U);
    CHECK(recovered.recovered_bursts == 1U);
    CHECK(recovered.failure_bursts == 2U);
    CHECK(recovered.stop_sequence == 1U);
    CHECK(recovered.failed_frames == 401U);

    CHECK(recorder.failure(0x108, true));
    recorder.stopped(1309000U);
    stop = recorder.snapshot();
    CHECK(stop.stop_sequence == 2U);
    CHECK(stop.stop_streak == 1U);
    CHECK(stop.stop_first_error == 0x108);
    CHECK(stop.stop_last_error == 0x108);
    CHECK(stop.stop_link_up);
    CHECK(stop.stop_uptime_ms == 1309000U);
    return 0;
}
