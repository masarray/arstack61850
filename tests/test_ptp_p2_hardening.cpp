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

[[nodiscard]] PtpTimestamp ts(const std::uint64_t seconds, const std::uint32_t ns) {
    return {seconds, ns};
}

[[nodiscard]] PtpClockIdentity identity(const std::uint8_t suffix) {
    return {0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U, 0x00U, suffix};
}

[[nodiscard]] PtpPortIdentity port(
    const std::uint8_t suffix,
    const std::uint16_t number = 1U) {
    return {identity(suffix), number};
}

[[nodiscard]] PtpFrame announce_frame(
    const PtpPortIdentity& source,
    const std::int8_t log_message_interval = 0) {
    PtpFrame frame;
    frame.header.message_type = PtpMessageType::announce;
    frame.header.domain_number = 0U;
    frame.header.transport_specific = 0U;
    frame.header.source_port_identity = source;
    frame.header.log_message_interval = log_message_interval;

    PtpAnnounceMessage announce;
    announce.priority1 = 128U;
    announce.priority2 = 128U;
    announce.clock_class = 248U;
    announce.clock_accuracy = PtpClockAccuracy::unknown;
    announce.offset_scaled_log_variance = 0xFFFFU;
    announce.grandmaster_identity = source.clock_identity;
    announce.steps_removed = 0U;
    announce.time_source = PtpTimeSource::internal_oscillator;
    frame.announce = announce;
    return frame;
}

[[nodiscard]] PtpFrame two_step_sync_frame(
    const PtpPortIdentity& source,
    const std::uint16_t sequence_id) {
    PtpFrame frame;
    frame.header.message_type = PtpMessageType::sync;
    frame.header.domain_number = 0U;
    frame.header.transport_specific = 0U;
    frame.header.source_port_identity = source;
    frame.header.sequence_id = sequence_id;
    frame.header.flags = 0x0200U;
    return frame;
}

[[nodiscard]] PtpFrame follow_up_frame(
    const PtpPortIdentity& source,
    const std::uint16_t sequence_id,
    const PtpTimestamp& origin) {
    PtpFrame frame;
    frame.header.message_type = PtpMessageType::follow_up;
    frame.header.domain_number = 0U;
    frame.header.transport_specific = 0U;
    frame.header.source_port_identity = source;
    frame.header.sequence_id = sequence_id;
    frame.timestamp = origin;
    return frame;
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
        PtpOffsetMeasurement{port(0x70U), 1U, cold_epoch_offset_ns, 500LL, true}, t0);
    CHECK(acquisition.kind == PtpClockCommandKind::step_phase);
    CHECK(acquisition.phase_step_ns == 1'800'000'000'000'000'000LL);
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    CHECK(discipline.status().phase_steps == 1U);
    CHECK(discipline.status().accepted_samples == 1U);
    CHECK(discipline.status().rejected_samples == 0U);
    CHECK(!discipline.measured_smp_synch().has_value());

    for (std::uint16_t sequence = 2U; sequence <= 3U; ++sequence) {
        const auto command = discipline.observe(
            PtpOffsetMeasurement{port(0x70U), sequence, 500LL, 500LL, true},
            t0 + std::chrono::milliseconds{100LL * static_cast<long long>(sequence - 1U)});
        CHECK(command.kind == PtpClockCommandKind::set_frequency);
    }
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::global_synchronized);
}

void large_epoch_step_is_one_shot_and_never_allowed_post_lock() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    constexpr std::int64_t huge = -1'700'000'000'000'000'000LL;

    CHECK(discipline.observe(
              PtpOffsetMeasurement{port(0x71U), 1U, huge, 500LL, false}, t0).kind ==
          PtpClockCommandKind::step_phase);
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

void fault_is_latched_until_explicit_reset() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x73U), 1U, 500LL, 500LL, false}, t0));
    CHECK(discipline.status().state == PtpDisciplineState::locked);

    constexpr std::int64_t huge = -1'700'000'000'000'000'000LL;
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x73U), 2U, huge, 500LL, false},
        t0 + std::chrono::milliseconds{10}));
    CHECK(discipline.status().state == PtpDisciplineState::fault);
    const auto phase_steps_before = discipline.status().phase_steps;
    const auto frequency_updates_before = discipline.status().frequency_updates;
    const auto while_faulted = discipline.observe(
        PtpOffsetMeasurement{port(0x73U), 3U, 2'000'000LL, 500LL, false},
        t0 + std::chrono::milliseconds{20});
    CHECK(while_faulted.kind == PtpClockCommandKind::none);
    CHECK(discipline.status().state == PtpDisciplineState::fault);
    CHECK(discipline.status().phase_steps == phase_steps_before);
    CHECK(discipline.status().frequency_updates == frequency_updates_before);

    discipline.reset();
    const auto after_reset = discipline.observe(
        PtpOffsetMeasurement{port(0x73U), 4U, 2'000'000LL, 500LL, false},
        t0 + std::chrono::milliseconds{30});
    CHECK(after_reset.kind == PtpClockCommandKind::step_phase);
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
}

