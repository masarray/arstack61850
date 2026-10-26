// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/live_signal_state.hpp"
#include "ariec61850/time_sync/sv_ptp_epoch.hpp"
#include "ariec61850/time_sync/sv_boot_clock_policy.hpp"
#include "ariec61850/time_sync/ptp_external_candidate_policy.hpp"
#include "ariec61850/time_sync/ptp_advanced_policy.hpp"
#include "ariec61850/time_sync/sv_ptp_clock_lease.hpp"
#include "ariec61850/time_sync/ptp_runtime.hpp"

#include "driver/gptimer.h"
#include "ethernet_port.h"
#include "live_control.hpp"
#include "runtime_profile.hpp"
#include "multi_profile_control.hpp"
#include "compiled_profile_adapter.hpp"
#include "ariec61850/sampled_values/m4_three_sv_grid.hpp"
#include "sdkconfig.h"
#if CONFIG_AR_PTP_LAB_TX
#include "ptp_lab_task.hpp"
#include "esp_idf_version.h"
#include "soc/soc_caps.h"
#if ESP_IDF_VERSION_MAJOR < 6
#include "esp_eth_mac_esp.h"
#endif
#endif
#include "smp_synch_lab.hpp"
#if defined(CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL) && CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL && \
    CONFIG_AR_PTP_LAB_TX && defined(SOC_EMAC_IEEE1588V2_SUPPORTED) && \
    SOC_EMAC_IEEE1588V2_SUPPORTED && ESP_IDF_VERSION_MAJOR < 6
#define AR_SV_PTP_PHASE_SUPPORTED 1
#include "ariec61850/time_sync/sv_ptp_phase_authority.hpp"
// The fail-closed SOURCE entry filter remains available even when descriptor
// evidence is disabled by firmware config (AUTO then stays NOT SYNC).
#include "ariec61850/time_sync/sv_tx_egress_evidence.hpp"
#include "freertos/semphr.h"
#else
#define AR_SV_PTP_PHASE_SUPPORTED 0
#endif
#if AR_SV_PTP_PHASE_SUPPORTED && defined(CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL) && \
    CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL
#define AR_SV_TX_EGRESS_SUPPORTED 1
#include "ariec61850/time_sync/sv_tx_egress_evidence.hpp"
#else
#define AR_SV_TX_EGRESS_SUPPORTED 0
#endif
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

using ar::esp32p4::smv::RuntimePublisherProfile;
using ar::esp32p4::smv::live_control_bind_publisher_task;
using ar::esp32p4::smv::live_control_force_stop;
using ar::esp32p4::smv::live_control_set_ethernet_link;
using ar::esp32p4::smv::live_control_initialize;
using ar::esp32p4::smv::live_control_task;
using ar::esp32p4::smv::live_signal_snapshot;
using ar::esp32p4::smv::live_tx_running;
using ar::esp32p4::smv::runtime_profile_initialize;
using ar::esp32p4::smv::runtime_profile_snapshot;
using ar::esp32p4::smv::runtime_profile_validate;
using ar::esp32p4::smv::smp_synch_lab_decision;
using ar::esp32p4::smv::smp_synch_lab_mode;
using ar::esp32p4::smv::SvSyncPolicyMode;
using ar::esp32p4::smv::smp_synch_set_source_local_qualified;
using ar::esp32p4::smv::smp_synch_source_local_qualified;
using ar::esp32p4::smv::smp_synch_set_receiver_timeline_qualified;
using ar::esp32p4::smv::smp_synch_diag_observe;
using ar::esp32p4::smv::SvSyncLossReason;
using ar::esp32p4::smv::smp_synch_receiver_timeline_qualified;
using ar::esp32p4::smv::take_start_request;
using ar::iec61850::ethernet::MacAddress;
using ar::iec61850::ethernet::VlanTag;
using ar::iec61850::sampled_values::SampledValueAsdu;
using ar::iec61850::sampled_values::SampledValueAsduEncodeLayout;
using ar::iec61850::sampled_values::SampledValuesFrame;
using ar::iec61850::sampled_values::SampledValuesFrameCodec;
using ar::iec61850::sampled_values::SampledValuesPdu;
using ar::iec61850::sampled_values::SvFixedPointSineEngine;
using ar::iec61850::sampled_values::SvLiveSignalState;
using ar::iec61850::time_sync::SvSecondBoundaryArm;
using ar::iec61850::time_sync::SvPtpClockObservation;
using ar::iec61850::time_sync::plan_sv_ptp_second_arm;
using ar::iec61850::time_sync::rebase_sv_ptp_second_arm;
using ar::iec61850::time_sync::SvPtpClockLease;
using ar::iec61850::time_sync::PtpAuthorityRole;
using ar::iec61850::time_sync::same_sv_ptp_clock_lease;
using ar::iec61850::time_sync::valid_sv_ptp_clock_lease;
#if AR_SV_PTP_PHASE_SUPPORTED
using ar::iec61850::time_sync::SvBoundedSlewCursor;
using ar::iec61850::time_sync::SvPtpPhaseAuthority;
using ar::iec61850::time_sync::SvAutoPreflightPolicy;
using ar::iec61850::time_sync::SvLocalRefAcquisition;
using ar::iec61850::time_sync::SvSourceSelfClockAcquisition;
using ar::iec61850::time_sync::SvPtpPhaseObservation;
using ar::iec61850::time_sync::SvPtpPublisherContinuityGate;
#endif
#if AR_SV_TX_EGRESS_SUPPORTED
using ar::iec61850::time_sync::SvTxEgressEvidence;
using ar::iec61850::time_sync::SvLocalSyncEvidenceGate;
using ar::iec61850::time_sync::SvTxEgressObservation;
using ar::iec61850::time_sync::SvTxEgressSummary;
#endif

constexpr char kTag[] = "ar_smv_p3";
constexpr EventBits_t kLinkUpBit = BIT0;
constexpr std::uint16_t kMirrorAppId = 0x4F01U;
constexpr char kMirrorSvId[] = "AR_DIAG_SV1";
constexpr std::uint16_t kSampleModeSamplesPerSecond = 1U;
constexpr std::uint16_t kDiagnosticEtherType = 0x88B5U;
constexpr std::uint32_t kStatsMergeEvery = 400U;
constexpr std::uint32_t kTransportFailureStopThreshold = 400U; // 100 ms at 4 kHz
// Only one publisher task writes this recorder. Status/serial may read it on CPU0.
// Capture real first/last ESP-IDF TX errors before a fail-closed STOP.
ar::iec61850::sampled_values::SvTxFaultBurst g_sv_tx_faults{};
constexpr std::uint32_t kTimerResolutionHz = 1000000U;
#if AR_SV_PTP_PHASE_SUPPORTED
// Separate clock authority from SV TX session state. During SV STOP the
// exact 4-kHz grid/phase servo and PTP SOURCE continue uninterrupted;
// remember only a PREVIOUSLY MEASURED SOURCE certification. Never use this
// latch to qualify a cold start or a changed GM/clock/rate.
std::atomic<bool> g_parked_source_clock_certified{false};
#endif

// A single fail-closed operation for both AUTO roles. In particular, keep
// receiver invalidation INSIDE the same conditional as SOURCE invalidation:
// clearing it unconditionally after a valid sparse MAC TX stamp would make
// RECEIVER qualification impossible at every evidence epoch.
inline void revoke_sv_auto_claims(
    const SvSyncLossReason reason = SvSyncLossReason::other) noexcept {
#if AR_SV_PTP_PHASE_SUPPORTED
    // Every genuine timing/lease/transport fault revokes any parked
    // certificate as well; only a verified SV-only STOP may preserve it.
    g_parked_source_clock_certified.store(false, std::memory_order_release);
#endif
    // Only timestamp an actual lock transition; ordinary 4kHz frame
    // processing keeps its existing work budget.
    const bool was_qualified = smp_synch_source_local_qualified() ||
        smp_synch_receiver_timeline_qualified();
    const auto ms = was_qualified
        ? static_cast<std::uint32_t>(esp_timer_get_time() / 1000LL) : 0U;
    smp_synch_set_source_local_qualified(false, reason, ms);
    smp_synch_set_receiver_timeline_qualified(false, reason, ms);
}

EventGroupHandle_t g_link_events = nullptr;
portMUX_TYPE g_stats_mux = portMUX_INITIALIZER_UNLOCKED;

struct TimingStats final {
    std::uint64_t samples{};
    std::uint64_t canonical_ok{};
    std::uint64_t canonical_fail{};
    std::uint64_t mirror_ok{};
    std::uint64_t mirror_fail{};
    std::uint64_t missed_slots{};
    std::uint32_t multi_batch_max_us{};
    std::uint64_t multi_batch_overruns{};
    std::uint64_t multi_batches{};
    std::int64_t lateness_sum_us{};
    std::int32_t lateness_min_us{std::numeric_limits<std::int32_t>::max()};
    std::int32_t lateness_max_us{std::numeric_limits<std::int32_t>::min()};
    std::uint16_t last_sample_count{};
    std::uint64_t last_signal_generation{};
};

