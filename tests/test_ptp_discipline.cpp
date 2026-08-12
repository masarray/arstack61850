// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_discipline.hpp"

#include <array>
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

[[nodiscard]] PtpPortIdentity port(const std::uint8_t suffix, const std::uint16_t number = 1U) {
    return {identity(suffix), number};
}

[[nodiscard]] std::vector<std::uint8_t> pdelay_body(
    const PtpTimestamp& timestamp,
    const PtpPortIdentity& requester) {
    std::vector<std::uint8_t> body(20U, 0U);
    CHECK(timestamp.write(std::span<std::uint8_t>{body}.first(10U)));
    for (std::size_t index = 0U; index < requester.clock_identity.size(); ++index) {
        body[10U + index] = requester.clock_identity[index];
    }
    body[18] = static_cast<std::uint8_t>((requester.port_number >> 8U) & 0xFFU);
    body[19] = static_cast<std::uint8_t>(requester.port_number & 0xFFU);
    return body;
}

[[nodiscard]] PtpFrame announce_frame(
    const PtpPortIdentity& source,
    const std::uint8_t priority1,
    const bool traceable) {
    PtpFrame frame;
    frame.header.message_type = PtpMessageType::announce;
    frame.header.domain_number = 0U;
    frame.header.transport_specific = 0U;
    frame.header.source_port_identity = source;
    if (traceable) {
        frame.header.flags = static_cast<std::uint16_t>(
            ptp_flag_current_utc_offset_valid |
            ptp_flag_ptp_timescale |
            ptp_flag_time_traceable |
            ptp_flag_frequency_traceable);
    }
    PtpAnnounceMessage announce;
    announce.priority1 = priority1;
    announce.priority2 = 128U;
    announce.clock_class = traceable ? 6U : 248U;
    announce.clock_accuracy = traceable
        ? PtpClockAccuracy::within_1_us
        : PtpClockAccuracy::unknown;
    announce.offset_scaled_log_variance = traceable ? 0x1000U : 0xFFFFU;
    announce.grandmaster_identity = source.clock_identity;
    announce.steps_removed = 0U;
    announce.time_source = traceable
        ? PtpTimeSource::gps
        : PtpTimeSource::internal_oscillator;
    frame.announce = announce;
    return frame;
}