void consecutive_unlock_samples_leave_locked_state() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    options.unlock_required_samples = 3U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x76U), 1U, 500LL, 500LL, true}, t0));
    CHECK(discipline.status().state == PtpDisciplineState::locked);

    constexpr std::int64_t out_of_tolerance_ns = 50'000LL;
    for (std::uint16_t sequence = 2U; sequence <= 3U; ++sequence) {
        static_cast<void>(discipline.observe(
            PtpOffsetMeasurement{port(0x76U), sequence, out_of_tolerance_ns, 500LL, true},
            t0 + std::chrono::milliseconds{10LL * static_cast<long long>(sequence - 1U)}));
        CHECK(discipline.status().state == PtpDisciplineState::locked);
        CHECK(discipline.status().consecutive_bad_samples == sequence - 1U);
    }
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x76U), 4U, out_of_tolerance_ns, 500LL, true},
        t0 + std::chrono::milliseconds{30}));
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    CHECK(discipline.status().consecutive_bad_samples == 3U);
    CHECK(!discipline.status().globally_traceable);
    CHECK(!discipline.measured_smp_synch().has_value());
}

void phase_step_stays_disabled_after_lock_until_reset() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    options.unlock_required_samples = 3U;
    options.sync_timeout = std::chrono::milliseconds{100};
    options.holdover_timeout = std::chrono::milliseconds{500};
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x77U), 1U, 500LL, 500LL, false}, t0));
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    const auto initial_steps = discipline.status().phase_steps;

    for (std::uint16_t sequence = 2U; sequence <= 4U; ++sequence) {
        static_cast<void>(discipline.observe(
            PtpOffsetMeasurement{port(0x77U), sequence, 50'000LL, 500LL, false},
            t0 + std::chrono::milliseconds{10LL * static_cast<long long>(sequence - 1U)}));
    }
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    const auto after_unlock = discipline.observe(
        PtpOffsetMeasurement{port(0x77U), 5U, 2'000'000LL, 500LL, false},
        t0 + std::chrono::milliseconds{40});
    CHECK(after_unlock.kind == PtpClockCommandKind::set_frequency);
    CHECK(discipline.status().phase_steps == initial_steps);

    discipline.reset();
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x77U), 6U, 500LL, 500LL, false},
        t0 + std::chrono::milliseconds{100}));
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    discipline.tick(t0 + std::chrono::milliseconds{201});
    CHECK(discipline.status().state == PtpDisciplineState::holdover);
    const auto holdover_steps = discipline.status().phase_steps;
    const auto after_holdover = discipline.observe(
        PtpOffsetMeasurement{port(0x77U), 7U, 2'000'000LL, 500LL, false},
        t0 + std::chrono::milliseconds{210});
    CHECK(after_holdover.kind == PtpClockCommandKind::set_frequency);
    CHECK(discipline.status().phase_steps == holdover_steps);

    discipline.reset();
    const auto after_explicit_reset = discipline.observe(
        PtpOffsetMeasurement{port(0x77U), 8U, 2'000'000LL, 500LL, false},
        t0 + std::chrono::milliseconds{300});
    CHECK(after_explicit_reset.kind == PtpClockCommandKind::step_phase);
}

void global_provenance_revokes_immediately_and_requires_new_measurement() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    CHECK(discipline.observe(
              PtpOffsetMeasurement{port(0x72U), 1U, 500LL, 500LL, true}, t0).kind ==
          PtpClockCommandKind::set_frequency);
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::global_synchronized);

    discipline.revoke_global_traceability();
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::local_synchronized);
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x72U), 2U, 400LL, 500LL, true},
        t0 + std::chrono::milliseconds{100}));
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::global_synchronized);
}

void stale_same_source_announce_forces_reselection() {
    using namespace std::chrono_literals;
    const auto local = port(0x74U, 2U);
    const auto master = port(0x75U, 1U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{0U, 0U, local, 100ms, 50ms});
    const auto t0 = Clock::time_point{};
    const auto announce = announce_frame(master);
    CHECK(receiver.observe_announce(announce, t0));
    CHECK(!receiver.observe_announce(announce, t0 + 50ms));
    CHECK(receiver.status().selected_source == master);
    CHECK(receiver.observe_announce(announce, t0 + 151ms));
    CHECK(receiver.status().selected_source == master);
    CHECK(!receiver.observe_announce(announce, t0 + 152ms));
}