TimingStats g_interval_stats{};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
#if AR_SV_PTP_PHASE_SUPPORTED
static_assert(std::atomic<std::int32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
#endif
std::atomic<std::uint32_t> g_sample_tick_total{0U};
#if AR_SV_TX_EGRESS_SUPPORTED
constexpr std::uint32_t kEgressEvidenceEveryZeroEpochs =
    SvLocalSyncEvidenceGate::kEvidenceStride;
portMUX_TYPE g_tx_egress_mux = portMUX_INITIALIZER_UNLOCKED;
SvTxEgressEvidence g_tx_egress_evidence{};
SvLocalSyncEvidenceGate g_local_sync_evidence{}; // same critical section as egress tracker
// SOURCE-only certified resume: the dry GPTimer stays aligned across STOP.
// The first new MAC baseline is NOT a clock step; any real error revokes.
// Zero means cold/expired. Never read these atomics from the 4-kHz ISR.
std::atomic<bool> g_warm_resume_baseline_consumed{false};
#endif
#if AR_SV_PTP_PHASE_SUPPORTED
std::atomic<std::uint32_t> g_warm_resume_first_epoch{0U};
SemaphoreHandle_t g_phase_timer_mutex = nullptr;
gptimer_handle_t g_monitored_sample_clock = nullptr; // under mutex, never in ISR
TaskHandle_t g_phase_observer_task = nullptr;
std::atomic<std::uint32_t> g_phase_session_serial{0U};
// SOURCE AUTO preflight disciplines the sample clock while SV TX is OFF.
// Only the realtime publisher task creates/stops this dry-run GPTimer.
std::atomic<bool> g_preflight_phase_clock_active{false};
#endif

struct PacketTemplate final {
    std::vector<std::uint8_t> bytes;
    std::size_t sample_count_offset{std::numeric_limits<std::size_t>::max()};
    std::size_t sample_synchronization_offset{std::numeric_limits<std::size_t>::max()};
    std::size_t sample_payload_offset{std::numeric_limits<std::size_t>::max()};
};

using WaveformRow = SvFixedPointSineEngine::SampleRow;

struct RationalScheduleCursor final {
    std::uint32_t rate_hz{};
    std::uint32_t base_ticks{};
    std::uint32_t remainder{};
    std::uint32_t accumulator{};

    void reset(const std::uint32_t rate) noexcept {
        rate_hz = rate;
        base_ticks = kTimerResolutionHz / rate_hz;
        remainder = kTimerResolutionHz % rate_hz;
        accumulator = 0U;
    }

    [[nodiscard]] std::uint32_t next_ticks() noexcept {
        std::uint32_t ticks = base_ticks;
        accumulator += remainder;
        if (accumulator >= rate_hz) {
            accumulator -= rate_hz;
            ++ticks;
        }
        return ticks;
    }
};

struct SampleClockContext final {
    TaskHandle_t publisher_task{};
    RationalScheduleCursor schedule{};
    gptimer_alarm_config_t next_alarm{};
#if AR_SV_PTP_PHASE_SUPPORTED
    // ISR-owned epoch count/deadline are published with release/acquire.
    // Only the low 32 bits are used (71-minute wrap at 1 MHz).
    std::atomic<std::uint32_t> epoch_generation{0U};
    std::atomic<std::uint32_t> session_generation{0U};
    std::atomic<std::uint32_t> last_epoch_alarm{0U};
    std::atomic<std::int32_t> requested_slew_ticks{0};
    std::atomic<std::int32_t> cumulative_slew_ticks{0};
    std::atomic<std::uint32_t> publisher_fault_generation{0U};
    std::atomic<bool> phase_authority_valid{false};
    // Last successfully observed sample epoch (published only by the 1Hz
    // control-plane observer). The 4kHz producer checks freshness without
    // waiting for that observer or consulting blocking MAC/PTP APIs.
    std::atomic<std::uint32_t> last_phase_observed_epoch{0U};
    // Immutable for one monitored timer session; read under phase mutex.
    SvPtpClockLease armed_ptp_clock{};
    SvBoundedSlewCursor phase_slew{};
    std::uint32_t phase_index{};
    bool previous_phase_healthy{};
    bool phase_enabled{}; // set before timer START; only ISR reads it
    // START transfers a warmed clock at the NEXT canonical second boundary.
    // Never wake the high-priority publisher for the 4kHz dry-run ticks.
    std::atomic<bool> publisher_notifications_enabled{true};
    std::atomic<bool> launch_on_next_epoch{false};
#endif
};

SampleClockContext g_sample_clock_context{};

void eth_event_handler(void*, esp_event_base_t, const std::int32_t event_id, void*) {
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        xEventGroupSetBits(g_link_events, kLinkUpBit);
        live_control_set_ethernet_link(true);
        ESP_LOGI(kTag, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED: {
        const bool was_up = (xEventGroupGetBits(g_link_events) & kLinkUpBit) != 0U;
        xEventGroupClearBits(g_link_events, kLinkUpBit);
        // Even an unexpected DOWN event must leave the START interlock closed.
        live_control_set_ethernet_link(false);
        if (was_up) {
#if CONFIG_AR_PTP_LAB_TX
            // Nonblocking request; the role owner performs PTP teardown.
            ar_ptp_lab_request_link_down_stop();
#endif
            ESP_LOGW(kTag, "Ethernet PHY lost: SV stopped; PTP teardown requested");
        } else {
            ESP_LOGW(kTag, "Ethernet down (no prior link-UP); publisher disarmed");
        }
        break;
    }
    case ETHERNET_EVENT_START:
        ESP_LOGI(kTag, "Ethernet driver started");
        break;
    case ETHERNET_EVENT_STOP:
        // A stopped driver is just as fatal as unplugging the PHY cable,
        // including if a DISCONNECTED event was never delivered.
        xEventGroupClearBits(g_link_events, kLinkUpBit);
        live_control_set_ethernet_link(false);
#if CONFIG_AR_PTP_LAB_TX
        ar_ptp_lab_request_link_down_stop();
#endif
        ESP_LOGW(kTag, "Ethernet driver stopped: SV/PTP transmit authority revoked");
        break;
    default:
        break;
    }
}

void write_u16_be(std::uint8_t* destination, const std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32_be(std::uint8_t* destination, const std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_i32_be(std::uint8_t* destination, const std::int32_t value) noexcept {
    write_u32_be(destination, static_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> make_4i4v_payload() {
    return std::vector<std::uint8_t>(8U * 8U, 0U);
}

SampledValuesFrame make_runtime_frame(
    const std::array<std::uint8_t, 6>& source_mac,
    const RuntimePublisherProfile& profile,
    const bool diagnostic_mirror) {
    SampledValueAsdu asdu;
    asdu.sv_id = diagnostic_mirror ? kMirrorSvId : profile.sv_id.data();
    asdu.data_set_reference =
        (!diagnostic_mirror && profile.include_data_set) ? profile.data_set_reference.data() : "";

    // Keep BER widths fixed so the realtime path only patches bytes in place.
    asdu.sample_count = 0x0100U;
    asdu.configuration_revision = 0x01000000U;
    asdu.reference_time.reset();
    // No disciplined PTP clock exists yet, so never claim synchronization.
    asdu.sample_synchronization = 0U;

    if (diagnostic_mirror || profile.include_sample_rate) {
        // Wire units follow the selected SCL profile, never FPS by assumption.
        asdu.sample_rate = diagnostic_mirror || profile.wire_sample_rate == 0U
            ? static_cast<std::uint16_t>(profile.publisher_rate_hz)
            : profile.wire_sample_rate;
        asdu.sample_mode = diagnostic_mirror
            ? kSampleModeSamplesPerSecond : profile.wire_sample_mode;
    } else {
        asdu.sample_rate.reset();
        asdu.sample_mode.reset();
    }
    asdu.sample_payload = make_4i4v_payload();

#if CONFIG_AR_PTP_LAB_TX
    // IEC 61850-9-2 Ed2.1 optional gmIdentity / SynchSrcID. The eight
    // bytes MUST equal the GrandmasterIdentity in Announce. On the
    // simulated GM bench this is the board's own clock identity; in
    // RECEIVER mode it is the measured selected external source.
    // Keep legacy 9-2LE unchanged unless a bench GM is selected.
    if (!diagnostic_mirror) {
        ar_ptp_lab_config_t ptp_config{};
        ar_ptp_timing_authority_t authority{};
        if (ar_ptp_lab_get_config(&ptp_config) &&
            ar_ptp_lab_get_timing_authority(&authority) &&
            authority.clock_usable) {
            if (ptp_config.role == AR_PTP_ROLE_LAB_SOURCE &&
                ptp_config.bench_global_gm) {
                if (ptp_config.clock_identity_override) {
                    std::array<std::uint8_t, 8> identity{};
                    std::copy_n(ptp_config.clock_identity, 8U, identity.begin());
                    asdu.gm_identity = identity;
                } else {
                    asdu.gm_identity =
                        ar::iec61850::time_sync::ptp_clock_identity_from_mac(source_mac);
                }
            } else if (ptp_config.role == AR_PTP_ROLE_TIME_RECEIVER &&
                       authority.source_selected) {
                std::array<std::uint8_t, 8> identity{};
                std::copy_n(authority.selected_source_clock_identity, 8U, identity.begin());
                asdu.gm_identity = identity;
            }
        }
    }
#endif

    SampledValuesFrame frame;
    frame.destination = MacAddress{std::span<const std::uint8_t>{profile.destination_mac}};
    frame.source = MacAddress{std::span<const std::uint8_t>{source_mac}};
    if (!diagnostic_mirror && profile.vlan_present) {
        frame.vlan = VlanTag{profile.vlan_priority, profile.vlan_id};
    } else {
        frame.vlan.reset();
    }
    frame.app_id = diagnostic_mirror ? kMirrorAppId : profile.app_id;
    frame.reserved1 = 0U;
    frame.reserved2 = 0U;
    frame.pdu = SampledValuesPdu{{std::move(asdu)}};
    return frame;
}

bool build_packet_template(
    const std::array<std::uint8_t, 6>& source_mac,
    const RuntimePublisherProfile& profile,
    const bool broadcast_destination,
    const bool diagnostic_mirror,
    PacketTemplate& packet) {
    packet = {};
    const auto frame = make_runtime_frame(source_mac, profile, diagnostic_mirror);
    const auto required = SampledValuesFrameCodec::encoded_size(frame);
    if (!required.has_value()) {
        ESP_LOGE(kTag, "SV template size validation failed");
        return false;
    }

    packet.bytes.resize(*required);
    std::array<SampledValueAsduEncodeLayout, 1> layouts{};
    const auto encoded = SampledValuesFrameCodec::encode_into_with_layout(
        frame, packet.bytes, layouts);
    if (!encoded.success() || encoded.bytes_written != packet.bytes.size()) {
        ESP_LOGE(kTag, "SV template encode/layout generation failed");
        return false;
    }

    const auto& layout = layouts.front();
    const auto exact_region = [&packet](const auto& region, const std::size_t expected_size) {
        return region.present && region.value_size == expected_size &&
            region.value_offset <= packet.bytes.size() &&
            region.value_size <= packet.bytes.size() - region.value_offset;
    };
    if (!exact_region(layout.sample_count, 2U) ||
        !exact_region(layout.configuration_revision, 4U) ||
        !exact_region(layout.sample_synchronization, 1U) ||
        !exact_region(layout.sample_payload, 64U)) {
        ESP_LOGE(kTag, "SV template patch layout does not match the bounded runtime contract");
        return false;
    }

    packet.sample_count_offset = layout.sample_count.value_offset;
    packet.sample_synchronization_offset = layout.sample_synchronization.value_offset;
    packet.sample_payload_offset = layout.sample_payload.value_offset;

    if (broadcast_destination) {
        std::fill_n(packet.bytes.begin(), 6U, std::uint8_t{0xFFU});
    }

    write_u16_be(packet.bytes.data() + packet.sample_count_offset, 0U);
    write_u32_be(
        packet.bytes.data() + layout.configuration_revision.value_offset,
        profile.configuration_revision);
    return true;
}

void patch_packet(
    PacketTemplate& packet,
    const std::uint16_t sample_count,
    const std::uint8_t sample_synchronization,
    const WaveformRow& values,
    const SvLiveSignalState& signal_state) noexcept {
    write_u16_be(packet.bytes.data() + packet.sample_count_offset, sample_count);
    packet.bytes[packet.sample_synchronization_offset] = sample_synchronization;
    for (std::size_t channel = 0U; channel < values.size(); ++channel) {
        const auto base = packet.sample_payload_offset + channel * 8U;
        write_i32_be(packet.bytes.data() + base, values[channel]);
        write_u32_be(packet.bytes.data() + base + 4U, signal_state.channels[channel].quality);
    }
}

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
std::array<std::uint8_t, 60> make_probe(
    const std::array<std::uint8_t, 6>& source_mac,
    const std::array<std::uint8_t, 6>& destination_mac,
    const bool multicast) {
    std::array<std::uint8_t, 60> frame{};
    for (std::size_t index = 0U; index < 6U; ++index) {
        frame[index] = multicast ? destination_mac[index] : 0xFFU;
        frame[6U + index] = source_mac[index];
    }
    frame[12] = static_cast<std::uint8_t>((kDiagnosticEtherType >> 8U) & 0xFFU);
    frame[13] = static_cast<std::uint8_t>(kDiagnosticEtherType & 0xFFU);
    constexpr char payload[] = "ARSTACK-P3-PROFILE-PROBE";
    for (std::size_t index = 0U;
         index < sizeof(payload) - 1U && 14U + index < frame.size(); ++index) {
        frame[14U + index] = static_cast<std::uint8_t>(payload[index]);
    }
    return frame;
}
#endif

void merge_stats(TimingStats& local) noexcept {
    if (local.samples == 0U) return;
    portENTER_CRITICAL(&g_stats_mux);
    g_interval_stats.samples += local.samples;
    g_interval_stats.canonical_ok += local.canonical_ok;
    g_interval_stats.canonical_fail += local.canonical_fail;
    g_interval_stats.mirror_ok += local.mirror_ok;
    g_interval_stats.mirror_fail += local.mirror_fail;
    g_interval_stats.missed_slots += local.missed_slots;
    g_interval_stats.multi_batch_max_us = std::max(
        g_interval_stats.multi_batch_max_us, local.multi_batch_max_us);
    g_interval_stats.multi_batch_overruns += local.multi_batch_overruns;
    g_interval_stats.multi_batches += local.multi_batches;
    g_interval_stats.lateness_sum_us += local.lateness_sum_us;
    g_interval_stats.lateness_min_us = std::min(g_interval_stats.lateness_min_us, local.lateness_min_us);
    g_interval_stats.lateness_max_us = std::max(g_interval_stats.lateness_max_us, local.lateness_max_us);
    g_interval_stats.last_sample_count = local.last_sample_count;
    g_interval_stats.last_signal_generation = local.last_signal_generation;
    portEXIT_CRITICAL(&g_stats_mux);
    local = TimingStats{};
}

void accumulate_stats(TimingStats& target, const TimingStats& source) noexcept {
    if (source.samples == 0U) return;
    target.samples += source.samples;
    target.canonical_ok += source.canonical_ok;
    target.canonical_fail += source.canonical_fail;
    target.mirror_ok += source.mirror_ok;
    target.mirror_fail += source.mirror_fail;
    target.missed_slots += source.missed_slots;
    target.multi_batch_max_us = std::max(
        target.multi_batch_max_us, source.multi_batch_max_us);
    target.multi_batch_overruns += source.multi_batch_overruns;
    target.multi_batches += source.multi_batches;
    target.lateness_sum_us += source.lateness_sum_us;
    target.lateness_min_us = std::min(target.lateness_min_us, source.lateness_min_us);
    target.lateness_max_us = std::max(target.lateness_max_us, source.lateness_max_us);
    target.last_sample_count = source.last_sample_count;
    target.last_signal_generation = source.last_signal_generation;
}

void reset_interval_stats() noexcept {
    portENTER_CRITICAL(&g_stats_mux);
    g_interval_stats = TimingStats{};
    portEXIT_CRITICAL(&g_stats_mux);
}

void update_local_timing(
    TimingStats& local,
    const std::int64_t lateness_us,
    const std::uint64_t missed_slots,
    const std::uint16_t sample_count,
    const std::uint64_t signal_generation) noexcept {
    const auto bounded = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        lateness_us,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
    ++local.samples;
    local.missed_slots += missed_slots;
    local.lateness_sum_us += lateness_us;
    local.lateness_min_us = std::min(local.lateness_min_us, bounded);
    local.lateness_max_us = std::max(local.lateness_max_us, bounded);
    local.last_sample_count = sample_count;
    local.last_signal_generation = signal_generation;
}

bool sample_alarm_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t* event,
    void* user_context) {
    auto* context = static_cast<SampleClockContext*>(user_context);
    std::int32_t adjustment = 0;
#if AR_SV_PTP_PHASE_SUPPORTED
    const std::uint32_t index = context->phase_index;
    bool epoch_boundary = false;
    if (context->phase_enabled) {
        if (index == 0U) {
            epoch_boundary = true;
            if (context->launch_on_next_epoch.exchange(
                    false, std::memory_order_acq_rel)) {
                context->publisher_notifications_enabled.store(
                    true, std::memory_order_release);
            }
            context->last_epoch_alarm.store(
                static_cast<std::uint32_t>(event->alarm_value),
                std::memory_order_relaxed);
            context->epoch_generation.fetch_add(1U, std::memory_order_release);
            context->phase_slew.arm(context->schedule.rate_hz, 0);
        }
        context->phase_index = index + 1U >= context->schedule.rate_hz
            ? 0U : index + 1U;
        // Commands arrive after a cross-clock read from the background task.
        // Apply within THIS epoch, across all remaining intervals. Never
        // change smpCnt or issue multiple timer alarms to catch up.
        const bool phase_valid = context->phase_authority_valid.load(
            std::memory_order_relaxed);
        if (!phase_valid) {
            // Cancel once on a healthy->fault transition; do not reset
            // distributor memory at the full 4k/4.8k ISR cadence.
            if (context->previous_phase_healthy) {
                context->phase_slew.arm(context->schedule.rate_hz, 0);
                context->previous_phase_healthy = false;
            }
        } else {
            context->previous_phase_healthy = true;
            // Most frames have no new command. Avoid a locked RMW atomic
            // exchange on all 4k/4.8k interrupts; take it only when nonzero.
            if (context->requested_slew_ticks.load(
                    std::memory_order_relaxed) != 0) {
                const std::int32_t requested =
                    context->requested_slew_ticks.exchange(
                        0, std::memory_order_relaxed);
                if (requested != 0 &&
                    index + 16U < context->schedule.rate_hz) {
                    context->phase_slew.arm(
                        context->schedule.rate_hz - index, requested);
                }
            }
        }
        adjustment = context->phase_slew.next_adjustment();
        if (adjustment != 0) {
            context->cumulative_slew_ticks.fetch_add(
                adjustment, std::memory_order_relaxed);
        }
    }
#endif
    std::uint32_t interval = context->schedule.next_ticks();
#if AR_SV_PTP_PHASE_SUPPORTED
    // Exactly the original ISR path in normal production builds. The
    // experimental opt-in only adjusts a positive deadline by one tick.
    if (adjustment > 0) ++interval;
    else if (adjustment < 0 && interval > 1U) --interval;
#endif
    context->next_alarm = {};
    context->next_alarm.alarm_count = event->alarm_value + interval;
    context->next_alarm.flags.auto_reload_on_alarm = false;
    static_cast<void>(gptimer_set_alarm_action(timer, &context->next_alarm));

    BaseType_t high_priority_task_woken = pdFALSE;
#if AR_SV_PTP_PHASE_SUPPORTED
    if (context->publisher_notifications_enabled.load(
            std::memory_order_acquire)) {
        vTaskNotifyGiveFromISR(context->publisher_task, &high_priority_task_woken);
    }
#else
    vTaskNotifyGiveFromISR(context->publisher_task, &high_priority_task_woken);
#endif
#if AR_SV_PTP_PHASE_SUPPORTED
    if (epoch_boundary && g_phase_observer_task != nullptr) {
        vTaskNotifyGiveFromISR(g_phase_observer_task, &high_priority_task_woken);
    }
#endif
    return high_priority_task_woken == pdTRUE;
}

// One status-derived authority snapshot per read: never assemble role,
// generation and GM identity from separate getters.
[[nodiscard]] SvPtpClockLease read_ptp_epoch_clock_lease() noexcept {
    SvPtpClockLease lease{};
#if CONFIG_AR_PTP_LAB_TX
    ar_ptp_timing_authority_t status{};
    if (ar_ptp_lab_get_timing_authority(&status)) {
        lease.role = static_cast<PtpAuthorityRole>(status.role);
        lease.generation = status.generation;
        lease.clock_usable = status.clock_usable;
        lease.source_selected = status.source_selected;
        std::copy_n(status.selected_source_clock_identity, 8U,
                    lease.source_clock_identity.begin());
        lease.source_port = status.selected_source_port_number;
    }
#endif
    return lease;
}

struct SvPtpEpochArm final {
    SvSecondBoundaryArm timer{};
    SvPtpClockLease lease{};
    std::uint64_t clock_read_after_us{};
};

// Align the initial sample with EMAC PTP and retain EXACT authority identity.
// The receiver path is experimental-only; normal builds stay free-running.
[[nodiscard]] std::optional<SvPtpEpochArm>
plan_ptp_epoch_arm(const esp_eth_handle_t eth_handle) noexcept {
#if CONFIG_AR_PTP_LAB_TX && defined(SOC_EMAC_IEEE1588V2_SUPPORTED) && \
    SOC_EMAC_IEEE1588V2_SUPPORTED && ESP_IDF_VERSION_MAJOR < 6
    const auto lease = read_ptp_epoch_clock_lease();
    if (!valid_sv_ptp_clock_lease(lease)) return std::nullopt;
    if (lease.role == PtpAuthorityRole::receiver &&
        !AR_SV_PTP_PHASE_SUPPORTED) return std::nullopt;
    eth_mac_time_t ptp_time{};
    const std::int64_t before_us = esp_timer_get_time();
    const auto result = esp_eth_ioctl(
        eth_handle, static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_G_PTP_TIME),
        &ptp_time);
    const std::int64_t after_us = esp_timer_get_time();
    if (result != ESP_OK || before_us < 0 || after_us < before_us ||
        (ptp_time.seconds == 0U && ptp_time.nanoseconds == 0U) ||
        !same_sv_ptp_clock_lease(lease, read_ptp_epoch_clock_lease())) {
        return std::nullopt;
    }
    const auto arm = plan_sv_ptp_second_arm(SvPtpClockObservation{
        ptp_time.nanoseconds, static_cast<std::uint64_t>(before_us),
        static_cast<std::uint64_t>(after_us)});
    if (!arm.has_value()) return std::nullopt;
    return SvPtpEpochArm{*arm, lease, static_cast<std::uint64_t>(after_us)};
#else
    (void)eth_handle;
    return std::nullopt;
#endif
}

gptimer_handle_t start_sample_clock(
    TaskHandle_t publisher_task_handle,
    const std::uint32_t publisher_rate_hz,
    const esp_eth_handle_t eth_handle,
    bool& epoch_armed,
    const bool profile_has_second_aligned_counter,
    const bool preflight_only = false) {
    epoch_armed = false;
#if AR_SV_PTP_PHASE_SUPPORTED
    g_sample_clock_context.publisher_task = publisher_task_handle;
    g_sample_clock_context.next_alarm = {};
    g_sample_clock_context.epoch_generation.store(0U, std::memory_order_relaxed);
    g_sample_clock_context.session_generation.store(
        g_phase_session_serial.fetch_add(1U, std::memory_order_relaxed) + 1U,
        std::memory_order_release);
    g_sample_clock_context.last_epoch_alarm.store(0U, std::memory_order_relaxed);
    g_sample_clock_context.requested_slew_ticks.store(0, std::memory_order_relaxed);
    g_sample_clock_context.cumulative_slew_ticks.store(0, std::memory_order_relaxed);
    g_sample_clock_context.publisher_fault_generation.store(0U, std::memory_order_relaxed);
    g_sample_clock_context.phase_authority_valid.store(false, std::memory_order_relaxed);
    g_sample_clock_context.last_phase_observed_epoch.store(0U, std::memory_order_relaxed);
    g_sample_clock_context.armed_ptp_clock = {};
    g_sample_clock_context.phase_index = 0U;
    g_sample_clock_context.previous_phase_healthy = false;
    g_sample_clock_context.phase_enabled = false;
    g_sample_clock_context.phase_slew.arm(publisher_rate_hz, 0);
#else
    g_sample_clock_context = {};
    (void)profile_has_second_aligned_counter;
#endif
    g_sample_clock_context.publisher_task = publisher_task_handle;
    g_sample_clock_context.schedule.reset(publisher_rate_hz);

    gptimer_config_t timer_config{};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = kTimerResolutionHz;

    gptimer_handle_t timer = nullptr;
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer));

    gptimer_event_callbacks_t callbacks{};
    callbacks.on_alarm = &sample_alarm_callback;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(
        timer, &callbacks, &g_sample_clock_context));

    // GPTimer enable may require driver/PM setup. Perform it BEFORE sampling
    // the PTP clock; otherwise that unpredictable setup delay moves the
    // first smpCnt=0 deadline by milliseconds and the deliberately bounded
    // 400us/second phase servo takes tens of seconds to converge.
    // The timer still does not count until gptimer_start() below.
    ESP_ERROR_CHECK(gptimer_enable(timer));
    const auto epoch_arm = plan_ptp_epoch_arm(eth_handle);
    // Time spent configuring GPTimer after the EMAC PTP read must not
    // shift the first smpCnt=0 deadline. Recheck the same lease before arm.
    const std::int64_t pre_arm_us = esp_timer_get_time();
    const auto corrected_arm = epoch_arm.has_value() && pre_arm_us >= 0
        ? rebase_sv_ptp_second_arm(
              epoch_arm->timer, epoch_arm->clock_read_after_us,
              static_cast<std::uint64_t>(pre_arm_us))
        : std::nullopt;
    epoch_armed = corrected_arm.has_value() &&
        same_sv_ptp_clock_lease(
            epoch_arm->lease, read_ptp_epoch_clock_lease());
#if AR_SV_PTP_PHASE_SUPPORTED
    if (epoch_armed) {
        g_sample_clock_context.armed_ptp_clock = epoch_arm->lease;
    }
    g_sample_clock_context.phase_enabled =
        epoch_armed && profile_has_second_aligned_counter &&
        g_phase_observer_task != nullptr && g_phase_timer_mutex != nullptr;
    g_sample_clock_context.publisher_notifications_enabled.store(
        !preflight_only, std::memory_order_relaxed);
    g_sample_clock_context.launch_on_next_epoch.store(
        false, std::memory_order_relaxed);
#endif
    g_sample_clock_context.next_alarm = {};
    g_sample_clock_context.next_alarm.alarm_count = epoch_armed
        ? corrected_arm->first_alarm_us
        : g_sample_clock_context.schedule.next_ticks();
    g_sample_clock_context.next_alarm.flags.auto_reload_on_alarm = false;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &g_sample_clock_context.next_alarm));
    ESP_ERROR_CHECK(gptimer_start(timer));