void timestamp_math_is_bounded() {
    CHECK(ptp_timestamp_nanoseconds(ts(2U, 3U)) == 2'000'000'003LL);
    CHECK(ptp_timestamp_delta_nanoseconds(ts(3U, 100U), ts(2U, 900U)) == 999'999'200LL);
    CHECK(!ptp_timestamp_nanoseconds(ts(UINT64_MAX, 0U)).has_value());
    CHECK(!ptp_timestamp_nanoseconds(ts(1U, 1'000'000'000U)).has_value());
    CHECK(ptp_correction_nanoseconds(5LL * 65'536LL) == 5LL);
    CHECK(ptp_correction_nanoseconds(-5LL * 65'536LL) == -5LL);
}

void peer_delay_uses_all_four_hardware_timestamps() {
    const PtpPeerDelayExchange exchange{
        ts(10U, 0U),
        ts(10U, 500U),
        ts(10U, 700U),
        ts(10U, 1'200U),
        0LL,
        0LL,
    };
    const auto result = calculate_peer_delay(exchange);
    CHECK(result.has_value());
    CHECK(result->round_trip_path_ns == 1'000LL);
    CHECK(result->mean_path_delay_ns == 500LL);

    auto corrected = exchange;
    corrected.response_correction_field = 20LL * 65'536LL;
    corrected.response_follow_up_correction_field = 20LL * 65'536LL;
    const auto corrected_result = calculate_peer_delay(corrected);
    CHECK(corrected_result.has_value());
    CHECK(corrected_result->mean_path_delay_ns == 480LL);
}

void offset_from_master_applies_delay_and_correction() {
    const PtpSyncExchange exchange{
        ts(20U, 0U),
        ts(20U, 1'500U),
        500LL,
        0LL,
        0LL,
    };
    CHECK(calculate_offset_from_master(exchange) == 1'000LL);

    auto corrected = exchange;
    corrected.sync_correction_field = 100LL * 65'536LL;
    corrected.follow_up_correction_field = 50LL * 65'536LL;
    CHECK(calculate_offset_from_master(corrected) == 850LL);

    corrected.mean_path_delay_ns = -1LL;
    CHECK(!calculate_offset_from_master(corrected).has_value());
}

void traceability_requires_real_announce_evidence() {
    auto local = announce_frame(port(0x10U), 128U, false);
    CHECK(!ptp_announce_is_globally_traceable(local));

    auto global = announce_frame(port(0x11U), 128U, true);
    CHECK(ptp_announce_is_globally_traceable(global));
    global.header.flags = ptp_flag_ptp_timescale;
    CHECK(!ptp_announce_is_globally_traceable(global));
}

void receiver_correlates_pdelay_and_two_step_sync() {
    const auto local = port(0x20U, 2U);
    const auto master = port(0x30U, 1U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{
        0U,
        0U,
        local,
        std::chrono::milliseconds{3000},
        std::chrono::milliseconds{2000},
    });
    const auto t0 = Clock::time_point{};
    CHECK(receiver.observe_announce(announce_frame(master, 128U, true), t0));
    CHECK(receiver.status().selected_source == master);
    CHECK(receiver.status().selected_source_globally_traceable);

    receiver.note_pdelay_request(7U, ts(10U, 0U), t0 + std::chrono::milliseconds{1});
    PtpFrame response;
    response.header.message_type = PtpMessageType::pdelay_resp;
    response.header.source_port_identity = master;
    response.header.sequence_id = 7U;
    response.body = pdelay_body(ts(10U, 500U), local);
    CHECK(receiver.observe_pdelay_response(
        response,
        ts(10U, 1'200U),
        t0 + std::chrono::milliseconds{2}));

    PtpFrame response_follow_up;
    response_follow_up.header.message_type = PtpMessageType::pdelay_resp_follow_up;
    response_follow_up.header.source_port_identity = master;
    response_follow_up.header.sequence_id = 7U;
    response_follow_up.body = pdelay_body(ts(10U, 700U), local);
    const auto delay = receiver.observe_pdelay_response_follow_up(
        response_follow_up,
        t0 + std::chrono::milliseconds{3});
    CHECK(delay.has_value());
    CHECK(delay->mean_path_delay_ns == 500LL);

    PtpFrame sync;
    sync.header.message_type = PtpMessageType::sync;
    sync.header.source_port_identity = master;
    sync.header.sequence_id = 8U;
    sync.header.flags = 0x0200U;
    CHECK(!receiver.observe_sync(
        sync,
        ts(20U, 1'500U),
        t0 + std::chrono::milliseconds{10}).has_value());

    PtpFrame follow_up;
    follow_up.header.message_type = PtpMessageType::follow_up;
    follow_up.header.source_port_identity = master;
    follow_up.header.sequence_id = 8U;
    follow_up.timestamp = ts(20U, 0U);
    const auto measurement = receiver.observe_follow_up(
        follow_up,
        t0 + std::chrono::milliseconds{11});
    CHECK(measurement.has_value());
    CHECK(measurement->offset_from_master_ns == 1'000LL);
    CHECK(measurement->mean_path_delay_ns == 500LL);
    CHECK(measurement->globally_traceable);
    CHECK(receiver.status().completed_pdelay_exchanges == 1U);
    CHECK(receiver.status().completed_sync_exchanges == 1U);
}

void receiver_prefers_better_source_and_drops_stale_source() {
    const auto local = port(0x40U, 2U);
    const auto worse = port(0x41U);
    const auto better = port(0x42U);
    PtpTimeReceiver receiver(PtpTimeReceiverOptions{
        0U,
        0U,
        local,
        std::chrono::milliseconds{100},
        std::chrono::milliseconds{50},
    });
    const auto t0 = Clock::time_point{};
    CHECK(receiver.observe_announce(announce_frame(worse, 200U, false), t0));
    CHECK(receiver.status().selected_source == worse);
    CHECK(receiver.observe_announce(
        announce_frame(better, 100U, true),
        t0 + std::chrono::milliseconds{10}));
    CHECK(receiver.status().selected_source == better);
    CHECK(receiver.tick(t0 + std::chrono::milliseconds{111}));
    CHECK(!receiver.status().selected_source.has_value());
    CHECK(!receiver.status().mean_path_delay_ns.has_value());
}

void discipline_steps_then_locks_and_promotes_measured_sync() {
    PtpDisciplineOptions options;
    options.lock_required_samples = 3U;
    options.sync_timeout = std::chrono::milliseconds{500};
    options.holdover_timeout = std::chrono::milliseconds{1000};
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};

    const auto phase_step = discipline.observe(
        PtpOffsetMeasurement{port(0x50U), 1U, 2'000'000LL, 500LL, true},
        t0);
    CHECK(phase_step.kind == PtpClockCommandKind::step_phase);
    CHECK(phase_step.phase_step_ns == -2'000'000LL);
    CHECK(discipline.status().state == PtpDisciplineState::acquiring);
    CHECK(!discipline.measured_smp_synch().has_value());

    for (std::uint16_t index = 0U; index < 3U; ++index) {
        const auto command = discipline.observe(
            PtpOffsetMeasurement{
                port(0x50U),
                static_cast<std::uint16_t>(10U + index),
                500LL,
                500LL,
                true},
            t0 + std::chrono::milliseconds{100LL * static_cast<long long>(index + 1U)});
        CHECK(command.kind == PtpClockCommandKind::set_frequency);
    }
    CHECK(discipline.status().state == PtpDisciplineState::locked);
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::global_synchronized);

    discipline.tick(t0 + std::chrono::milliseconds{900});
    CHECK(discipline.status().state == PtpDisciplineState::holdover);
    CHECK(discipline.measured_smp_synch() == SmpSynchValue::local_synchronized);

    discipline.tick(t0 + std::chrono::milliseconds{1900});
    CHECK(discipline.status().state == PtpDisciplineState::unlocked);
    CHECK(!discipline.measured_smp_synch().has_value());
}

void discipline_rejects_bad_path_and_faults_on_actuation_failure() {
    PtpDisciplineOptions options;
    options.invalid_samples_before_fault = 2U;
    PtpClockDiscipline discipline(options);
    const auto t0 = Clock::time_point{};
    const PtpOffsetMeasurement invalid{port(0x60U), 1U, 0LL, 10'000'000LL, false};
    CHECK(discipline.observe(invalid, t0).kind == PtpClockCommandKind::none);
    CHECK(discipline.observe(invalid, t0 + std::chrono::milliseconds{1}).kind ==
          PtpClockCommandKind::none);
    CHECK(discipline.status().state == PtpDisciplineState::fault);
    CHECK(!discipline.measured_smp_synch().has_value());

    discipline.reset();
    discipline.record_actuation_failure();
    CHECK(discipline.status().state == PtpDisciplineState::fault);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"timestamp math", timestamp_math_is_bounded},
        {"peer delay math", peer_delay_uses_all_four_hardware_timestamps},
        {"offset math", offset_from_master_applies_delay_and_correction},
        {"traceability evidence", traceability_requires_real_announce_evidence},
        {"receiver correlation", receiver_correlates_pdelay_and_two_step_sync},
        {"source selection and timeout", receiver_prefers_better_source_and_drops_stale_source},
        {"discipline lock and holdover", discipline_steps_then_locks_and_promotes_measured_sync},
        {"discipline fault", discipline_rejects_bad_path_and_faults_on_actuation_failure},
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
              << " PTP-P2 discipline tests.\n";
    return 0;
}
