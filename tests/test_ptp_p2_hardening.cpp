// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_discipline.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850::time_sync;
using Clock = std::chrono::steady_clock;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

[[nodiscard]] PtpClockIdentity identity(const std::uint8_t suffix) {
    return {0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U, 0x00U, suffix};
}

[[nodiscard]] PtpPortIdentity port(
    const std::uint8_t suffix,
    const std::uint16_t number = 1U) {
    return {identity(suffix), number};
}

void cold_epoch_can_acquire_once_then_lock() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 2U;
    options.sync_timeout = std::chrono::milliseconds{1000};
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    constexpr std::int64_t cold_epoch_offset_ns =
        -1'800'000'000'000'000'000LL;
    const auto acquisition = discipline.observe(
        PtpOffsetMeasurement{
            port(0x70U),
            1U,
            cold_epoch_offset_ns,
            500LL,
            true,
        },
        t0);

    CHECK(acquisition.kind == PtpClockCommandKind::step_phase);
    CHECK(acquisition.phase_step_ns == 1'800'000'000'000'000'000LL);
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    CHECK(discipline.status().phase_steps == 1U);
    CHECK(discipline.status().accepted_samples == 1U);
    CHECK(discipline.status().rejected_samples == 0U);
    CHECK(!discipline.measured_smp_synch().has_value());

    for (std::uint16_t sequence = 2U; sequence <= 3U; ++sequence) {
        const auto command = discipline.observe(
            PtpOffsetMeasurement{
                port(0x70U),
                sequence,
                500LL,
                500LL,
                true,
            },
            t0 + std::chrono::milliseconds{
                100LL * static_cast<long long>(sequence - 1U)});
        CHECK(command.kind == PtpClockCommandKind::set_frequency);
    }

    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() ==
          SmpSynchValue::global_synchronized);
}

void large_epoch_step_is_one_shot_and_never_allowed_post_lock() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    constexpr std::int64_t huge = -1'700'000'000'000'000'000LL;

    CHECK(discipline.observe(
              PtpOffsetMeasurement{port(0x71U), 1U, huge, 500LL, false},
              t0).kind == PtpClockCommandKind::step_phase);

    // A second epoch-sized correction before a qualified normal sample is not
    // accepted silently. The acquisition escape hatch is intentionally one-shot.
    const auto repeated = discipline.observe(
        PtpOffsetMeasurement{port(0x71U), 2U, huge, 500LL, false},
        t0 + std::chrono::milliseconds{1});
    CHECK(repeated.kind == PtpClockCommandKind::none);
    CHECK(discipline.status().state == PtpDisciplineState::fault);

    discipline.reset();
    CHECK(discipline.observe(
              PtpOffsetMeasurement{port(0x71U), 3U, 500LL, 500LL, false},
              t0 + std::chrono::milliseconds{10}).kind ==
          PtpClockCommandKind::set_frequency);
    CHECK(discipline.status().state == PtpDisciplineState::locked);

    const auto post_lock = discipline.observe(
        PtpOffsetMeasurement{port(0x71U), 4U, huge, 500LL, false},
        t0 + std::chrono::milliseconds{20});
    CHECK(post_lock.kind == PtpClockCommandKind::none);
    CHECK(discipline.status().state == PtpDisciplineState::fault);
    CHECK(!discipline.measured_smp_synch().has_value());
}

void global_provenance_revokes_immediately_and_requires_new_measurement() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    CHECK(discipline.observe(
              PtpOffsetMeasurement{port(0x72U), 1U, 500LL, 500LL, true},
              t0).kind == PtpClockCommandKind::set_frequency);
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() ==
          SmpSynchValue::global_synchronized);

    discipline.revoke_global_traceability();
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() ==
          SmpSynchValue::local_synchronized);

    // Fresh Announce evidence alone is intentionally not enough to promote 2;
    // the discipline receives no API to restore global provenance except a
    // subsequent qualified timing measurement carrying that fresh evidence.
    CHECK(discipline.measured_smp_synch() ==
          SmpSynchValue::local_synchronized);

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x72U), 2U, 400LL, 500LL, true},
        t0 + std::chrono::milliseconds{100}));
    CHECK(discipline.measured_smp_synch() ==
          SmpSynchValue::global_synchronized);
}

void path_delay_lifetime_tracks_configured_exchange_cadence() {
    using namespace std::chrono_literals;
    const PtpTimeReceiverOptions slow_profile{
        0U,
        0U,
        {},
        3000ms,
        7500ms,
    };
    CHECK(slow_profile.path_delay_timeout == 15000ms);
    CHECK(slow_profile.path_delay_timeout > 10000ms);

    const PtpTimeReceiverOptions fast_profile{
        0U,
        0U,
        {},
        3000ms,
        75ms,
    };
    CHECK(fast_profile.path_delay_timeout == 150ms);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"cold epoch acquisition", cold_epoch_can_acquire_once_then_lock},
        {"one-shot and post-lock guard", large_epoch_step_is_one_shot_and_never_allowed_post_lock},
        {"global provenance revoke", global_provenance_revokes_immediately_and_requires_new_measurement},
        {"path-delay cadence", path_delay_lifetime_tracks_configured_exchange_cadence},
    };

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "Passed " << passed << '/' << tests.size()
              << " PTP-P2 hardening tests.\n";
    return 0;
}