#if AR_SV_PTP_PHASE_SUPPORTED
    if (g_sample_clock_context.phase_enabled) {
        xSemaphoreTake(g_phase_timer_mutex, portMAX_DELAY);
        g_monitored_sample_clock = timer;
        xSemaphoreGive(g_phase_timer_mutex);
    }
#endif
    return timer;
}

void stop_sample_clock(gptimer_handle_t& timer) noexcept {
#if AR_SV_PTP_PHASE_SUPPORTED
    g_preflight_phase_clock_active.store(false, std::memory_order_release);
#endif
    // Release the wire claim before teardown; old MAC evidence never
    // survives STOP/START, even when the same SOURCE later restarts.
    revoke_sv_auto_claims(SvSyncLossReason::publisher_stop);
#if AR_SV_TX_EGRESS_SUPPORTED
    g_warm_resume_first_epoch.store(0U, std::memory_order_release);
#endif
    if (timer == nullptr) return;
#if AR_SV_PTP_PHASE_SUPPORTED
    // Serialize teardown with the non-ISR phase observer, so a GPTimer
    // handle can never be deleted during its bracketed EMAC PTP read.
    xSemaphoreTake(g_phase_timer_mutex, portMAX_DELAY);
    if (g_monitored_sample_clock == timer) g_monitored_sample_clock = nullptr;
    g_sample_clock_context.session_generation.store(
        0U, std::memory_order_release);
    g_sample_clock_context.requested_slew_ticks.store(
        0, std::memory_order_relaxed);
    g_sample_clock_context.phase_authority_valid.store(
        false, std::memory_order_release);
    g_sample_clock_context.last_phase_observed_epoch.store(
        0U, std::memory_order_release);
    g_sample_clock_context.armed_ptp_clock = {};
#endif
    static_cast<void>(gptimer_stop(timer));
    static_cast<void>(gptimer_disable(timer));
    static_cast<void>(gptimer_del_timer(timer));
    timer = nullptr;
#if AR_SV_PTP_PHASE_SUPPORTED
    xSemaphoreGive(g_phase_timer_mutex);
#endif
}

#if AR_SV_PTP_PHASE_SUPPORTED && defined(CONFIG_AR_PTP_BOOT_SOURCE_AUTOSTART) && CONFIG_AR_PTP_BOOT_SOURCE_AUTOSTART
// CPU0 control-plane only. PTP SOURCE and the dry sample grid are brought
// online from boot without waiting for a USB host or a first SV START.
// Publisher ISR/smpCnt/BER remain unchanged. Never issue an SV START here.
void sv_boot_clock_supervisor_task(void*) {
    using Boot = ar::iec61850::time_sync::SvBootClockPolicy;
    std::uint32_t attempts = 0U;
    std::uint32_t retry_after_ms = 0U;
    bool ready_once = false;
    bool observed_link_loss = false;
    std::uint32_t observed_policy_generation =
        ar_ptp_lab_advanced_policy_generation();
#if defined(CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL) && CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL
    bool external_runtime_selected = false;
    std::uint64_t external_quiet_since_ms = 0U;
    bool handover_failure_latched = false;
    std::uint32_t next_external_handover_ms = 0U;
#endif
    bool reported_grid = false;
    bool reported_operator_override = false;
    // Let the control and publisher tasks bind before background admission.
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(kTag, "M3B boot timebase: enabled for isolated SOURCE profile; SV remains OFF");
    while (true) {
        const bool operator_overrode =
            !ar_ptp_boot_supervisor_autostart_allowed();
        ar_ptp_lab_config_t config{};
        const bool have_config = ar_ptp_lab_get_config(&config);
        const auto mode = static_cast<
            ar::iec61850::time_sync::PtpAdvancedMode>(
                ar_ptp_lab_advanced_policy());
        using Advanced = ar::iec61850::time_sync::PtpAdvancedPolicy;
        // A role and an intent must both match. This is a fail-closed guard
        // against manual PTPROLE commands leaving a stale AUTO mode behind.
        const bool source_selected = have_config &&
            config.role == AR_PTP_ROLE_LAB_SOURCE &&
            Advanced::starts_source(mode);
        const bool auto_role_eligible = source_selected ||
            (have_config && config.role == AR_PTP_ROLE_TIME_RECEIVER &&
                Advanced::starts_receiver(mode)) ||
            (have_config && config.role == AR_PTP_ROLE_MONITOR &&
                Advanced::starts_monitor(mode));
        const bool link_ready =
            (xEventGroupGetBits(g_link_events) & kLinkUpBit) != 0U;
        const bool runtime_owned =
            ar_ptp_source_runtime_active() || ar_ptp_lab_is_running();
        const auto current_policy_generation =
            ar_ptp_lab_advanced_policy_generation();
        if (current_policy_generation != observed_policy_generation) {
            observed_policy_generation = current_policy_generation;
            // A new operator policy is permitted only after both PTP and
            // SV STOP. Re-arm the bounded boot worker once for that mode.
            // Old external quiet windows and failures never carry across.
            if (!runtime_owned && !live_tx_running()) {
                ready_once = false;
                observed_link_loss = false;
                attempts = 0U;
                retry_after_ms = 0U;
#if defined(CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL) && CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL
                external_runtime_selected = false;
                external_quiet_since_ms = 0U;
                handover_failure_latched = false;
                next_external_handover_ms = 0U;
#endif
                ESP_LOGI(kTag, "M3D policy generation %lu admitted at stopped boundary",
                    static_cast<unsigned long>(observed_policy_generation));
            }
        }
        const auto now_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000LL);
        if (ready_once && !link_ready) observed_link_loss = true;
        // If the original runtime survived the PHY cycle, it remains the
        // sole time authority. Do not retain a stale link-loss permission
        // that could restart it after an unrelated future task failure.
        if (observed_link_loss && link_ready && runtime_owned)
            observed_link_loss = false;
        if (Boot::may_recover_after_link_cycle(
                ready_once, observed_link_loss, link_ready, runtime_owned,
                operator_overrode, source_selected)) {
            // Only a real Ethernet link cycle may re-arm a lost boot SOURCE.
            // SOURCE generation changes and SV phase claims requalify rather
            // than inheriting stale global/LOCAL evidence.
            ready_once = false;
            observed_link_loss = false;
            attempts = 0U;
            retry_after_ms = now_ms + 2000U;
            ESP_LOGW(kTag,
                "M3B PHY recovered with no PTP runtime; retry SOURCE after guard");
        }
        if (Boot::may_start(
                !ready_once, operator_overrode, auto_role_eligible,
                link_ready, runtime_owned, attempts, now_ms, retry_after_ms)) {
            ++attempts;
            // Blocking verified SOURCE start is allowed only in this
            // low-priority worker. It is never called from ISR/publisher.
            const bool started = ar_esp32p4_ptp_start();
            if (started) {
                ready_once = true; // never auto-reseed after a later STOP
                ESP_LOGI(kTag,
                    "M3B SOURCE READY from boot: attempt=%lu; waiting for dry SV grid",
                    static_cast<unsigned long>(attempts));
            } else {
                retry_after_ms = now_ms + Boot::retry_delay_ms(attempts);
                ESP_LOGW(kTag,
                    "M3B boot SOURCE not ready: attempt=%lu/%lu; bounded retry",
                    static_cast<unsigned long>(attempts),
                    static_cast<unsigned long>(Boot::kMaxAttempts));
            }
        }
#if defined(CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL) && CONFIG_AR_PTP_AUTO_PREFER_EXTERNAL
        // SOURCE's existing hardware-Pdelay RX hook observes foreign
        // Announce without competing for the EMAC timestamp owner.
        // Handover is strictly CONTROL PLANE and deferred during active SV.
        const bool stable_better_gm =
            ar_ptp_source_external_candidate_stable();
        const bool may_external = 
            ar::iec61850::time_sync::PtpExternalHandoverGuard::may_handover(
                Advanced::auto_handover(mode), operator_overrode, stable_better_gm,
                live_tx_running(), ar_ptp_source_runtime_active(),
                external_runtime_selected, link_ready);
        if (may_external && !handover_failure_latched &&
            ar::iec61850::time_sync::PtpExternalHandoverGuard::cooldown_expired(
                now_ms, next_external_handover_ms)) {
            // No SV output, so a GM change explicitly invalidates the old
            // phase/lease. The dry grid's preflight owner sees generation
            // change and tears down/re-arms only after RECEIVER is LOCKED.
            ESP_LOGW(kTag,
                "M3C external GM candidate stable and superior: drain SOURCE before RECEIVER");
            ar_ptp_lab_stop();
            if (!ar_ptp_source_runtime_active() &&
                !live_tx_running() &&
                ar_ptp_boot_supervisor_autostart_allowed()) {
                auto follower = config;
                follower.role = AR_PTP_ROLE_TIME_RECEIVER;
                follower.bench_global_gm = false;
                if (ar_ptp_lab_configure(&follower) &&
                    ar_ptp_boot_supervisor_autostart_allowed() &&
                    ar_esp32p4_ptp_start()) {
                    external_runtime_selected = true;
                    external_quiet_since_ms = 0U;
                    // Avoid repeated GM oscillation when several clocks
                    // advertise near-equal priority or a flaky PHY bounces.
                    next_external_handover_ms = now_ms +
                        ar::iec61850::time_sync::PtpExternalHandoverGuard::kMinSwitchDwellMs;
                    ESP_LOGI(kTag,
                        "M3C RECEIVER active; waiting for measured GM/offset/Pdelay LOCK");
                } else {
                    // Do not blind-fallback while the foreign GM exists.
                    // Leave the system unqualified and await an explicit
                    // operator recovery command. No two EMAC role owners.
                    handover_failure_latched = true;
                    ESP_LOGE(kTag,
                        "M3C external handover rejected; auto SOURCE promotion blocked");
                }
            } else {
                handover_failure_latched = true;
                ESP_LOGW(kTag, "M3C handover interrupted by runtime/operator ownership");
            }
        }
        if (external_runtime_selected) {
            ar_ptp_timing_authority_t timing{};
            const bool valid_status =
                ar_ptp_lab_get_timing_authority(&timing) &&
                timing.role == AR_PTP_ROLE_TIME_RECEIVER;
            if (valid_status && timing.source_selected) {
                external_quiet_since_ms = 0U;
            } else if (external_quiet_since_ms == 0U) {
                external_quiet_since_ms = static_cast<std::uint64_t>(now_ms) + 1U;
            }
#if defined(CONFIG_AR_PTP_AUTO_LOCAL_FALLBACK) && CONFIG_AR_PTP_AUTO_LOCAL_FALLBACK
            const std::uint64_t quiet_duration_ms =
                external_quiet_since_ms > 0U &&
                static_cast<std::uint64_t>(now_ms) + 1U >= external_quiet_since_ms
                ? static_cast<std::uint64_t>(now_ms) + 1U - external_quiet_since_ms
                : 0U;
            const bool may_restore =
                ar::iec61850::time_sync::PtpExternalHandoverGuard::may_fallback(
                    Advanced::permits_local_fallback(mode, true),
                    operator_overrode, live_tx_running(),
                    ar_ptp_lab_is_running(), valid_status && timing.source_selected,
                    link_ready, quiet_duration_ms);
            if (may_restore) {
                ESP_LOGW(kTag,
                    "M3C no selected external GM for >=15s: explicit lab fallback");
                ar_ptp_lab_stop(); // fully drain receiver ownership first
                ar_ptp_lab_config_t local = config;
                // Failover changes the clock lease and must reacquire phase.
                // Never recycle a stale Studio seed. When the isolated FAT
                // build explicitly enables boot simulated GM, obtain a NEW
                // monotonic/RTC-derived test-set seed instead of silently
                // regressing from GLOBAL to LOCAL during local fallback.
                local.role = AR_PTP_ROLE_LAB_SOURCE;
                local.bench_global_gm = false;
                local.bench_utc_epoch_seconds = 0U;
                local.bench_utc_epoch_millisecond = 0U;
                local.bench_epoch_received_mono_us = 0LL;
#if defined(CONFIG_AR_PTP_BOOT_SIMULATED_GLOBAL_GM) && CONFIG_AR_PTP_BOOT_SIMULATED_GLOBAL_GM
                ar_ptp_lab_config_t fresh_lab{};
                ar_ptp_lab_get_default_config(&fresh_lab);
                if (fresh_lab.role == AR_PTP_ROLE_LAB_SOURCE &&
                    fresh_lab.bench_global_gm) {
                    local.bench_global_gm = true;
                    local.bench_utc_epoch_seconds = fresh_lab.bench_utc_epoch_seconds;
                    local.bench_utc_epoch_millisecond =
                        fresh_lab.bench_utc_epoch_millisecond;
                    local.bench_epoch_received_mono_us =
                        fresh_lab.bench_epoch_received_mono_us;
                }
#endif
                external_runtime_selected = false;
                // Prevent indefinite repeated local promotions if startup
                // fails; only explicit PTP control may reset this latch.
                handover_failure_latched = true;
                if (ar_ptp_boot_supervisor_autostart_allowed() &&
                    ar_ptp_lab_configure(&local) &&
                    ar_esp32p4_ptp_start()) {
                    ready_once = true;
                    handover_failure_latched = false;
                    next_external_handover_ms = now_ms +
                        ar::iec61850::time_sync::PtpExternalHandoverGuard::kMinSwitchDwellMs;
                    ESP_LOGI(kTag,
                        "M3C restored lab SOURCE; simulated Global GM=%u; new phase required",
                        local.bench_global_gm ? 1U : 0U);
                } else {
                    ESP_LOGE(kTag,
                        "M3C lab SOURCE restoration FAILED; leave PTP unqualified");
                }
            }
#endif
        }
#endif

        const bool dry_phase_observed =
            g_preflight_phase_clock_active.load(std::memory_order_acquire) &&
            g_sample_clock_context.epoch_generation.load(
                std::memory_order_acquire) != 0U &&
            g_sample_clock_context.last_phase_observed_epoch.load(
                std::memory_order_acquire) != 0U;
        if (dry_phase_observed && !reported_grid) {
            ESP_LOGI(kTag,
                "M3B dry PTP/SV sample grid tracking before injection; SV TX OFF");
            reported_grid = true;
        } else if (!dry_phase_observed) {
            reported_grid = false; // SOURCE/GM fault, operator STOP or relock
        }
        if (operator_overrode && !reported_operator_override) {
            ESP_LOGI(kTag,
                "M3B operator owns PTP; boot SOURCE autostart disabled until reboot");
            reported_operator_override = true;
        }
        // Monitoring continues quietly even after manual PTP role control.
        vTaskDelay(pdMS_TO_TICKS(
            operator_overrode || ready_once ? 1000U : Boot::kPollPeriodMs));
    }
}
#endif