void slow_announce_cadence_extends_source_evidence_lifetime() {
    using namespace std::chrono_literals;
    const auto local = port(0x78U, 2U);
    const auto master = port(0x79U, 1U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{0U, 0U, local, 3000ms, 500ms});
    const auto t0 = Clock::time_point{};

    // logMessageInterval=3 advertises an 8 s nominal Announce cadence. The
    // receiver therefore allows 24 s (three missed periods), which safely
    // accommodates the product's 10 s slow-cadence profile without falsely
    // dropping and reacquiring the same healthy source between messages.
    const auto slow_announce = announce_frame(master, 3);
    CHECK(receiver.observe_announce(slow_announce, t0));
    CHECK(!receiver.tick(t0 + 9999ms));
    CHECK(!receiver.observe_announce(slow_announce, t0 + 10000ms));
    CHECK(receiver.status().selected_source == master);
    CHECK(!receiver.tick(t0 + 33000ms));
    CHECK(receiver.status().selected_source == master);
    CHECK(receiver.tick(t0 + 34001ms));
    CHECK(!receiver.status().selected_source.has_value());
}

void path_delay_lifetime_tracks_configured_exchange_cadence() {
    using namespace std::chrono_literals;
    const PtpTimeReceiverOptions slow_profile{0U, 0U, {}, 3000ms, 7500ms};
    CHECK(slow_profile.path_delay_timeout == 15000ms);
    CHECK(slow_profile.path_delay_timeout > 10000ms);
    const PtpTimeReceiverOptions fast_profile{0U, 0U, {}, 3000ms, 75ms};
    CHECK(fast_profile.path_delay_timeout == 150ms);
}

void active_receiver_counts_replaced_pending_sync() {
    using namespace std::chrono_literals;
    const auto local = port(0x7AU, 2U);
    const auto master = port(0x7BU, 1U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{0U, 0U, local, 3000ms, 500ms});
    const auto t0 = Clock::time_point{};
    CHECK(receiver.observe_announce(announce_frame(master), t0));

    // Owning a Pdelay request marks this as an active TIME_RECEIVER-style
    // correlation path even before a valid path-delay result is available.
    receiver.note_pdelay_request(1U, ts(1U, 0U), t0 + 1ms);
    CHECK(!receiver.observe_sync(
        two_step_sync_frame(master, 10U), ts(2U, 100U), t0 + 2ms).has_value());
    CHECK(receiver.status().rejected_exchanges == 0U);

    CHECK(!receiver.observe_sync(
        two_step_sync_frame(master, 11U), ts(2U, 200U), t0 + 3ms).has_value());
    CHECK(receiver.status().sync_frames == 2U);
    CHECK(receiver.status().rejected_exchanges == 1U);
}

void passive_sync_observation_does_not_create_false_rejections() {
    using namespace std::chrono_literals;
    const auto local = port(0x7CU, 2U);
    const auto master = port(0x7DU, 1U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{0U, 0U, local, 3000ms, 100ms});
    const auto t0 = Clock::time_point{};
    CHECK(receiver.observe_announce(announce_frame(master), t0));

    CHECK(!receiver.observe_sync(
        two_step_sync_frame(master, 20U), ts(3U, 100U), t0 + 1ms).has_value());
    CHECK(!receiver.observe_follow_up(
        follow_up_frame(master, 20U, ts(3U, 0U)), t0 + 2ms).has_value());
    CHECK(receiver.status().sync_frames == 1U);
    CHECK(receiver.status().follow_up_frames == 1U);
    CHECK(receiver.status().rejected_exchanges == 0U);

    // A passive observer may also miss Follow_Up traffic. A later Sync and the
    // timeout cleanup must remain observation-only rather than a receiver fault.
    CHECK(!receiver.observe_sync(
        two_step_sync_frame(master, 21U), ts(4U, 100U), t0 + 3ms).has_value());
    CHECK(!receiver.observe_sync(
        two_step_sync_frame(master, 22U), ts(4U, 200U), t0 + 4ms).has_value());
    CHECK(receiver.status().rejected_exchanges == 0U);
    CHECK(!receiver.tick(t0 + 200ms));
    CHECK(receiver.status().rejected_exchanges == 0U);
}