void publisher_task(void* argument) {
#if CONFIG_AR_SMV_LIVE_TX && CONFIG_AR_SMV_P2_REALTIME_50HZ
    const auto eth_handle = static_cast<esp_eth_handle_t>(argument);
    const auto task_handle = xTaskGetCurrentTaskHandle();
    live_control_bind_publisher_task(task_handle);

    ESP_LOGW(kTag, "LIVE SV TX ENABLED: isolated lab network only");
    ESP_LOGI(kTag, "Waiting for Ethernet link...");
    xEventGroupWaitBits(g_link_events, kLinkUpBit, pdFALSE, pdTRUE, portMAX_DELAY);

    std::array<std::uint8_t, 6> source_mac{};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, source_mac.data()));
    ESP_LOGI(kTag, "EMAC source MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             source_mac[0], source_mac[1], source_mac[2],
             source_mac[3], source_mac[4], source_mac[5]);

    const auto initial_signal = live_signal_snapshot();
    const auto initial_profile = runtime_profile_snapshot();
    ESP_LOGI(kTag,
             "Live signal engine ready: profile=%s rate=%lu fps signal=%u mHz generation=%llu; use GUI START",
             initial_profile.sv_id.data(),
             static_cast<unsigned long>(initial_profile.publisher_rate_hz),
             static_cast<unsigned>(initial_signal.frequency_millihz),
             static_cast<unsigned long long>(initial_signal.generation));

    PacketTemplate canonical;
    PacketTemplate mirror;
    // One publisher task, one timer and one Ethernet MAC. Extra MUs have
    // dedicated packet storage and DDS phase; no extra RTOS tasks/timers.
    static std::array<PacketTemplate, 2> multi_extra_packets{};
    static std::array<RuntimePublisherProfile, 3> multi_profiles{};
    static std::array<SvFixedPointSineEngine, 2> multi_extra_engines{};
    static std::array<SvLiveSignalState, 2> multi_extra_signals{};
    ar::iec61850::sampled_values::M4ThreeSvGridPlanner multi_grid{};
    bool multi_active = false;
    std::uint64_t multi_tick_index = 0U;
    std::uint64_t multi_ptp_session = 1U;
    RuntimePublisherProfile active_profile = initial_profile;
    static SvFixedPointSineEngine signal_engine;
    gptimer_handle_t sample_clock = nullptr;
    RationalScheduleCursor expected_schedule{};
    std::uint16_t canonical_sample_count = 0U;
    std::uint16_t mirror_sample_count = 0U;
    TimingStats local{};
    bool schedule_anchored = false;
    bool epoch_armed = false;
    std::int64_t expected_wake_us = 0;
#if AR_SV_TX_EGRESS_SUPPORTED
    std::uint32_t zero_epoch_frames_seen = 0U;
#endif
#if AR_SV_PTP_PHASE_SUPPORTED
    std::int32_t previous_slew_total = 0;
#endif
    std::uint32_t consecutive_canonical_tx_failures = 0U;
    esp_err_t last_canonical_tx_error = ESP_OK;

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
    std::array<std::uint8_t, 60> probe_broadcast{};
    std::array<std::uint8_t, 60> probe_multicast{};
#endif

    bool preflight_clock = false;
    std::uint32_t preflight_rate_hz = 0U;
    while (true) {
        // With SV OFF, inspect verified SOURCE/AUTO readiness at 5Hz. The
        // dry clock runs autonomously on the existing ISR; zero SV Ethernet
        // packets are transmitted until the operator's explicit START.
        const std::uint32_t pending_ticks = ulTaskNotifyTake(
            pdTRUE, live_tx_running() ? portMAX_DELAY : pdMS_TO_TICKS(200));
        if (pending_ticks == 0U && live_tx_running()) continue;

        if (take_start_request()) {
            // BEGIN/VALIDATE/COMMIT and explicit SELECT came through one
            // console task while STOPPED. A selected roster is immutable
            // until STOP; never decode XML or allocate in a 250us tick.
            multi_active = false;
            if (const auto* roster =
                    ar::esp32p4::smv::multi_profile_tx_roster_if_selected()) {
                bool valid = true;
                for (std::size_t slot = 0U; slot < multi_profiles.size(); ++slot) {
                    if (ar::esp32p4::smv::map_compiled_runtime_profile(
                            roster[slot], multi_profiles[slot]) !=
                            ar::esp32p4::smv::BinaryRuntimeProfileStatus::ok ||
                        !runtime_profile_validate(multi_profiles[slot]) ||
                        multi_profiles[slot].publisher_rate_hz != 4000U ||
                        multi_profiles[slot].sample_counter_modulus != 4000U) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    ESP_LOGE(kTag, "MULTISV START rejected: invalid or unsupported MU profile");
                    live_control_force_stop();
                    continue;
                }
                multi_active = true;
            }
#if AR_SV_PTP_PHASE_SUPPORTED
            const auto now_lease = read_ptp_epoch_clock_lease();
            const auto requested_profile = multi_active
                ? multi_profiles[0] : runtime_profile_snapshot();
            // A fast STOP→START may arrive before the publisher has run its
            // 200-ms idle/park branch. There is still exactly one valid timer;
            // do not destroy that continuously aligned phase grid just
            // because the control task delivered START first. STOP has
            // already been verified by the serial control state machine.
            const bool source_unchanged = same_sv_ptp_clock_lease(
                g_sample_clock_context.armed_ptp_clock, now_lease);
            const bool profile_unchanged =
                requested_profile.publisher_rate_hz == active_profile.publisher_rate_hz &&
                requested_profile.sample_counter_modulus ==
                    active_profile.sample_counter_modulus;
            const bool auto_mode =
                smp_synch_lab_mode() == SvSyncPolicyMode::external_ptp_auto;
            const bool hot_stopped_grid =
                !preflight_clock &&
                SvAutoPreflightPolicy::can_park_running_grid(
                    sample_clock != nullptr, epoch_armed,
                    g_sample_clock_context.phase_enabled, source_unchanged,
                    profile_unchanged, auto_mode);
            const bool reuse_preflight = !multi_active &&
                sample_clock != nullptr && epoch_armed &&
                requested_profile.sample_counter_modulus ==
                    requested_profile.publisher_rate_hz &&
                SvAutoPreflightPolicy::can_reuse(
                    preflight_clock || hot_stopped_grid,
                    g_sample_clock_context.phase_enabled,
                    source_unchanged,
                    hot_stopped_grid ||
                    preflight_rate_hz == requested_profile.publisher_rate_hz,
                    auto_mode);
            if (!reuse_preflight) stop_sample_clock(sample_clock);
            // Only previously measured SOURCE synchronization on the exact
            // same dry-running sample grid may survive START. The 1-Hz
            // phase observer and per-frame link/fault/lease checks remain
            // authoritative. Fresh MAC descriptors must follow within 8 s.
            const bool current_phase_healthy =
                g_sample_clock_context.phase_authority_valid.load(
                    std::memory_order_acquire);
            const bool phase_observer_fresh =
                ar::iec61850::time_sync::SvPhaseObservationFreshness::fresh(
                    g_sample_clock_context.last_phase_observed_epoch.load(
                        std::memory_order_acquire),
                    g_sample_clock_context.epoch_generation.load(
                        std::memory_order_acquire));
            // STOP can occur between two control-plane phase observations.
            // Do not treat the SV session switch as loss of the time domain:
            // prior measured clock authority was captured when we PARKED.
            const bool resume_certified =
                smp_synch_source_local_qualified() ||
                g_parked_source_clock_certified.load(std::memory_order_acquire);
            const bool warm_resume =
                ar::iec61850::time_sync::SvParkedClockAuthority::may_resume(
                    resume_certified, source_unchanged,
                    reuse_preflight && profile_unchanged,
                    current_phase_healthy, phase_observer_fresh,
                    now_lease.role == PtpAuthorityRole::source, auto_mode);
            // WarmResumePolicy remains an independent admission guard:
            // no new MAC exception on a cold start.
            const bool warm_admitted =
                ar::iec61850::time_sync::SvWarmResumePolicy::may_begin(
                    source_unchanged, reuse_preflight, warm_resume,
                    current_phase_healthy && phase_observer_fresh,
                    now_lease.role == PtpAuthorityRole::source);
#else
            constexpr bool reuse_preflight = false;
            stop_sample_clock(sample_clock);
#endif
            preflight_clock = false;
            active_profile = multi_active
                ? multi_profiles[0] : runtime_profile_snapshot();
            if (!runtime_profile_validate(active_profile)) {
                ESP_LOGE(kTag, "START rejected: active runtime profile is invalid");
                continue;
            }
            if (!build_packet_template(source_mac, active_profile, false, false, canonical)) {
                ESP_LOGE(kTag, "START rejected: canonical template build failed");
                live_control_force_stop();
                continue;
            }
            if (multi_active) {
                bool built = true;
                for (std::size_t slot = 1U; slot < multi_profiles.size(); ++slot) {
                    if (!build_packet_template(source_mac, multi_profiles[slot],
                                               false, false, multi_extra_packets[slot - 1U])) {
                        built = false;
                        break;
                    }
                }
                if (!built) {
                    ESP_LOGE(kTag, "MULTISV START rejected: three canonical templates required");
                    live_control_force_stop();
                    continue;
                }
                for (std::size_t slot = 0U; slot < multi_extra_signals.size(); ++slot) {
                    multi_extra_signals[slot] =
                        ar::esp32p4::smv::multi_profile_signal_snapshot(slot + 1U);
                    multi_extra_engines[slot].reset_phase();
                }
                multi_tick_index = 0U;
            }
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            if (!multi_active &&
                !build_packet_template(source_mac, active_profile, true, true, mirror)) {
                ESP_LOGE(kTag, "START rejected: diagnostic mirror template build failed");
                continue;
            }
#endif
#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
            probe_broadcast = make_probe(source_mac, active_profile.destination_mac, false);
            probe_multicast = make_probe(source_mac, active_profile.destination_mac, true);
#endif
            canonical_sample_count = 0U;
            mirror_sample_count = 0U;
            signal_engine.reset_phase();
            local = TimingStats{};
            reset_interval_stats();
            schedule_anchored = false;
            consecutive_canonical_tx_failures = 0U;
            last_canonical_tx_error = ESP_OK;
            g_sv_tx_faults.new_session();
            expected_schedule.reset(active_profile.publisher_rate_hz);
#if AR_SV_TX_EGRESS_SUPPORTED
            zero_epoch_frames_seen = 0U;
            portENTER_CRITICAL(&g_tx_egress_mux);
            g_local_sync_evidence.reset();
            g_tx_egress_evidence.reset();
            portEXIT_CRITICAL(&g_tx_egress_mux);
#if AR_SV_PTP_PHASE_SUPPORTED
            g_warm_resume_first_epoch.store(
                warm_admitted
                    ? g_sample_clock_context.epoch_generation.load(
                        std::memory_order_acquire) : 0U,
                std::memory_order_release);
            g_warm_resume_baseline_consumed.store(
                false, std::memory_order_release);
#endif
#endif
#if AR_SV_PTP_PHASE_SUPPORTED
            previous_slew_total = 0;
            // Restore only an ALREADY CERTIFIED clock on the same live PTP
            // lease and phase grid. The descriptor tracker will acquire a
            // fresh MAC baseline within its existing bounded warm window.
            // This happens before the first resumed SV packet; never force
            // smpSynch 2 or bypass the bench-global/source-readiness gate.
            if (warm_admitted) {
                smp_synch_set_source_local_qualified(
                    true, SvSyncLossReason::none);
            }
            g_parked_source_clock_certified.store(
                false, std::memory_order_release);
            ESP_LOGI(kTag, "SV CLOCK RESUME %s: same PTP lease=%u phase=%u fresh=%u",
                warm_admitted ? "WARM CERTIFIED" : "COLD REQUALIFY",
                source_unchanged ? 1U : 0U,
                current_phase_healthy ? 1U : 0U,
                phase_observer_fresh ? 1U : 0U);
#endif
            if (multi_active)
                ESP_LOGI(kTag, "MULTISV TX selected: 3 x 4000fps, common GPTimer/PTP sample grid");
            ESP_LOGI(kTag,
                     "PROFILE armed generation=%llu svID=%s APPID=0x%04X rate=%lu wrap=%u confRev=%lu VLAN=%u/%u/%u frameLen=%u",
                     static_cast<unsigned long long>(active_profile.generation),
                     active_profile.sv_id.data(),
                     static_cast<unsigned>(active_profile.app_id),
                     static_cast<unsigned long>(active_profile.publisher_rate_hz),
                     static_cast<unsigned>(active_profile.sample_counter_modulus),
                     static_cast<unsigned long>(active_profile.configuration_revision),
                     active_profile.vlan_present ? 1U : 0U,
                     static_cast<unsigned>(active_profile.vlan_id),
                     static_cast<unsigned>(active_profile.vlan_priority),
                     static_cast<unsigned>(canonical.bytes.size()));

            // Timer commit is the final operation in this branch.
            // Formatting the profile log after timer enable previously queued
            // dozens of ISR notifications before the publisher could consume
            // its first tick, so every START falsely began with missed slots.
            if (reuse_preflight) {
#if AR_SV_PTP_PHASE_SUPPORTED
                // Atomic handoff works for both parked and fast STOP→START
                // cycles. The ISR may have notified once while we disabled
                // it; clear that stale count BEFORE arming the next epoch.
                g_sample_clock_context.launch_on_next_epoch.store(
                    false, std::memory_order_release);
                g_sample_clock_context.publisher_notifications_enabled.store(
                    false, std::memory_order_release);
                static_cast<void>(ulTaskNotifyTake(pdTRUE, 0U));
                g_preflight_phase_clock_active.store(false, std::memory_order_release);
                g_sample_clock_context.launch_on_next_epoch.store(
                    true, std::memory_order_release);
#endif
            } else {
                sample_clock = start_sample_clock(
                    task_handle, active_profile.publisher_rate_hz, eth_handle,
                    epoch_armed,
                    // Only rate==modulus has one exact sample epoch per second.
                    active_profile.sample_counter_modulus ==
                        active_profile.publisher_rate_hz);
                if (!epoch_armed) {
                    static_cast<void>(expected_schedule.next_ticks());
                }
            }
            // The timer/ISR is the sole sampling clock for all selected
            // MUs. Its PTP session generation is shared, not one per stream.
            if (multi_active) {
#if AR_SV_PTP_PHASE_SUPPORTED
                multi_ptp_session = std::max<std::uint64_t>(
                    1U, g_sample_clock_context.session_generation.load(
                        std::memory_order_acquire));
#else
                multi_ptp_session = 1U;
#endif
                multi_grid.reset(multi_ptp_session);

            }
            // Do not log after the timer is enabled: even a single slow
            // console write can accumulate ISR notifications before the
            // publisher consumes its first slot (false START misses).
            // This does not change fail-closed smpSynch AUTO=0.
            continue;
        }

        if (!live_tx_running()) {
#if AR_SV_PTP_PHASE_SUPPORTED
            const auto profile = runtime_profile_snapshot();
            const auto lease = read_ptp_epoch_clock_lease();
            // An external-GM RECEIVER can pre-steer its dry sample clock
            // after disciplined LOCKED/selected-GM lease, just like SOURCE.
            // No canonical SV frame or smpSynch claim is emitted pre-START.
            const bool auto_clock_ready = SvAutoPreflightPolicy::can_prepare(
                smp_synch_lab_mode() == SvSyncPolicyMode::external_ptp_auto,
                (lease.role == PtpAuthorityRole::source ||
                 lease.role == PtpAuthorityRole::receiver) &&
                    valid_sv_ptp_clock_lease(lease) &&
                    (xEventGroupGetBits(g_link_events) & kLinkUpBit) != 0U,
                runtime_profile_validate(profile), profile.publisher_rate_hz,
                profile.sample_counter_modulus);
            // M3.A lifecycle: the operator stopped SV transmission, NOT
            // the PTP time domain. Keep the already-disciplined GPTimer and
            // phase observer running silently while the 4I4V output is OFF.
            // Do not allocate another timer, reset the epoch, or wake the
            // publisher 4000 times/s. Profile/source faults take the existing
            // fail-closed teardown path instead of preserving stale proof.
            if (sample_clock != nullptr && !preflight_clock) {
                const bool same_lease = same_sv_ptp_clock_lease(
                    g_sample_clock_context.armed_ptp_clock, lease);
                const bool park = SvAutoPreflightPolicy::can_park_running_grid(
                    true, epoch_armed, g_sample_clock_context.phase_enabled,
                    same_lease,
                    profile.publisher_rate_hz == active_profile.publisher_rate_hz &&
                    profile.sample_counter_modulus == active_profile.sample_counter_modulus,
                    auto_clock_ready);
                if (park) {
                    const bool clock_was_certified =
                        ar::iec61850::time_sync::SvParkedClockAuthority::may_preserve(
                            smp_synch_source_local_qualified(),
                            lease.role == PtpAuthorityRole::source, same_lease,
                            true,
                            g_sample_clock_context.phase_authority_valid.load(
                                std::memory_order_acquire),
                            ar::iec61850::time_sync::SvPhaseObservationFreshness::fresh(
                                g_sample_clock_context.last_phase_observed_epoch.load(
                                    std::memory_order_acquire),
                                g_sample_clock_context.epoch_generation.load(
                                    std::memory_order_acquire)),
                            smp_synch_lab_mode() == SvSyncPolicyMode::external_ptp_auto);
                    g_parked_source_clock_certified.store(
                        clock_was_certified, std::memory_order_release);
                    // ISR can have one in-flight wake at this edge; the next
                    // START explicitly drains it before enabling epoch launch.
                    g_sample_clock_context.launch_on_next_epoch.store(
                        false, std::memory_order_release);
                    g_sample_clock_context.publisher_notifications_enabled.store(
                        false, std::memory_order_release);
                    g_preflight_phase_clock_active.store(
                        true, std::memory_order_release);
                    preflight_clock = true;
                    preflight_rate_hz = profile.publisher_rate_hz;
                    ESP_LOGI(kTag,
                        "M3E PARKED: SV TX OFF, PTP/4k clock continuous certified=%u",
                        clock_was_certified ? 1U : 0U);
                } else {
                    stop_sample_clock(sample_clock);
                    epoch_armed = false;
                }
            }
            if (preflight_clock && (!auto_clock_ready ||
                preflight_rate_hz != profile.publisher_rate_hz ||
                !same_sv_ptp_clock_lease(
                    g_sample_clock_context.armed_ptp_clock, lease))) {
                stop_sample_clock(sample_clock);
                preflight_clock = false;
                epoch_armed = false;
            }
            if (sample_clock == nullptr && auto_clock_ready) {
                bool aligned = false;
                sample_clock = start_sample_clock(
                    task_handle, profile.publisher_rate_hz, eth_handle,
                    aligned, true, true);
                if (aligned && g_sample_clock_context.phase_enabled) {
                    epoch_armed = true;
                    preflight_clock = true;
                    preflight_rate_hz = profile.publisher_rate_hz;
                    g_preflight_phase_clock_active.store(true, std::memory_order_release);
                    ESP_LOGI(kTag, "SV/PTP AUTO preflight sample clock armed (SV TX OFF)");
                } else {
                    stop_sample_clock(sample_clock);
                }
            }
#else
            stop_sample_clock(sample_clock);
#endif
            schedule_anchored = false;
            continue;
        }
        if (sample_clock == nullptr) continue;

        const std::int64_t actual_wake_us = esp_timer_get_time();
        std::uint64_t missed_slots = 0U;
        if (!schedule_anchored) {
            expected_wake_us = actual_wake_us;
            schedule_anchored = true;
        } else {
            std::uint64_t elapsed_ticks = 0U;
            for (std::uint32_t i = 0U; i < pending_ticks; ++i) {
                elapsed_ticks += expected_schedule.next_ticks();
            }
            expected_wake_us += static_cast<std::int64_t>(elapsed_ticks);
#if AR_SV_PTP_PHASE_SUPPORTED
            // Preserve the existing lateness metric when the ISR slews its
            // timer deadlines. Otherwise planned corrective ticks would be
            // misreported as publisher latency/drift.
            const std::int32_t latest_slew_total =
                g_sample_clock_context.cumulative_slew_ticks.load(
                    std::memory_order_relaxed);
            expected_wake_us +=
                ar::iec61850::time_sync::sv_phase_detail::signed_delta_u32(
                    static_cast<std::uint32_t>(latest_slew_total),
                    static_cast<std::uint32_t>(previous_slew_total));
            previous_slew_total = latest_slew_total;
#endif
        }

        const auto signal_state = multi_active
            ? ar::esp32p4::smv::multi_profile_signal_snapshot(0U)
            : live_signal_snapshot();
        if (multi_active) {
            for (std::size_t slot = 0U; slot < multi_extra_signals.size(); ++slot) {
                multi_extra_signals[slot] =
                    ar::esp32p4::smv::multi_profile_signal_snapshot(slot + 1U);
            }
        }
        if (pending_ticks > 1U) {
#if AR_SV_PTP_PHASE_SUPPORTED
            // A skipped frame breaks canonical timeline evidence, even if
            // GPTimer deadlines were mathematically aligned. Fault-only
            // atomic operations; the normal 4k/4.8kHz path is untouched.
            g_sample_clock_context.publisher_fault_generation.fetch_add(
                1U, std::memory_order_release);
            g_sample_clock_context.phase_authority_valid.store(
                false, std::memory_order_release);
            revoke_sv_auto_claims(SvSyncLossReason::missed_slot);
#endif
            missed_slots = static_cast<std::uint64_t>(pending_ticks - 1U);
            canonical_sample_count = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(canonical_sample_count) + missed_slots) %
                active_profile.sample_counter_modulus);
            mirror_sample_count = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(mirror_sample_count) +
                static_cast<std::uint32_t>(missed_slots));
            signal_engine.advance(
                signal_state,
                active_profile.publisher_rate_hz,
                static_cast<std::uint32_t>(missed_slots));
            if (multi_active) {
                for (std::size_t slot = 0U; slot < multi_extra_engines.size(); ++slot)
                    multi_extra_engines[slot].advance(
                        multi_extra_signals[slot], 4000U,
                        static_cast<std::uint32_t>(missed_slots));
                multi_tick_index += missed_slots;
            }
        }

        const std::int64_t lateness_us = actual_wake_us - expected_wake_us;
        const auto row = signal_engine.next(signal_state, active_profile.publisher_rate_hz);

        if ((xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U) {
            ++local.canonical_fail;
            last_canonical_tx_error = ESP_ERR_INVALID_STATE;
            ++consecutive_canonical_tx_failures;
            const bool first_link_failure = g_sv_tx_faults.failure(
                static_cast<std::int32_t>(ESP_ERR_INVALID_STATE), false);
#if AR_SV_PTP_PHASE_SUPPORTED
            // The first failure revokes stale evidence immediately. Repeating
            // the same invalidation 400 times in the 4-kHz hot path cannot
            // improve safety and adds work while the MAC is already failing.
            if (first_link_failure) {
                g_sample_clock_context.publisher_fault_generation.fetch_add(
                    1U, std::memory_order_release);
                g_sample_clock_context.phase_authority_valid.store(
                    false, std::memory_order_release);
                revoke_sv_auto_claims(SvSyncLossReason::ethernet_link);
            }
#else
            (void)first_link_failure;
#endif
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            ++local.mirror_fail;
#endif
        } else {
#if AR_SV_PTP_PHASE_SUPPORTED
            // After qualification, consult the lightweight SOURCE freshness
            // authority on each frame; a stopped/faulted PTP source must not
            // continue advertising previously measured local synchronization.
            if (smp_synch_source_local_qualified() ||
                smp_synch_receiver_timeline_qualified()) {
#if AR_SV_TX_EGRESS_SUPPORTED
                const auto warm_first = g_warm_resume_first_epoch.load(
                    std::memory_order_acquire);
                if (warm_first != 0U &&
                    !ar::iec61850::time_sync::SvWarmResumePolicy::within_window(
                        warm_first, g_sample_clock_context.epoch_generation.load(
                            std::memory_order_acquire))) {
                    g_warm_resume_first_epoch.store(0U, std::memory_order_release);
                    revoke_sv_auto_claims(SvSyncLossReason::mac_baseline_reset);
                }
#endif
                const bool phase_ok = g_sample_clock_context.phase_authority_valid.load(
                    std::memory_order_acquire);
                const bool observer_fresh =
                    ar::iec61850::time_sync::SvPhaseObservationFreshness::fresh(
                        g_sample_clock_context.last_phase_observed_epoch.load(
                            std::memory_order_acquire),
                        g_sample_clock_context.epoch_generation.load(
                            std::memory_order_acquire));
                const bool source_generation_ok =
                    g_sample_clock_context.armed_ptp_clock.role !=
                        PtpAuthorityRole::source ||
                    ar_ptp_lab_source_generation() ==
                        g_sample_clock_context.armed_ptp_clock.generation;
                if (!source_generation_ok)
                    revoke_sv_auto_claims(SvSyncLossReason::ptp_lease);
                else if (!observer_fresh)
                    revoke_sv_auto_claims(SvSyncLossReason::observer_stale);
                else if (!phase_ok)
                    revoke_sv_auto_claims(SvSyncLossReason::phase_or_mac);
            }
#endif
            const auto sync_decision = smp_synch_lab_decision();
            const auto sample_synchronization =
                static_cast<std::uint8_t>(sync_decision.value);
            if (multi_active) {
#if AR_SV_PTP_PHASE_SUPPORTED
                const auto actual_session = std::max<std::uint64_t>(
                    1U, g_sample_clock_context.session_generation.load(
                        std::memory_order_acquire));
#else
                const auto actual_session = multi_ptp_session;
#endif
                const auto shared = multi_grid.plan({
                    actual_session, multi_tick_index, sample_synchronization, true});
                // A change to the one sample clock/lease revokes the whole
                // three-stream batch, rather than sending three stale epochs.
                if (shared.timing_discontinuity || shared.count != 3U ||
                    shared.frames[0].smp_cnt != canonical_sample_count) {
                    ESP_LOGE(kTag, "MULTISV shared PTP/sample grid revoked");
                    live_control_force_stop();
                    stop_sample_clock(sample_clock);
                    schedule_anchored = false;
                    continue;
                }
            }
            // One mask snapshot per batch. A disabled MU still advances its
            // DDS phase on the shared 250us grid; it simply skips Ethernet.
            const std::uint8_t multi_enabled_mask = multi_active
                ? ar::esp32p4::smv::multi_profile_enabled_mask() : 0x01U;
            const bool send_primary = (multi_enabled_mask & 0x01U) != 0U;
            if (send_primary) {
                patch_packet(
                    canonical, canonical_sample_count, sample_synchronization, row, signal_state);
            }
            // One timestamp BEFORE all three canonical EMAC submissions.
            // Budget telemetry is folded into the existing 400-tick stats
            // flush, never logged or malloc'ed on the real-time path.
            const std::int64_t multi_batch_start_us =
                multi_active ? esp_timer_get_time() : 0LL;
#if AR_SV_TX_EGRESS_SUPPORTED
            // Every 2 seconds, replace normal SV TX for the real canonical
            // frame with descriptor-stamped TX. This reaches two valid
            // measured pairs after ~6 seconds when all independent clocks
            // qualify. ESP-IDF polls the DMA descriptor: NEVER do this on
            // every frame; a slow TX must fail closed via no_missed_slots.
            // This is diagnostic evidence, not validated IED synchronization.
            bool take_egress_timestamp = false;
            SvTxEgressObservation egress{};
            SvPtpClockLease evidence_lease{};
            std::uint32_t evidence_session = 0U;
            std::uint32_t fault_generation_before = 0U;
            if (send_primary && epoch_armed && g_sample_clock_context.phase_enabled &&
                canonical_sample_count == 0U && missed_slots == 0U &&
                active_profile.sample_counter_modulus ==
                    active_profile.publisher_rate_hz) {
                ++zero_epoch_frames_seen;
                if ((zero_epoch_frames_seen % kEgressEvidenceEveryZeroEpochs) == 0U &&
                    ar_ptp_lab_is_running()) {
                    // SOURCE-only generation accessor is ZERO for RECEIVER.
                    // Both roles must use the same source-scoped canonical
                    // lease as the phase observer, before and after TX.
                    const auto live_lease = read_ptp_epoch_clock_lease();
                    if (same_sv_ptp_clock_lease(
                            g_sample_clock_context.armed_ptp_clock,
                            live_lease)) {
                    take_egress_timestamp = true;
                    evidence_lease = live_lease;
                    evidence_session = g_sample_clock_context.session_generation.load(
                        std::memory_order_acquire);
                    egress.sample_epoch_generation =
                        g_sample_clock_context.epoch_generation.load(
                            std::memory_order_acquire);
                    egress.publisher_session_generation = evidence_session;
                    egress.ptp_source_generation = evidence_lease.generation;
                    egress.scheduled_alarm_ticks_low32 =
                        g_sample_clock_context.last_epoch_alarm.load(
                            std::memory_order_acquire);
                    fault_generation_before =
                        g_sample_clock_context.publisher_fault_generation.load(
                            std::memory_order_acquire);
                    egress.publisher_fault_generation = fault_generation_before;
                    egress.smp_cnt = canonical_sample_count;
                    egress.no_missed_slots = pending_ticks == 1U;
                    }
                }
            }
            eth_mac_time_t egress_hw_time{};
            const std::int64_t submit_before_us =
                take_egress_timestamp ? esp_timer_get_time() : 0;
            const esp_err_t canonical_result = !send_primary ? ESP_OK :
                take_egress_timestamp
                ? esp_eth_transmit_ctrl_vargs(
                    eth_handle, &egress_hw_time, 2U, canonical.bytes.data(),
                    static_cast<std::uint32_t>(canonical.bytes.size()))
                : esp_eth_transmit(
                    eth_handle, canonical.bytes.data(), canonical.bytes.size());
            if (take_egress_timestamp) {
                const std::int64_t submit_after_us = esp_timer_get_time();
                const std::int64_t duration =
                    submit_after_us >= submit_before_us
                    ? submit_after_us - submit_before_us : 0LL;
                egress.submit_duration_us = static_cast<std::uint32_t>(
                    std::min<std::int64_t>(duration, 0xFFFFFFFFLL));
                // A descriptor polling delay that exceeds one scheduled
                // sample interval is diagnostic interference: retain the TX
                // timestamp but mark the sample's timing evidence unqualified.
                egress.no_missed_slots = egress.no_missed_slots &&
                    egress.submit_duration_us <=
                        kTimerResolutionHz / active_profile.publisher_rate_hz;
                egress.canonical_tx_success = canonical_result == ESP_OK;
                egress.hw_ptp_seconds = egress_hw_time.seconds;
                egress.hw_ptp_nanoseconds = egress_hw_time.nanoseconds;
                egress.same_sample_epoch = egress.ptp_source_generation != 0U &&
                    same_sv_ptp_clock_lease(
                        evidence_lease, read_ptp_epoch_clock_lease()) &&
                    evidence_session != 0U &&
                    g_sample_clock_context.session_generation.load(
                        std::memory_order_acquire) == evidence_session &&
                    g_sample_clock_context.epoch_generation.load(
                        std::memory_order_acquire) == egress.sample_epoch_generation &&
                    g_sample_clock_context.publisher_fault_generation.load(
                        std::memory_order_acquire) == fault_generation_before;
                portENTER_CRITICAL(&g_tx_egress_mux);
                const auto result = g_tx_egress_evidence.record(egress);
                const auto phase_ns = g_tx_egress_evidence.summary().last_phase_ns;
                g_local_sync_evidence.observe(egress, result, phase_ns);
                portEXIT_CRITICAL(&g_tx_egress_mux);
                // Keep the EXACT real descriptor rejection class. This runs
                // once every two seconds, never per-frame or in an ISR.
                using Evidence = ar::iec61850::time_sync::SvTxEvidenceResult;
                if (result != Evidence::measured) {
                    // On a genuinely warm SOURCE restart, the FIRST new MAC
                    // baseline is expected. It is not lost sample phase.
                    // One exemption only; repeated baselines or any real
                    // descriptor/lease/TX fault revoke immediately.
                    const auto warm_first = g_warm_resume_first_epoch.load(
                        std::memory_order_acquire);
                    const bool same_warm_lease =
                        evidence_lease.role == PtpAuthorityRole::source &&
                        same_sv_ptp_clock_lease(
                            evidence_lease, read_ptp_epoch_clock_lease());
                    bool expected_first_baseline = false;
                    if (result == Evidence::baseline_established &&
                        same_warm_lease &&
                        ar::iec61850::time_sync::SvWarmResumePolicy::within_window(
                            warm_first, egress.sample_epoch_generation)) {
                        bool unused = false;
                        expected_first_baseline =
                            g_warm_resume_baseline_consumed.compare_exchange_strong(
                                unused, true, std::memory_order_acq_rel);
                    }
                    if (!expected_first_baseline) {
                    const auto cause = result == Evidence::transmit_failed
                        ? SvSyncLossReason::canonical_tx
                        : result == Evidence::missing_hw_timestamp
                            ? SvSyncLossReason::mac_missing_timestamp
                        : result == Evidence::not_a_qualified_epoch
                            ? SvSyncLossReason::mac_bad_epoch
                        : result == Evidence::publisher_discontinuity
                            ? SvSyncLossReason::mac_publisher_discontinuity
                        : result == Evidence::ptp_clock_discontinuity
                            ? SvSyncLossReason::mac_ptp_clock_step
                        : result == Evidence::baseline_established
                            ? SvSyncLossReason::mac_baseline_reset
                        : SvSyncLossReason::mac_egress;
                    revoke_sv_auto_claims(cause);
                    g_warm_resume_first_epoch.store(0U, std::memory_order_release);
                    }
                }
            }
#else
            const esp_err_t canonical_result = send_primary
                ? esp_eth_transmit(eth_handle, canonical.bytes.data(), canonical.bytes.size())
                : ESP_OK;
#endif
            if (send_primary) {
            if (canonical_result == ESP_OK) {
                ++local.canonical_ok;
                if (consecutive_canonical_tx_failures != 0U)
                    g_sv_tx_faults.recovered();
                consecutive_canonical_tx_failures = 0U;
                last_canonical_tx_error = ESP_OK;
            } else {
                ++local.canonical_fail;
                last_canonical_tx_error = canonical_result;
                ++consecutive_canonical_tx_failures;
                const bool first_tx_failure = g_sv_tx_faults.failure(
                    static_cast<std::int32_t>(canonical_result),
                    (xEventGroupGetBits(g_link_events) & kLinkUpBit) != 0U);
#if AR_SV_PTP_PHASE_SUPPORTED
                if (first_tx_failure) {
                    g_sample_clock_context.publisher_fault_generation.fetch_add(
                        1U, std::memory_order_release);
                    g_sample_clock_context.phase_authority_valid.store(
                        false, std::memory_order_release);
                    revoke_sv_auto_claims(SvSyncLossReason::canonical_tx);
                }
#else
                (void)first_tx_failure;
#endif
            }

            } // primary-MU enable gate

            // M4: exactly two additional canonical SV frames from the same
            // hardware-timed tick. Each has independent prebuilt packet bytes
            // and DDS state. The three frames share smpCnt/smpSynch; egress
            // uses ONE EMAC and is necessarily serialized. No malloc, locks,
            // XML traversal or logging on a successful 250us batch.
            if (multi_active && live_tx_running()) {
                for (std::size_t slot = 0U; slot < multi_extra_packets.size(); ++slot) {
                    const auto extra_row = multi_extra_engines[slot].next(
                        multi_extra_signals[slot], 4000U);
                    // Advance phase even when muted, so re-enabled frames
                    // rejoin this same shared PTP sample epoch.
                    if ((multi_enabled_mask & (1U << (slot + 1U))) == 0U) continue;
                    patch_packet(multi_extra_packets[slot], canonical_sample_count,
                                 sample_synchronization, extra_row,
                                 multi_extra_signals[slot]);
                    const auto tx = esp_eth_transmit(
                        eth_handle, multi_extra_packets[slot].bytes.data(),
                        multi_extra_packets[slot].bytes.size());
                    if (tx == ESP_OK) {
                        ++local.canonical_ok;
                        if (consecutive_canonical_tx_failures != 0U)
                            g_sv_tx_faults.recovered();
                        consecutive_canonical_tx_failures = 0U;
                        last_canonical_tx_error = ESP_OK;
                    } else {
                        ++local.canonical_fail;
                        last_canonical_tx_error = tx;
                        ++consecutive_canonical_tx_failures;
                        const bool first = g_sv_tx_faults.failure(
                            static_cast<std::int32_t>(tx),
                            (xEventGroupGetBits(g_link_events) & kLinkUpBit) != 0U);
#if AR_SV_PTP_PHASE_SUPPORTED
                        if (first) {
                            g_sample_clock_context.publisher_fault_generation.fetch_add(
                                1U, std::memory_order_release);
                            g_sample_clock_context.phase_authority_valid.store(
                                false, std::memory_order_release);
                            revoke_sv_auto_claims(SvSyncLossReason::canonical_tx);
                        }
#else
                        (void)first;
#endif
                    }
                }
            }

#if CONFIG_AR_SMV_BROADCAST_MIRROR
            if (!multi_active) {
            patch_packet(
                mirror, mirror_sample_count, sample_synchronization, row, signal_state);
            const esp_err_t mirror_result =
                esp_eth_transmit(eth_handle, mirror.bytes.data(), mirror.bytes.size());
            if (mirror_result == ESP_OK) ++local.mirror_ok;
            else ++local.mirror_fail;
            }
#endif

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
            if (!multi_active && canonical_sample_count == 0U) {
                static_cast<void>(esp_eth_transmit(
                    eth_handle, probe_broadcast.data(), probe_broadcast.size()));
                static_cast<void>(esp_eth_transmit(
                    eth_handle, probe_multicast.data(), probe_multicast.size()));
            }
#endif
            if (multi_active && multi_enabled_mask != 0U) {
                const std::int64_t elapsed = esp_timer_get_time() - multi_batch_start_us;
                const auto bounded = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(elapsed, 0LL, 0xFFFFFFFFLL));
                local.multi_batch_max_us = std::max(local.multi_batch_max_us, bounded);
                ++local.multi_batches;
                if (bounded > 250U) ++local.multi_batch_overruns;
            }
        }

        update_local_timing(
            local, lateness_us, missed_slots, canonical_sample_count, signal_state.generation);
        g_sample_tick_total.fetch_add(1U, std::memory_order_relaxed);
        if (consecutive_canonical_tx_failures >= kTransportFailureStopThreshold) {
            g_sv_tx_faults.stopped(
                static_cast<std::uint32_t>(esp_timer_get_time() / 1000LL));
            const auto fault = g_sv_tx_faults.snapshot();
            ESP_LOGE(kTag,
                     "SV transport fault: %lu consecutive TX failures; first=%s "
                     "last=%s link_at_failure=%u burst=%lu total=%lu "
                     "stop_seq=%lu; forcing STOP (fail closed)",
                     static_cast<unsigned long>(consecutive_canonical_tx_failures),
                     esp_err_to_name(static_cast<esp_err_t>(fault.stop_first_error)),
                     esp_err_to_name(last_canonical_tx_error),
                     fault.stop_link_up ? 1U : 0U,
                     static_cast<unsigned long>(fault.stop_streak),
                     static_cast<unsigned long>(fault.failed_frames),
                     static_cast<unsigned long>(fault.stop_sequence));
            merge_stats(local);
            live_control_force_stop();
            stop_sample_clock(sample_clock);
            schedule_anchored = false;
            consecutive_canonical_tx_failures = 0U;
            continue;
        }
        canonical_sample_count = static_cast<std::uint16_t>(
            (canonical_sample_count + 1U) % active_profile.sample_counter_modulus);
        if (multi_active) ++multi_tick_index;
        mirror_sample_count = static_cast<std::uint16_t>(mirror_sample_count + 1U);
        if (local.samples >= kStatsMergeEvery) merge_stats(local);
    }