void rejected_measurements_preserve_qualified_telemetry() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    options.maximum_path_delay_ns = 2'000LL;
    options.maximum_path_delay_jitter_ns = 100LL;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7EU), 1U, 500LL, 500LL, true}, t0));
    CHECK(discipline.status().offset_from_master_ns == 500LL);
    CHECK(discipline.status().mean_path_delay_ns == 500LL);
    CHECK(discipline.status().path_delay_jitter_ns == 0LL);
    CHECK(discipline.status().accepted_samples == 1U);

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7EU), 2U, 9'999LL, 900LL, false},
        t0 + std::chrono::milliseconds{10}));
    CHECK(discipline.status().offset_from_master_ns == 500LL);
    CHECK(discipline.status().mean_path_delay_ns == 500LL);
    CHECK(discipline.status().path_delay_jitter_ns == 0LL);
    CHECK(discipline.status().accepted_samples == 1U);
    CHECK(discipline.status().rejected_samples == 1U);

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7EU), 3U, 8'888LL, 5'000LL, false},
        t0 + std::chrono::milliseconds{20}));
    CHECK(discipline.status().offset_from_master_ns == 500LL);
    CHECK(discipline.status().mean_path_delay_ns == 500LL);
    CHECK(discipline.status().path_delay_jitter_ns == 0LL);
    CHECK(discipline.status().accepted_samples == 1U);
    CHECK(discipline.status().rejected_samples == 2U);
}

void invalid_samples_do_not_seed_unlock_streak() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 1U;
    options.unlock_required_samples = 3U;
    options.invalid_samples_before_fault = 8U;
    options.maximum_path_delay_ns = 1'000LL;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7FU), 1U, 500LL, 500LL, true}, t0));
    CHECK(discipline.status().state == PtpDisciplineState::locked);

    for (std::uint16_t sequence = 2U; sequence <= 3U; ++sequence) {
        static_cast<void>(discipline.observe(
            PtpOffsetMeasurement{port(0x7FU), sequence, 500LL, 5'000LL, true},
            t0 + std::chrono::milliseconds{10LL * static_cast<long long>(sequence - 1U)}));
        CHECK(discipline.status().state == PtpDisciplineState::locked);
        CHECK(discipline.status().consecutive_bad_samples == 0U);
    }

    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7FU), 4U, 50'000LL, 500LL, true},
        t0 + std::chrono::milliseconds{30}));
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.status().consecutive_bad_samples == 1U);

    // An invalid sample breaks the valid high-offset unlock streak.
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7FU), 5U, 50'000LL, 5'000LL, true},
        t0 + std::chrono::milliseconds{40}));
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.status().consecutive_bad_samples == 0U);

    for (std::uint16_t sequence = 6U; sequence <= 7U; ++sequence) {
        static_cast<void>(discipline.observe(
            PtpOffsetMeasurement{port(0x7FU), sequence, 50'000LL, 500LL, true},
            t0 + std::chrono::milliseconds{10LL * static_cast<long long>(sequence - 1U)}));
        CHECK(discipline.status().state == PtpDisciplineState::locked);
        CHECK(discipline.status().consecutive_bad_samples == sequence - 5U);
    }
    static_cast<void>(discipline.observe(
        PtpOffsetMeasurement{port(0x7FU), 8U, 50'000LL, 500LL, true},
        t0 + std::chrono::milliseconds{70}));
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    CHECK(discipline.status().consecutive_bad_samples == 3U);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"cold epoch acquisition", cold_epoch_can_acquire_once_then_lock},
        {"one-shot and post-lock guard", large_epoch_step_is_one_shot_and_never_allowed_post_lock},
        {"fault latch", fault_is_latched_until_explicit_reset},
        {"consecutive unlock", consecutive_unlock_samples_leave_locked_state},
        {"no phase step after lock", phase_step_stays_disabled_after_lock_until_reset},
        {"global provenance revoke", global_provenance_revokes_immediately_and_requires_new_measurement},
        {"stale same-source reselection", stale_same_source_announce_forces_reselection},
        {"slow Announce cadence", slow_announce_cadence_extends_source_evidence_lifetime},
        {"path-delay cadence", path_delay_lifetime_tracks_configured_exchange_cadence},
        {"active Sync replacement rejection", active_receiver_counts_replaced_pending_sync},
        {"passive Sync observation", passive_sync_observation_does_not_create_false_rejections},
        {"qualified telemetry survives rejects", rejected_measurements_preserve_qualified_telemetry},
        {"invalid samples break unlock streak", invalid_samples_do_not_seed_unlock_streak},
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