#else
    (void)argument;
    ESP_LOGW(kTag, "Realtime publisher is disabled in menuconfig");
    vTaskDelete(nullptr);
#endif
}

#if AR_SV_PTP_PHASE_SUPPORTED
// Revoke only the matching GPTimer generation; a stale monitor must never
// overwrite the new session's phase gate following STOP/START.
void revoke_sv_phase_session(const std::uint32_t expected_session) noexcept {
    xSemaphoreTake(g_phase_timer_mutex, portMAX_DELAY);
    if (expected_session != 0U &&
        g_sample_clock_context.session_generation.load(
            std::memory_order_acquire) == expected_session) {
        g_sample_clock_context.phase_authority_valid.store(
            false, std::memory_order_release);
        g_sample_clock_context.requested_slew_ticks.store(
            0, std::memory_order_relaxed);
        revoke_sv_auto_claims(SvSyncLossReason::ptp_lease);
    }
    xSemaphoreGive(g_phase_timer_mutex);
}

// Shadow-verified software phase servo. This task never controls smpSynch,
// executes no ISR code and acquires a GPTimer handle under a teardown mutex.
// It samples clocks once per sample-counter epoch, 250ms after smpCnt=0.
void sv_ptp_phase_observer_task(void* argument) {
    const auto eth_handle = static_cast<esp_eth_handle_t>(argument);
    SvPtpPhaseAuthority controller;
    SvPtpPublisherContinuityGate continuity;
    SvLocalRefAcquisition local_acquisition;
    SvSourceSelfClockAcquisition source_fast_acquisition;
    std::uint32_t accepted_epochs = 0U;
    std::uint32_t controller_session = 0U;
    bool previous_auto_local = false;
    while (true) {
        const std::uint32_t events = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const std::uint32_t notified_session =
            g_sample_clock_context.session_generation.load(
                std::memory_order_acquire);
        if (controller_session != notified_session) {
            controller.reset();
            continuity.reset();
            local_acquisition.reset();
            source_fast_acquisition.reset();
            accepted_epochs = 0U;
            previous_auto_local = false;
            controller_session = notified_session;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        const std::uint32_t current_faults =
            g_sample_clock_context.publisher_fault_generation.load(
                std::memory_order_acquire);
        const auto lease = read_ptp_epoch_clock_lease();
        // Only the 1Hz phase observer emits transition diagnostics. Never
        // log from ISR or the 4kHz TX path. Do not mask genuine evidence loss.
        const auto revoke_with_reason = [&](const char* reason) {
            const bool previously_local = previous_auto_local ||
                smp_synch_source_local_qualified() ||
                smp_synch_receiver_timeline_qualified();
            if (previously_local) {
                ESP_LOGW(kTag,
                         "SV/PTP LOCAL REVOKED reason=%s session=%lu fault_gen=%lu "
                         "source_gen=%lu phase_epoch=%lu",
                         reason,
                         static_cast<unsigned long>(notified_session),
                         static_cast<unsigned long>(current_faults),
                         static_cast<unsigned long>(lease.generation),
                         static_cast<unsigned long>(
                             g_sample_clock_context.epoch_generation.load(
                                 std::memory_order_acquire)));
            }
            static_cast<void>(controller.invalidate());
            local_acquisition.reset();
            source_fast_acquisition.reset();
            revoke_sv_phase_session(notified_session);
            previous_auto_local = false;
        };
        // A role/GM/relock change revokes the previous PI integrator.
        if (!valid_sv_ptp_clock_lease(lease) ||
            !continuity.unchanged(notified_session, current_faults,
                                  lease.generation)) {
            revoke_with_reason("source_lease_or_publisher_fault");
            continue;
        }

        // SV RUNNING and preflight are asynchronous publisher/session flags.
        // STOP toggles RUNNING before the publisher can publish preflight.
        // A still-owned GPTimer/EMAC PTP lease cannot lose clock alignment
        // merely because both flags happen to be false during this handoff.
        if (events != 1U ||
            (xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U ||
            !same_sv_ptp_clock_lease(lease, read_ptp_epoch_clock_lease())) {
            revoke_with_reason("epoch_notification_or_link");
            continue;
        }

        std::uint64_t before = 0U;
        std::uint64_t after = 0U;
        eth_mac_time_t ptp{};
        bool evidence_valid = false;
        SvPtpPhaseObservation observation{};
        SvPtpClockLease armed_lease{};
        xSemaphoreTake(g_phase_timer_mutex, portMAX_DELAY);
        const gptimer_handle_t timer = g_monitored_sample_clock;
        if (timer != nullptr && notified_session != 0U &&
            g_sample_clock_context.session_generation.load(
                std::memory_order_acquire) == notified_session) {
            armed_lease = g_sample_clock_context.armed_ptp_clock;
            const std::uint32_t generation_before =
                g_sample_clock_context.epoch_generation.load(
                    std::memory_order_acquire);
            const esp_err_t before_rc = gptimer_get_raw_count(timer, &before);
            const esp_err_t ptp_rc = esp_eth_ioctl(
                eth_handle, static_cast<esp_eth_io_cmd_t>(
                    ETH_MAC_ESP_CMD_G_PTP_TIME), &ptp);
            const esp_err_t after_rc = gptimer_get_raw_count(timer, &after);
            const std::uint32_t generation_after =
                g_sample_clock_context.epoch_generation.load(
                    std::memory_order_acquire);
            if (before_rc == ESP_OK && ptp_rc == ESP_OK &&
                after_rc == ESP_OK && generation_before != 0U &&
                generation_before == generation_after &&
                ptp.nanoseconds >= 100000000U &&
                ptp.nanoseconds <= 800000000U) {
                observation = SvPtpPhaseObservation{
                    static_cast<std::uint32_t>(before),
                    static_cast<std::uint32_t>(after),
                    g_sample_clock_context.last_epoch_alarm.load(
                        std::memory_order_acquire),
                    generation_after,
                    static_cast<std::uint64_t>(ptp.seconds),
                    ptp.nanoseconds};
                evidence_valid = true;
            }
        }
        xSemaphoreGive(g_phase_timer_mutex);

        if (!evidence_valid ||
            !same_sv_ptp_clock_lease(armed_lease, lease) ||
            !same_sv_ptp_clock_lease(lease, read_ptp_epoch_clock_lease())) {
            revoke_with_reason("ptp_time_read_or_lease_changed");
            continue;
        }

        const auto decision = controller.observe(observation);
        // Snapshot the independently stamped canonical SV evidence outside
        // the GPTimer mutex. Source identity and fault/session generations
        // must match the exact phase observation before any AUTO promotion.
        bool source_local_qualified = false;
        bool source_fast_mac_qualified = false;
        bool measured_phase_qualified = false;
        std::uint32_t mac_pairs = 0U;
#if AR_SV_TX_EGRESS_SUPPORTED
        if ((lease.role == PtpAuthorityRole::source ||
             lease.role == PtpAuthorityRole::receiver) &&
            decision.accepted && decision.epoch_second_continuous) {
            const std::int64_t abs_phase_us = decision.phase_error_us < 0
                ? -static_cast<std::int64_t>(decision.phase_error_us)
                : static_cast<std::int64_t>(decision.phase_error_us);
            const std::uint64_t worst_case_phase_us =
                static_cast<std::uint64_t>(abs_phase_us) +
                decision.observation_uncertainty_us;
            // The external RECEIVER remains strict (4 us). Internal GM
            // samples the very same EMAC oscillator used for PTP packets;
            // allow bounded 2-3us read-bracket quantization without
            // toggling smpSynch 2 -> 0 every second. This is NOT a bypass:
            // phase tracking, consecutive epochs, MAC TX timestamps,
            // same source/session and zero missed frames still apply.
            const std::uint32_t phase_bound_us =
                lease.role == PtpAuthorityRole::source
                    ? SvLocalSyncEvidenceGate::kMaxSourceSoftwarePhaseBoundUs
                    : SvLocalSyncEvidenceGate::kMaxSoftwarePhaseBoundUs;
            measured_phase_qualified = decision.software_tracking &&
                worst_case_phase_us <= phase_bound_us;
            portENTER_CRITICAL(&g_tx_egress_mux);
            const std::uint32_t bound_us = worst_case_phase_us <= UINT32_MAX
                ? static_cast<std::uint32_t>(worst_case_phase_us) : UINT32_MAX;
            source_local_qualified = g_local_sync_evidence.qualified(
                notified_session, current_faults, lease.generation,
                observation.last_sv_epoch_generation,
                decision.software_tracking, bound_us);
            // Faster admission is exclusively for the ESP32's *own*
            // PTP SOURCE (shared EMAC clock). No receiver/external-GM bypass.
            if (lease.role == PtpAuthorityRole::source) {
                source_fast_mac_qualified =
                    g_local_sync_evidence.qualified_source_fast(
                        notified_session, current_faults, lease.generation,
                        observation.last_sv_epoch_generation,
                        decision.software_tracking, bound_us);
            }
            mac_pairs = g_local_sync_evidence.measured_pairs();
            portEXIT_CRITICAL(&g_tx_egress_mux);
        }
#endif
        // Handoff and GPTimer deletion share a control-plane mutex:
        // neither a stale sample nor a recycled timer can receive a command.
        bool committed = false;
        xSemaphoreTake(g_phase_timer_mutex, portMAX_DELAY);
        // Check fault generation at the serialized command handoff, not
        // earlier while this task may be waiting for timer teardown.
        const bool no_new_publisher_fault =
            g_sample_clock_context.publisher_fault_generation.load(
                std::memory_order_acquire) == current_faults;
        const bool session_matches = notified_session != 0U &&
            g_monitored_sample_clock != nullptr &&
            g_sample_clock_context.session_generation.load(
                std::memory_order_acquire) == notified_session;
        // Serial START becomes RUNNING before the next PTP-second gate
        // physically enables publisher notifications. Until that ISR edge,
        // continue observing a DRY synchronized clock, not a missing MAC
        // stream. Also covers STOP arriving before the PARK control pass.
        const bool injecting = SvAutoPreflightPolicy::live_publisher_active(
            live_tx_running(),
            g_sample_clock_context.publisher_notifications_enabled.load(
                std::memory_order_acquire));
        const bool clock_timer_owned_and_enabled =
            g_monitored_sample_clock != nullptr &&
            g_sample_clock_context.phase_enabled;
        const bool auto_sync_mode =
            smp_synch_lab_mode() == SvSyncPolicyMode::external_ptp_auto;
        if (g_sample_clock_context.epoch_generation.load(
                std::memory_order_acquire) == observation.last_sv_epoch_generation &&
            SvAutoPreflightPolicy::can_steer_owned_grid(
                session_matches, decision.accepted,
                same_sv_ptp_clock_lease(
                    armed_lease, g_sample_clock_context.armed_ptp_clock) &&
                same_sv_ptp_clock_lease(lease, read_ptp_epoch_clock_lease()),
                no_new_publisher_fault,
                clock_timer_owned_and_enabled, auto_sync_mode)) {
            // The PRE-START clock must truly steer while SV TX is OFF.
            // The old live_tx_running() gate invalidated its servo on every
            // 1Hz observation; capture verified 120s of smpSynch=0.
            g_sample_clock_context.last_phase_observed_epoch.store(
                decision.epoch_second_continuous
                    ? observation.last_sv_epoch_generation : 0U,
                std::memory_order_release);
            g_sample_clock_context.phase_authority_valid.store(
                decision.epoch_second_continuous, std::memory_order_release);
            g_sample_clock_context.requested_slew_ticks.store(
                decision.epoch_second_continuous
                    ? decision.slew_ticks_next_epoch : 0,
                std::memory_order_release);
            if (injecting) {
                // WARM may reuse ten actual pre-START phase observations
                // only if this is the same GPTimer/PTP session. The sparse
                // canonical MAC descriptor evidence is measured AFTER START,
                // and three consecutive live epochs are still mandatory.
                // COLD retains the full ten live phase+MAC epochs.
                const bool valid_live_phase = measured_phase_qualified &&
                    decision.epoch_second_continuous;
                const bool stable_conservative = local_acquisition.observe_live(
                    valid_live_phase,
                    SvAutoPreflightPolicy::can_advertise_local(
                        injecting, source_local_qualified),
                    notified_session, current_faults, lease.generation,
                    observation.last_sv_epoch_generation);
                const bool stable_source_fast =
                    lease.role == PtpAuthorityRole::source &&
                    source_fast_acquisition.observe(
                        valid_live_phase,
                        SvAutoPreflightPolicy::can_advertise_local(
                            injecting, source_fast_mac_qualified),
                        notified_session, current_faults, lease.generation,
                        observation.last_sv_epoch_generation);
                if (lease.role != PtpAuthorityRole::source)
                    source_fast_acquisition.reset();
                const std::uint32_t warm_first =
                    g_warm_resume_first_epoch.load(std::memory_order_acquire);
                const bool warm_resume_authorized =
                    lease.role == PtpAuthorityRole::source &&
                    ar::iec61850::time_sync::SvWarmResumePolicy::retain(
                        warm_first, observation.last_sv_epoch_generation,
                        valid_live_phase,
                        same_sv_ptp_clock_lease(
                            armed_lease, g_sample_clock_context.armed_ptp_clock),
                        no_new_publisher_fault, smp_synch_source_local_qualified());
                if ((stable_source_fast || stable_conservative) && warm_first != 0U)
                    g_warm_resume_first_epoch.store(0U, std::memory_order_release);
                else if (warm_first != 0U && !warm_resume_authorized)
                    g_warm_resume_first_epoch.store(0U, std::memory_order_release);
                const bool stable_local =
                    stable_conservative || stable_source_fast ||
                    warm_resume_authorized;
                if (!stable_local &&
                    (smp_synch_source_local_qualified() ||
                     smp_synch_receiver_timeline_qualified())) {
                    ESP_LOGW(kTag,
                             "SV/PTP LOCAL REVOKED reason=phase_or_MAC_evidence "
                             "phase=%ldus uncertainty=%luus tracking=%u "
                             "MACpairs=%lu qualified=%u continuous=%u "
                             "fault_gen=%lu source_gen=%lu",
                             static_cast<long>(decision.phase_error_us),
                             static_cast<unsigned long>(
                                 decision.observation_uncertainty_us),
                             decision.software_tracking ? 1U : 0U,
                             static_cast<unsigned long>(mac_pairs),
                             source_local_qualified ? 1U : 0U,
                             decision.epoch_second_continuous ? 1U : 0U,
                             static_cast<unsigned long>(current_faults),
                             static_cast<unsigned long>(lease.generation));
                }
                // An external GM may yield LOCAL or GLOBAL only after both
                // PTP discipline and this actual SV phase/MAC gate pass.
                // SOURCE never promotes to GLOBAL through this path.
                // Publish measured phase/MAC context BEFORE a lock edge,
                // so each ring event snapshots the causally relevant
                // observation rather than last second's cached values.
                smp_synch_diag_observe(
                    decision.phase_error_us, decision.observation_uncertainty_us,
                    mac_pairs,
                    lease.role == PtpAuthorityRole::source
                        ? source_fast_acquisition.clean_epochs()
                        : local_acquisition.clean_epochs(),
                    observation.last_sv_epoch_generation,
                    decision.accepted && decision.epoch_second_continuous,
                    decision.software_tracking);
                const auto stamp_ms = static_cast<std::uint32_t>(
                    esp_timer_get_time() / 1000LL);
                smp_synch_set_source_local_qualified(
                    stable_local && lease.role == PtpAuthorityRole::source,
                    SvSyncLossReason::phase_or_mac, stamp_ms);
                smp_synch_set_receiver_timeline_qualified(
                    stable_local && lease.role == PtpAuthorityRole::receiver,
                    SvSyncLossReason::phase_or_mac, stamp_ms);
            } else {
                // M3.E: the same owned timebase keeps steering throughout
                // STOP->PARK->START, even before the publisher updates its
                // optional preflight flag. No SV packet is emitted here.
                    // M3.A: keep *clock* qualification across an SV-only STOP.
                    // No SV frame exists while TX is off. Preserve a prior
                    // verified claim only while this SAME timer, lease,
                    // source generation, fault epoch and measured phase stay
                    // healthy. A cold preflight cannot create a claim.
                    const bool healthy_dry_phase =
                        measured_phase_qualified &&
                        decision.epoch_second_continuous &&
                        no_new_publisher_fault;
                    local_acquisition.observe_preflight(
                        healthy_dry_phase,
                        notified_session, current_faults, lease.generation,
                        observation.last_sv_epoch_generation);
                    if (!healthy_dry_phase) {
                        revoke_sv_auto_claims(SvSyncLossReason::phase_or_mac);
                    } else if (lease.role == PtpAuthorityRole::source &&
                               smp_synch_source_local_qualified()) {
                        // Keep the established SOURCE certificate across
                        // arbitrarily many healthy parked PTP seconds.
                        // Preflight cannot promote an unqualified cold clock.
                        g_parked_source_clock_certified.store(
                            true, std::memory_order_release);
                    }
            }
            committed = true;
        } else if (session_matches) {
            if (smp_synch_source_local_qualified() ||
                smp_synch_receiver_timeline_qualified()) {
                ESP_LOGW(kTag,
                         "SV/PTP LOCAL REVOKED reason=phase_handoff_or_source_race "
                         "accepted=%u session=%lu fault_gen=%lu source_gen=%lu",
                         decision.accepted ? 1U : 0U,
                         static_cast<unsigned long>(notified_session),
                         static_cast<unsigned long>(current_faults),
                         static_cast<unsigned long>(lease.generation));
            }
            g_sample_clock_context.phase_authority_valid.store(
                false, std::memory_order_release);
            g_sample_clock_context.requested_slew_ticks.store(
                0, std::memory_order_relaxed);
            local_acquisition.reset();
            source_fast_acquisition.reset();
            revoke_sv_auto_claims(SvSyncLossReason::phase_handoff);
        }
        xSemaphoreGive(g_phase_timer_mutex);
        if (!committed) {
            static_cast<void>(controller.invalidate());
            local_acquisition.reset();
            source_fast_acquisition.reset();
            continue;
        }
        ++accepted_epochs;
        const bool auto_local = smp_synch_source_local_qualified() ||
            smp_synch_receiver_timeline_qualified();
        // Emit bounded ACQUISITION diagnostics at 2s intervals initially,
        // then every 5s until qualified; retain sparse 60s tracking telemetry.
        // Only this 1Hz control-plane task logs, never the 4kHz SV ISR/TX path.
        const bool early_observation = accepted_epochs <= 10U &&
            (accepted_epochs % 2U) == 0U;
        const bool still_acquiring = !auto_local &&
            (accepted_epochs % 5U) == 0U;
        const bool preflight_telemetry = !live_tx_running() &&
            (accepted_epochs % 2U) == 0U;
        if ((lease.role == PtpAuthorityRole::source ||
             lease.role == PtpAuthorityRole::receiver) &&
            (early_observation || still_acquiring ||
             preflight_telemetry || auto_local != previous_auto_local ||
             (accepted_epochs % 60U) == 0U)) {
            ESP_LOGI(kTag,
                "SV/PTP AUTO phase=%ldus uncertainty=%luus "
                "slew=%ldus/s tracking=%u MACpairs=%lu "
                "live_clean=%lu/%lu source_fast=%lu/%lu warm_phase=%lu/%lu "
                "live_age=%lu epoch=%llu LOCAL=%u",
                static_cast<long>(decision.phase_error_us),
                static_cast<unsigned long>(decision.observation_uncertainty_us),
                static_cast<long>(decision.slew_ticks_next_epoch),
                decision.software_tracking ? 1U : 0U,
                static_cast<unsigned long>(mac_pairs),
                static_cast<unsigned long>(local_acquisition.clean_epochs()),
                static_cast<unsigned long>(SvLocalRefAcquisition::kRequiredCleanEpochs),
                static_cast<unsigned long>(source_fast_acquisition.clean_epochs()),
                static_cast<unsigned long>(
                    SvSourceSelfClockAcquisition::kRequiredQualifiedLiveEpochs),
                static_cast<unsigned long>(local_acquisition.preflight_epochs()),
                static_cast<unsigned long>(SvLocalRefAcquisition::kRequiredCleanEpochs),
                static_cast<unsigned long>(local_acquisition.live_age_epochs()),
                static_cast<unsigned long long>(decision.sv_epoch_ptp_second),
                auto_local ? 1U : 0U);
        }
        previous_auto_local = auto_local;
    }
}
#endif

void telemetry_task(void*) {
    std::uint32_t previous_tick_total = 0U;
    std::int64_t previous_tick_time_us = 0;
    bool rate_baseline_valid = false;
    TimingStats baseline_stats{};
    std::uint32_t last_flight_events_reported = 0U;
#if AR_SV_TX_EGRESS_SUPPORTED
    std::uint32_t previously_reported_egress = 0U;
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // No serial logging on the real-time publisher or phase ISR.
        // Auto-surface each change in the flight recorder once per second,
        // even if the operator forgets to request Inspect SV lock. The full
        // event ring remains available on demand through Studio.
        // Do one atomic counter read per second during steady-state LOCK.
        // Constructing a 16-event snapshot is only needed after a transition.
        if (ar::esp32p4::smv::smp_synch_diag_event_count() !=
                last_flight_events_reported) {
            const auto flight = ar::esp32p4::smv::smp_synch_diag_snapshot();
            if (flight.events != last_flight_events_reported &&
                flight.recent_count > 0U &&
                flight.recent[flight.recent_count - 1U].sequence == flight.events) {
                const auto changes = flight.events - last_flight_events_reported;
                last_flight_events_reported = flight.events;
                const auto& latest = flight.recent[flight.recent_count - 1U];
                ESP_LOGW(kTag,
                    "SVSYNC FLIGHT transitions=%lu latest_seq=%lu state=%s reason=%s "
                    "uptime_ms=%lu event_phase_us=%ld event_MACpairs=%lu "
                    "phase_us=%ld uncertainty_us=%lu MACpairs=%lu clean=%lu",
                    static_cast<unsigned long>(changes),
                    static_cast<unsigned long>(latest.sequence),
                    latest.acquired ? "ACQUIRED" : "REVOKED",
                    ar::esp32p4::smv::smp_synch_diag_reason_name(latest.reason),
                    static_cast<unsigned long>(latest.monotonic_ms),
                    static_cast<long>(latest.phase_error_us),
                    static_cast<unsigned long>(latest.mac_pairs),
                    static_cast<long>(flight.phase_error_us),
                    static_cast<unsigned long>(flight.uncertainty_us),
                    static_cast<unsigned long>(flight.mac_pairs),
                    static_cast<unsigned long>(flight.clean_epochs));
            }
        }
        TimingStats snapshot{};
        portENTER_CRITICAL(&g_stats_mux);
        snapshot = g_interval_stats;
        g_interval_stats = TimingStats{};
        portEXIT_CRITICAL(&g_stats_mux);
#if AR_SV_TX_EGRESS_SUPPORTED
        SvTxEgressSummary egress_snapshot{};
        portENTER_CRITICAL(&g_tx_egress_mux);
        egress_snapshot = g_tx_egress_evidence.summary();
        portEXIT_CRITICAL(&g_tx_egress_mux);
        if (egress_snapshot.attempted != previously_reported_egress) {
            previously_reported_egress = egress_snapshot.attempted;
            ESP_LOGI(kTag,
                "SV MAC TX descriptor evidence (LAB, 1/16 smpCnt=0): "
                "attempted=%lu baseline=%lu measured=%lu invalid=%lu txfail=%lu no_hw_ts=%lu "
                "SV_discontinuity=%lu PTP_clock_step=%lu PTP_source_changed=%lu "
                "PTP-egress-phase_ns[min/mean/max]=%ld/%lld/%ld "
                "submit_max_us=%lu last_epoch=%lu last_alarm_ticks=%lu; "
                "PTP/SV absolute accuracy and smpSynch NOT QUALIFIED",
                static_cast<unsigned long>(egress_snapshot.attempted),
                static_cast<unsigned long>(egress_snapshot.baseline_established),
                static_cast<unsigned long>(egress_snapshot.measured),
                static_cast<unsigned long>(egress_snapshot.rejected_epoch),
                static_cast<unsigned long>(egress_snapshot.failed_transmit),
                static_cast<unsigned long>(egress_snapshot.missing_hw_timestamp),
                static_cast<unsigned long>(egress_snapshot.publisher_discontinuities),
                static_cast<unsigned long>(egress_snapshot.ptp_clock_discontinuities),
                static_cast<unsigned long>(egress_snapshot.ptp_source_changes),
                static_cast<long>(egress_snapshot.measured > 0U
                    ? egress_snapshot.min_phase_ns : 0),
                static_cast<long long>(egress_snapshot.measured > 0U
                    ? egress_snapshot.phase_sum_ns /
                        static_cast<std::int64_t>(egress_snapshot.measured) : 0LL),
                static_cast<long>(egress_snapshot.measured > 0U
                    ? egress_snapshot.max_phase_ns : 0),
                static_cast<unsigned long>(egress_snapshot.max_submit_duration_us),
                static_cast<unsigned long>(egress_snapshot.last_epoch_generation),
                static_cast<unsigned long>(egress_snapshot.last_alarm_ticks_low32));
        }
#endif

        const std::int64_t now_us = esp_timer_get_time();
        const std::uint32_t tick_total = g_sample_tick_total.load(std::memory_order_relaxed);
        if (!live_tx_running() || snapshot.samples == 0U) {
            rate_baseline_valid = false;
            baseline_stats = {};
            previous_tick_total = tick_total;
            previous_tick_time_us = now_us;
            continue;
        }

        // The telemetry task is independent from START and can first wake in
        // the middle of a publisher second. Use that partial bucket only as a
        // baseline, while carrying its failures into the first complete report.
        if (!rate_baseline_valid) {
            rate_baseline_valid = true;
            baseline_stats = snapshot;
            previous_tick_total = tick_total;
            previous_tick_time_us = now_us;
            continue;
        }

        const std::uint32_t elapsed_ticks = tick_total - previous_tick_total;
        const std::int64_t elapsed_us = now_us - previous_tick_time_us;
        previous_tick_total = tick_total;
        previous_tick_time_us = now_us;
        if (elapsed_ticks == 0U || elapsed_us <= 0) continue;

        accumulate_stats(snapshot, baseline_stats);
        baseline_stats = {};
        const std::uint64_t normalized_fps =
            (static_cast<std::uint64_t>(elapsed_ticks) * 1000000ULL +
             static_cast<std::uint64_t>(elapsed_us / 2)) /
            static_cast<std::uint64_t>(elapsed_us);

        const std::int64_t mean_lateness_us =
            snapshot.lateness_sum_us / static_cast<std::int64_t>(snapshot.samples);
        ESP_LOGI(kTag,
                 "timing: samples=%llu (~%llu fps) MC ok=%llu fail=%llu mirror ok=%llu fail=%llu missed=%llu wake_late_us[min/mean/max]=%ld/%lld/%ld smpCnt=%u signal_gen=%llu",
                 static_cast<unsigned long long>(snapshot.samples),
                 static_cast<unsigned long long>(normalized_fps),
                 static_cast<unsigned long long>(snapshot.canonical_ok),
                 static_cast<unsigned long long>(snapshot.canonical_fail),
                 static_cast<unsigned long long>(snapshot.mirror_ok),
                 static_cast<unsigned long long>(snapshot.mirror_fail),
                 static_cast<unsigned long long>(snapshot.missed_slots),
                 static_cast<long>(snapshot.lateness_min_us),
                 static_cast<long long>(mean_lateness_us),
                 static_cast<long>(snapshot.lateness_max_us),
                 static_cast<unsigned>(snapshot.last_sample_count),
                 static_cast<unsigned long long>(snapshot.last_signal_generation));
        if (snapshot.multi_batches != 0U) {
            ESP_LOGI(kTag,
                "MULTISV TX budget: batches=%llu max_3frame_submit=%luus "
                "over_250us=%llu canonical_ok=%llu fail=%llu",
                static_cast<unsigned long long>(snapshot.multi_batches),
                static_cast<unsigned long>(snapshot.multi_batch_max_us),
                static_cast<unsigned long long>(snapshot.multi_batch_overruns),
                static_cast<unsigned long long>(snapshot.canonical_ok),
                static_cast<unsigned long long>(snapshot.canonical_fail));
        }
    }
}

} // namespace

namespace ar::esp32p4::smv {
ar::iec61850::sampled_values::SvTxFaultSnapshot
live_tx_fault_snapshot() noexcept {
    return g_sv_tx_faults.snapshot();
}
} // namespace ar::esp32p4::smv

extern "C" void app_main(void) {
    g_link_events = xEventGroupCreate();
    if (g_link_events == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate Ethernet event group");
        return;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr));

    esp_eth_handle_t eth_handle = ar_esp32p4_eth_init();
    if (eth_handle == nullptr) {
        ESP_LOGE(kTag, "ESP32-P4 Ethernet bring-up failed");
        return;
    }
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

#if CONFIG_AR_SMV_LIVE_TX && CONFIG_AR_SMV_P2_REALTIME_50HZ
    runtime_profile_initialize();
#if AR_SV_PTP_PHASE_SUPPORTED
    g_phase_timer_mutex = xSemaphoreCreateMutex();
    if (g_phase_timer_mutex == nullptr) {
        ESP_LOGE(kTag, "PTP/SV phase observer mutex allocation failed");
        return;
    }
    if (xTaskCreatePinnedToCore(
            &sv_ptp_phase_observer_task, "ar_smv_ptp_phase", 6144, eth_handle,
            2, &g_phase_observer_task, 0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create PTP/SV phase observer on CPU0");
        return;
    }
    ESP_LOGW(kTag,
        "Experimental PTP/SV phase servo enabled: software-only evidence; "
        "physical time accuracy and smpSynch qualification NOT established");
#endif
    live_control_initialize(
        static_cast<std::int32_t>(CONFIG_AR_SMV_CURRENT_RMS_MA),
        static_cast<std::int32_t>(CONFIG_AR_SMV_VOLTAGE_RMS_MV / 10));

    if (xTaskCreatePinnedToCore(
            &live_control_task, "ar_smv_control", 7168, nullptr, 1, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create live control task on CPU0");
        return;
    }
    if (xTaskCreatePinnedToCore(
            &publisher_task, "ar_smv_publisher", 9216, eth_handle,
            configMAX_PRIORITIES - 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create realtime publisher task on CPU1");
        return;
    }
    if (xTaskCreatePinnedToCore(
            &telemetry_task, "ar_smv_telemetry", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create telemetry task on CPU0");
        return;
    }
#if AR_SV_PTP_PHASE_SUPPORTED && defined(CONFIG_AR_PTP_BOOT_SOURCE_AUTOSTART) && CONFIG_AR_PTP_BOOT_SOURCE_AUTOSTART
    if (xTaskCreatePinnedToCore(
            &sv_boot_clock_supervisor_task, "ar_smv_boot_clock", 4096,
            nullptr, 1, nullptr, 0) != pdPASS) {
        // The verified legacy manual PTP START remains available.
        ESP_LOGE(kTag, "M3B boot clock supervisor creation failed; manual PTP available");
    }
#endif
#else
    ESP_LOGW(kTag, "Live transmission disabled; Ethernet bring-up only");
#endif
}
