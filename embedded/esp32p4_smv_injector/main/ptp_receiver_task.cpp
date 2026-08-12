// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_receiver_task.hpp"

#include "smp_synch_lab.hpp"

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_discipline.hpp"
#include "ariec61850/time_sync/ptp_runtime.hpp"

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#if ESP_IDF_VERSION_MAJOR < 6
#include "esp_eth_mac_esp.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::PtpAnnounceMessage;
using ar::iec61850::time_sync::PtpBuildOptions;
using ar::iec61850::time_sync::PtpClockDiscipline;
using ar::iec61850::time_sync::PtpClockCommand;
using ar::iec61850::time_sync::PtpClockCommandKind;
using ar::iec61850::time_sync::PtpCodec;
using ar::iec61850::time_sync::PtpDisciplineOptions;
using ar::iec61850::time_sync::PtpDisciplineState;
using ar::iec61850::time_sync::PtpFrame;
using ar::iec61850::time_sync::PtpMessageType;
using ar::iec61850::time_sync::PtpPortIdentity;
using ar::iec61850::time_sync::PtpTimeReceiver;
using ar::iec61850::time_sync::PtpTimeReceiverOptions;
using ar::iec61850::time_sync::PtpTimestamp;
using ar::iec61850::time_sync::format_ptp_clock_identity;
using ar::iec61850::time_sync::ptp_clock_identity_from_mac;
using ar::iec61850::time_sync::ptp_discipline_state_name;
using ar::iec61850::time_sync::ptp_peer_delay_multicast_mac;

constexpr char kTag[] = "ar_ptp_p2";
constexpr UBaseType_t kRxQueueDepth = 24U;
constexpr TickType_t kStartupDelay = pdMS_TO_TICKS(300);

struct PtpRxEvent final {
    PtpMessageType message_type{PtpMessageType::sync};
    PtpPortIdentity source{};
    std::uint16_t sequence_id{};
    std::uint16_t flags{};
    std::int64_t correction_field{};
    std::int8_t log_message_interval{};
    bool rx_timestamp_valid{};
    PtpTimestamp rx_timestamp{};
    bool message_timestamp_valid{};
    PtpTimestamp message_timestamp{};
    bool announce_valid{};
    PtpAnnounceMessage announce{};
    bool pdelay_body_valid{};
    PtpTimestamp pdelay_body_timestamp{};
    PtpPortIdentity requesting_port_identity{};
};

static_assert(std::is_trivially_copyable_v<PtpRxEvent>);

struct ReceiverContext final {
    esp_eth_handle_t eth_handle{};
    ar_ptp_lab_config_t config{};
    std::array<std::uint8_t, 6> source_mac{};
    PtpPortIdentity local_port_identity{};
    QueueHandle_t rx_queue{};
    TaskHandle_t task_handle{};
    std::optional<PtpTimeReceiver> receiver;
    std::optional<PtpClockDiscipline> discipline;
    std::int32_t applied_frequency_ppb{};
    std::uint16_t pdelay_sequence{1U};
};

ReceiverContext g_receiver_context{};
std::atomic_bool g_receiver_running{false};
std::atomic_bool g_receiver_stop_requested{false};
std::atomic_bool g_receiver_accept_rx{false};
std::atomic<std::uint64_t> g_pdelay_requests_sent{0U};
std::atomic<std::uint64_t> g_peer_delay_frames_observed{0U};
std::atomic<std::uint64_t> g_receiver_tx_failures{0U};
portMUX_TYPE g_receiver_status_mux = portMUX_INITIALIZER_UNLOCKED;
ar_ptp_lab_status_t g_receiver_status{};

#if defined(SOC_EMAC_IEEE1588V2_SUPPORTED) && SOC_EMAC_IEEE1588V2_SUPPORTED && ESP_IDF_VERSION_MAJOR < 6

[[nodiscard]] bool valid_hw_timestamp(const eth_mac_time_t& timestamp) noexcept {
    return timestamp.nanoseconds < 1'000'000'000U &&
           (timestamp.seconds != 0U || timestamp.nanoseconds != 0U);
}

[[nodiscard]] PtpTimestamp to_ptp_timestamp(const eth_mac_time_t& timestamp) noexcept {
    return {static_cast<std::uint64_t>(timestamp.seconds), timestamp.nanoseconds};
}

[[nodiscard]] bool enable_hardware_ptp(const esp_eth_handle_t handle) noexcept {
    bool enable = true;
    const auto result = esp_eth_ioctl(
        handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_PTP_ENABLE),
        &enable);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Unable to enable IEEE1588 hardware timestamping: %s",
                 esp_err_to_name(result));
        return false;
    }
    return true;
}

void seed_hardware_clock(const esp_eth_handle_t handle) noexcept {
    eth_mac_time_t current{};
    if (esp_eth_ioctl(
            handle,
            static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_G_PTP_TIME),
            &current) == ESP_OK &&
        valid_hw_timestamp(current)) {
        return;
    }

    eth_mac_time_t initial{};
    const std::time_t system_time = std::time(nullptr);
    if (system_time > 0 &&
        static_cast<std::uint64_t>(system_time) <=
            std::numeric_limits<std::uint32_t>::max()) {
        initial.seconds = static_cast<std::uint32_t>(system_time);
    }
    static_cast<void>(esp_eth_ioctl(
        handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_S_PTP_TIME),
        &initial));
}

[[nodiscard]] bool transmit_with_hw_timestamp(
    const esp_eth_handle_t handle,
    std::vector<std::uint8_t>& frame,
    PtpTimestamp& timestamp) noexcept {
    if (frame.empty()) return false;
    eth_mac_time_t tx_timestamp{};
    const auto result = esp_eth_transmit_ctrl_vargs(
        handle,
        &tx_timestamp,
        2U,
        frame.data(),
        frame.size());
    if (result != ESP_OK || !valid_hw_timestamp(tx_timestamp)) return false;
    timestamp = to_ptp_timestamp(tx_timestamp);
    return true;
}

[[nodiscard]] bool apply_phase_step(
    const esp_eth_handle_t handle,
    const std::int64_t delta_ns) noexcept {
    eth_mac_time_t current{};
    if (esp_eth_ioctl(
            handle,
            static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_G_PTP_TIME),
            &current) != ESP_OK ||
        current.nanoseconds >= 1'000'000'000U) {
        return false;
    }

    const std::int64_t current_ns =
        static_cast<std::int64_t>(current.seconds) * 1'000'000'000LL +
        static_cast<std::int64_t>(current.nanoseconds);
    if ((delta_ns > 0 &&
         current_ns > std::numeric_limits<std::int64_t>::max() - delta_ns) ||
        (delta_ns < 0 &&
         current_ns < std::numeric_limits<std::int64_t>::min() - delta_ns)) {
        return false;
    }
    const std::int64_t adjusted_ns = current_ns + delta_ns;
    if (adjusted_ns < 0) return false;

    const auto seconds = adjusted_ns / 1'000'000'000LL;
    const auto nanoseconds = adjusted_ns % 1'000'000'000LL;
    if (seconds > static_cast<std::int64_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    eth_mac_time_t adjusted{};
    adjusted.seconds = static_cast<std::uint32_t>(seconds);
    adjusted.nanoseconds = static_cast<std::uint32_t>(nanoseconds);
    return esp_eth_ioctl(
               handle,
               static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_S_PTP_TIME),
               &adjusted) == ESP_OK;
}

[[nodiscard]] bool apply_frequency_adjustment(
    const esp_eth_handle_t handle,
    const std::int32_t desired_ppb,
    std::int32_t& applied_ppb) noexcept {
    if (desired_ppb == applied_ppb) return true;

    constexpr double kPpb = 1'000'000'000.0;
    const double desired_scale = 1.0 + static_cast<double>(desired_ppb) / kPpb;
    const double applied_scale = 1.0 + static_cast<double>(applied_ppb) / kPpb;
    if (desired_scale <= 0.0 || applied_scale <= 0.0) return false;
    double relative_scale = desired_scale / applied_scale;
    if (!std::isfinite(relative_scale) || relative_scale <= 0.0) return false;

    const auto result = esp_eth_ioctl(
        handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_ADJ_PTP_FREQ),
        &relative_scale);
    if (result != ESP_OK) return false;
    applied_ppb = desired_ppb;
    return true;
}

[[nodiscard]] bool apply_clock_command(
    ReceiverContext& context,
    const PtpClockCommand& command) noexcept {
    switch (command.kind) {
    case PtpClockCommandKind::none: return true;
    case PtpClockCommandKind::step_phase:
        return apply_phase_step(context.eth_handle, command.phase_step_ns);
    case PtpClockCommandKind::set_frequency:
        return apply_frequency_adjustment(
            context.eth_handle,
            command.frequency_adjustment_ppb,
            context.applied_frequency_ppb);
    }
    return false;
}

[[nodiscard]] bool parse_pdelay_body(
    const PtpFrame& frame,
    PtpTimestamp& timestamp,
    PtpPortIdentity& requester) noexcept {
    if (frame.body.size() < 20U ||
        !PtpTimestamp::try_read(
            std::span<const std::uint8_t>{frame.body}.first(10U),
            timestamp)) {
        return false;
    }
    std::copy_n(frame.body.begin() + 10, 8, requester.clock_identity.begin());
    requester.port_number = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frame.body[18]) << 8U) |
        static_cast<std::uint16_t>(frame.body[19]));
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> make_pdelay_body(
    const PtpTimestamp& timestamp,
    const PtpPortIdentity& requester) {
    std::vector<std::uint8_t> body(20U, 0U);
    if (!timestamp.write(std::span<std::uint8_t>{body}.first(10U))) return {};
    std::copy(
        requester.clock_identity.begin(),
        requester.clock_identity.end(),
        body.begin() + 10);
    body[18] = static_cast<std::uint8_t>((requester.port_number >> 8U) & 0xFFU);
    body[19] = static_cast<std::uint8_t>(requester.port_number & 0xFFU);
    return body;
}

[[nodiscard]] constexpr bool is_peer_delay_message(
    const PtpMessageType type) noexcept {
    return type == PtpMessageType::pdelay_req ||
           type == PtpMessageType::pdelay_resp ||
           type == PtpMessageType::pdelay_resp_follow_up;
}

[[nodiscard]] bool vlan_profile_matches(
    const ar_ptp_lab_config_t& config,
    const PtpFrame& frame) noexcept {
    // P2 currently configures either untagged or one 802.1Q VLAN. Do not let
    // untagged, wrong-VLAN, or QinQ traffic contribute timing evidence for a
    // different configured profile.
    if (frame.outer_vlan_id.has_value()) return false;
    if (config.vlan_enabled) {
        return frame.vlan_id.has_value() && *frame.vlan_id == config.vlan_id;
    }
    return !frame.vlan_id.has_value();
}

[[nodiscard]] bool should_queue(
    const ar_ptp_role_t role,
    const PtpMessageType type) noexcept {
    if (role == AR_PTP_ROLE_MONITOR) {
        // Peer-delay frames are counted raw before this queue decision. Do not
        // feed them into exchange correlation in passive MONITOR, otherwise a
        // response with no owned request would be mislabeled as rejected.
        return type == PtpMessageType::announce ||
               type == PtpMessageType::sync ||
               type == PtpMessageType::follow_up;
    }
    return role == AR_PTP_ROLE_TIME_RECEIVER &&
           (type == PtpMessageType::announce ||
            type == PtpMessageType::sync ||
            type == PtpMessageType::follow_up ||
            type == PtpMessageType::pdelay_resp ||
            type == PtpMessageType::pdelay_resp_follow_up);
}

esp_err_t receiver_input_info(
    esp_eth_handle_t,
    std::uint8_t* buffer,
    const std::uint32_t length,
    void* priv,
    void* info) {
    auto* context = static_cast<ReceiverContext*>(priv);
    if (buffer == nullptr) return ESP_OK;

    if (g_receiver_accept_rx.load(std::memory_order_acquire) &&
        context != nullptr &&
        context->rx_queue != nullptr) {
        PtpFrame frame;
        if (PtpCodec::try_parse_ethernet_frame(
                std::span<const std::uint8_t>{buffer, length}, frame) &&
            frame.header.domain_number == context->config.domain_number &&
            frame.header.transport_specific == context->config.transport_specific &&
            vlan_profile_matches(context->config, frame)) {
            // Raw passive observability is independent of owning/correlating a
            // peer-delay exchange. Count matching-profile Pdelay traffic before
            // role-specific queue filtering so MONITOR sees Req/Resp/RespFU.
            if (is_peer_delay_message(frame.header.message_type)) {
                g_peer_delay_frames_observed.fetch_add(1U, std::memory_order_relaxed);
            }

            if (should_queue(context->config.role, frame.header.message_type)) {
                PtpRxEvent event;
                event.message_type = frame.header.message_type;
                event.source = frame.header.source_port_identity;
                event.sequence_id = frame.header.sequence_id;
                event.flags = frame.header.flags;
                event.correction_field = frame.header.correction_field;
                event.log_message_interval = frame.header.log_message_interval;
                if (info != nullptr) {
                    const auto& rx = *static_cast<const eth_mac_time_t*>(info);
                    if (valid_hw_timestamp(rx)) {
                        event.rx_timestamp_valid = true;
                        event.rx_timestamp = to_ptp_timestamp(rx);
                    }
                }
                if (frame.timestamp.has_value()) {
                    event.message_timestamp_valid = true;
                    event.message_timestamp = *frame.timestamp;
                }
                if (frame.announce.has_value()) {
                    event.announce_valid = true;
                    event.announce = *frame.announce;
                }
                if (frame.header.message_type == PtpMessageType::pdelay_resp ||
                    frame.header.message_type == PtpMessageType::pdelay_resp_follow_up) {
                    event.pdelay_body_valid = parse_pdelay_body(
                        frame,
                        event.pdelay_body_timestamp,
                        event.requesting_port_identity);
                }
                if (xQueueSend(context->rx_queue, &event, 0U) == pdTRUE &&
                    context->task_handle != nullptr) {
                    xTaskNotifyGive(context->task_handle);
                }
            }
        }
    }

    std::free(buffer);
    return ESP_OK;
}

[[nodiscard]] PtpFrame frame_from_event(
    const ReceiverContext& context,
    const PtpRxEvent& event) {
    PtpFrame frame;
    frame.header.transport_specific = context.config.transport_specific;
    frame.header.domain_number = context.config.domain_number;
    frame.header.message_type = event.message_type;
    frame.header.source_port_identity = event.source;
    frame.header.sequence_id = event.sequence_id;
    frame.header.flags = event.flags;
    frame.header.correction_field = event.correction_field;
    frame.header.log_message_interval = event.log_message_interval;
    if (event.message_timestamp_valid) frame.timestamp = event.message_timestamp;
    if (event.announce_valid) frame.announce = event.announce;
    if (event.pdelay_body_valid) {
        frame.body = make_pdelay_body(
            event.pdelay_body_timestamp,
            event.requesting_port_identity);
    }
    return frame;
}

[[nodiscard]] PtpDisciplineOptions discipline_options(
    const ar_ptp_lab_config_t& config) noexcept {
    PtpDisciplineOptions options;
    options.maximum_path_delay_ns = config.maximum_path_delay_ns;
    options.maximum_path_delay_jitter_ns = config.maximum_path_delay_jitter_ns;
    options.lock_offset_threshold_ns = config.lock_offset_threshold_ns;
    options.unlock_offset_threshold_ns = config.unlock_offset_threshold_ns;
    options.phase_step_threshold_ns = config.phase_step_threshold_ns;
    options.lock_required_samples = config.lock_required_samples;
    options.maximum_frequency_adjustment_ppb = config.maximum_frequency_adjustment_ppb;
    options.sync_timeout = std::chrono::milliseconds{config.sync_timeout_ms};
    options.holdover_timeout = std::chrono::milliseconds{config.holdover_timeout_ms};
    return options;
}

[[nodiscard]] std::int8_t pdelay_log_interval(const std::uint32_t interval_ms) noexcept {
    std::uint32_t value = 1000U;
    std::int8_t result = 0;
    if (interval_ms >= value) {
        while (result < 7 && value <= 5'000U && value * 2U <= interval_ms) {
            value *= 2U;
            ++result;
        }
    } else {
        while (result > -7 && value > 8U && value / 2U >= interval_ms) {
            value /= 2U;
            --result;
        }
    }
    return result;
}

[[nodiscard]] bool send_pdelay_request(ReceiverContext& context) {
    if (!context.receiver.has_value() ||
        !context.receiver->status().selected_source.has_value()) {
        return false;
    }

    PtpBuildOptions options;
    options.transport_specific = context.config.transport_specific;
    options.domain_number = context.config.domain_number;
    options.source_port_identity = context.local_port_identity;
    options.sequence_id = context.pdelay_sequence++;
    options.log_message_interval = pdelay_log_interval(
        context.config.pdelay_request_interval_ms);
    options.timestamp = {};
    const auto message = PtpCodec::build_pdelay_req(options);
    auto frame = PtpCodec::build_ethernet_frame(
        ptp_peer_delay_multicast_mac,
        context.source_mac,
        message,
        context.config.vlan_enabled
            ? std::optional<std::uint16_t>{context.config.vlan_id}
            : std::nullopt,
        context.config.vlan_enabled ? context.config.vlan_priority : 0U);

    PtpTimestamp t1{};
    if (!transmit_with_hw_timestamp(context.eth_handle, frame, t1)) {
        g_receiver_tx_failures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    context.receiver->note_pdelay_request(options.sequence_id, t1);
    g_pdelay_requests_sent.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

void publish_measured_sync(const ReceiverContext& context) noexcept {
    if (context.config.role != AR_PTP_ROLE_TIME_RECEIVER ||
        !context.discipline.has_value()) {
        smp_synch_lab_set_measured(std::nullopt);
        return;
    }
    smp_synch_lab_set_measured(context.discipline->measured_smp_synch());
}

void update_status(ReceiverContext& context) noexcept {
    ar_ptp_lab_status_t snapshot{};
    snapshot.is_running = g_receiver_running.load(std::memory_order_acquire);
    snapshot.role = context.config.role;
    snapshot.peer_delay_requests_sent =
        g_pdelay_requests_sent.load(std::memory_order_relaxed);
    snapshot.peer_delay_frames_observed =
        g_peer_delay_frames_observed.load(std::memory_order_relaxed);
    snapshot.tx_failure_count =
        g_receiver_tx_failures.load(std::memory_order_relaxed);

    std::uint64_t receiver_rejected_exchanges = 0U;
    if (context.receiver.has_value()) {
        const auto& receiver = context.receiver->status();
        snapshot.announce_received = receiver.announce_frames;
        snapshot.sync_received = receiver.sync_frames;
        snapshot.follow_up_received = receiver.follow_up_frames;
        snapshot.peer_delay_responses_received = receiver.pdelay_responses;
        receiver_rejected_exchanges = receiver.rejected_exchanges;
        if (receiver.selected_source.has_value()) {
            snapshot.source_selected = true;
            std::copy(
                receiver.selected_source->clock_identity.begin(),
                receiver.selected_source->clock_identity.end(),
                std::begin(snapshot.selected_source_clock_identity));
            snapshot.selected_source_port_number =
                receiver.selected_source->port_number;
        }
        // The receiver owns path-delay evidence lifetime. If it has expired,
        // leave validity false instead of resurrecting the discipline's cached
        // value from the last accepted offset measurement.
        if (receiver.mean_path_delay_ns.has_value()) {
            snapshot.mean_path_delay_valid = true;
            snapshot.mean_path_delay_ns = *receiver.mean_path_delay_ns;
        }
    }

    snapshot.rejected_discipline_samples = receiver_rejected_exchanges;
    if (context.discipline.has_value()) {
        const auto& discipline = context.discipline->status();
        snapshot.discipline_state = static_cast<ar_ptp_discipline_state_t>(discipline.state);
        if (discipline.offset_from_master_ns.has_value()) {
            snapshot.offset_valid = true;
            snapshot.offset_from_master_ns = *discipline.offset_from_master_ns;
        }
        // Do not set mean_path_delay_valid from discipline cache; validity is
        // authoritative only while PtpTimeReceiver still owns fresh Pdelay evidence.
        if (snapshot.mean_path_delay_valid && discipline.path_delay_jitter_ns.has_value()) {
            snapshot.path_delay_jitter_valid = true;
            snapshot.path_delay_jitter_ns = *discipline.path_delay_jitter_ns;
        }
        snapshot.frequency_adjustment_ppb = discipline.frequency_adjustment_ppb;
        snapshot.accepted_discipline_samples = discipline.accepted_samples;
        if (std::numeric_limits<std::uint64_t>::max() -
                snapshot.rejected_discipline_samples < discipline.rejected_samples) {
            snapshot.rejected_discipline_samples =
                std::numeric_limits<std::uint64_t>::max();
        } else {
            snapshot.rejected_discipline_samples += discipline.rejected_samples;
        }
        snapshot.globally_traceable = discipline.globally_traceable;
        const auto measured = context.discipline->measured_smp_synch();
        if (measured.has_value()) {
            snapshot.measured_smp_synch_valid = true;
            snapshot.measured_smp_synch = static_cast<std::uint8_t>(*measured);
        }
    } else {
        snapshot.discipline_state = AR_PTP_DISCIPLINE_UNLOCKED;
    }

    portENTER_CRITICAL(&g_receiver_status_mux);
    g_receiver_status = snapshot;
    portEXIT_CRITICAL(&g_receiver_status_mux);
}

void reset_discipline_for_new_source(ReceiverContext& context) noexcept {
    if (!context.discipline.has_value()) return;
    if (!apply_frequency_adjustment(
            context.eth_handle, 0, context.applied_frequency_ppb)) {
        context.discipline->record_actuation_failure();
        return;
    }
    context.discipline->reset();
    smp_synch_lab_set_measured(std::nullopt);
}

void process_rx_event(ReceiverContext& context, const PtpRxEvent& event) {
    if (!context.receiver.has_value()) return;
    auto frame = frame_from_event(context, event);
    const auto now = std::chrono::steady_clock::now();

    switch (event.message_type) {
    case PtpMessageType::announce: {
        const bool was_globally_traceable =
            context.receiver->status().selected_source_globally_traceable;
        const bool changed = context.receiver->observe_announce(frame, now);
        const bool is_globally_traceable =
            context.receiver->status().selected_source_globally_traceable;

        if (changed && context.config.role == AR_PTP_ROLE_TIME_RECEIVER) {
            reset_discipline_for_new_source(context);
            const auto source = context.receiver->status().selected_source;
            if (source.has_value()) {
                const auto name = format_ptp_clock_identity(source->clock_identity);
                ESP_LOGI(kTag, "Selected PTP source %s/%u traceable=%s",
                         name.c_str(),
                         static_cast<unsigned>(source->port_number),
                         is_globally_traceable ? "yes" : "no");
            }
        } else if (context.config.role == AR_PTP_ROLE_TIME_RECEIVER &&
                   context.discipline.has_value() &&
                   was_globally_traceable &&
                   !is_globally_traceable) {
            context.discipline->revoke_global_traceability();
            ESP_LOGW(kTag,
                     "Selected PTP source revoked global traceability; measured sync demoted to local");
        }
        break;
    }
    case PtpMessageType::pdelay_resp:
        if (event.rx_timestamp_valid) {
            static_cast<void>(context.receiver->observe_pdelay_response(
                frame, event.rx_timestamp, now));
        }
        break;
    case PtpMessageType::pdelay_resp_follow_up:
        static_cast<void>(context.receiver->observe_pdelay_response_follow_up(frame, now));
        break;
    case PtpMessageType::sync:
        if (event.rx_timestamp_valid) {
            const auto measurement = context.receiver->observe_sync(
                frame, event.rx_timestamp, now);
            if (measurement.has_value() && context.discipline.has_value()) {
                const auto command = context.discipline->observe(*measurement, now);
                if (!apply_clock_command(context, command)) {
                    context.discipline->record_actuation_failure();
                }
            }
        }
        break;
    case PtpMessageType::follow_up: {
        const auto measurement = context.receiver->observe_follow_up(frame, now);
        if (measurement.has_value() && context.discipline.has_value()) {
            const auto previous_state = context.discipline->status().state;
            const auto command = context.discipline->observe(*measurement, now);
            if (!apply_clock_command(context, command)) {
                context.discipline->record_actuation_failure();
            }
            const auto state = context.discipline->status().state;
            if (state != previous_state) {
                ESP_LOGI(kTag,
                         "Discipline %s -> %s offset=%lld ns path=%lld ns freq=%ld ppb",
                         ptp_discipline_state_name(previous_state),
                         ptp_discipline_state_name(state),
                         static_cast<long long>(measurement->offset_from_master_ns),
                         static_cast<long long>(measurement->mean_path_delay_ns),
                         static_cast<long>(context.discipline->status().frequency_adjustment_ppb));
            }
        }
        break;
    }
    default: break;
    }
    publish_measured_sync(context);
}

void finish_receiver(ReceiverContext& context) noexcept {
    g_receiver_accept_rx.store(false, std::memory_order_release);
    if (context.applied_frequency_ppb != 0) {
        static_cast<void>(apply_frequency_adjustment(
            context.eth_handle, 0, context.applied_frequency_ppb));
    }
    smp_synch_lab_set_measured(std::nullopt);
    context.receiver.reset();
    context.discipline.reset();
    context.task_handle = nullptr;
    g_receiver_running.store(false, std::memory_order_release);
    update_status(context);
}

void receiver_task(void* argument) {
    auto& context = *static_cast<ReceiverContext*>(argument);
    vTaskDelay(kStartupDelay);

    if (!enable_hardware_ptp(context.eth_handle)) {
        finish_receiver(context);
        vTaskDelete(nullptr);
        return;
    }
    if (context.config.role == AR_PTP_ROLE_TIME_RECEIVER) {
        seed_hardware_clock(context.eth_handle);
    }

    const auto mac_result = esp_eth_ioctl(
        context.eth_handle,
        ETH_CMD_G_MAC_ADDR,
        context.source_mac.data());
    if (mac_result != ESP_OK) {
        ESP_LOGE(kTag, "Unable to read Ethernet MAC: %s", esp_err_to_name(mac_result));
        finish_receiver(context);
        vTaskDelete(nullptr);
        return;
    }

    auto clock_identity = ptp_clock_identity_from_mac(context.source_mac);
    if (context.config.clock_identity_override) {
        std::copy(
            std::begin(context.config.clock_identity),
            std::end(context.config.clock_identity),
            clock_identity.begin());
    }
    context.local_port_identity = {clock_identity, context.config.port_number};

    const auto interval = std::chrono::milliseconds{
        context.config.pdelay_request_interval_ms};
    const auto exchange_timeout = std::max(
        std::chrono::milliseconds{50}, interval * 3 / 4);
    context.receiver.emplace(PtpTimeReceiverOptions{
        context.config.domain_number,
        context.config.transport_specific,
        context.local_port_identity,
        std::chrono::milliseconds{3000},
        exchange_timeout,
    });
    if (context.config.role == AR_PTP_ROLE_TIME_RECEIVER) {
        context.discipline.emplace(discipline_options(context.config));
    }

    const auto input_result = esp_eth_update_input_path_info(
        context.eth_handle, &receiver_input_info, &context);
    if (input_result != ESP_OK) {
        ESP_LOGE(kTag, "Hardware RX timestamp path unavailable: %s",
                 esp_err_to_name(input_result));
        finish_receiver(context);
        vTaskDelete(nullptr);
        return;
    }
    g_receiver_accept_rx.store(true, std::memory_order_release);

    const auto local_name = format_ptp_clock_identity(clock_identity);
    ESP_LOGW(kTag,
             "%s active: external PTP evidence only; not a GPS/certified-grandmaster claim",
             context.config.role == AR_PTP_ROLE_TIME_RECEIVER
                 ? "PTP TIME_RECEIVER"
                 : "PTP MONITOR");
    ESP_LOGI(kTag,
             "domain=%u transportSpecific=0x%X local=%s/%u pdelay=%lu ms",
             static_cast<unsigned>(context.config.domain_number),
             static_cast<unsigned>(context.config.transport_specific),
             local_name.c_str(),
             static_cast<unsigned>(context.config.port_number),
             static_cast<unsigned long>(context.config.pdelay_request_interval_ms));

    auto next_pdelay = std::chrono::steady_clock::now();
    while (!g_receiver_stop_requested.load(std::memory_order_acquire)) {
        PtpRxEvent event;
        while (context.rx_queue != nullptr &&
               xQueueReceive(context.rx_queue, &event, 0U) == pdTRUE) {
            process_rx_event(context, event);
        }

        const auto now = std::chrono::steady_clock::now();
        const bool source_dropped = context.receiver->tick(now);
        if (source_dropped) {
            ESP_LOGW(kTag, "Selected PTP source timed out");
            if (context.discipline.has_value()) {
                context.discipline->revoke_global_traceability();
            }
        }
        if (context.discipline.has_value()) {
            const auto previous_state = context.discipline->status().state;
            context.discipline->tick(now);
            if (context.discipline->status().state != previous_state) {
                ESP_LOGW(kTag,
                         "Discipline timeout %s -> %s",
                         ptp_discipline_state_name(previous_state),
                         ptp_discipline_state_name(context.discipline->status().state));
            }
            publish_measured_sync(context);
        }

        if (context.config.role == AR_PTP_ROLE_TIME_RECEIVER &&
            context.receiver->status().selected_source.has_value() &&
            now >= next_pdelay) {
            static_cast<void>(send_pdelay_request(context));
            next_pdelay = now + interval;
        }

        update_status(context);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    }

    finish_receiver(context);
    vTaskDelete(nullptr);
}

#else

void receiver_task(void*) {
    ESP_LOGE(kTag,
             "PTP-P2 receiver requires ESP32-P4 IEEE1588 support and ESP-IDF 5.x");
    smp_synch_lab_set_measured(std::nullopt);
    g_receiver_running.store(false, std::memory_order_release);
    g_receiver_context.task_handle = nullptr;
    vTaskDelete(nullptr);
}

#endif

} // namespace

bool ptp_receiver_start(
    const esp_eth_handle_t eth_handle,
    const ar_ptp_lab_config_t& config) noexcept {
    if (eth_handle == nullptr ||
        (config.role != AR_PTP_ROLE_TIME_RECEIVER &&
         config.role != AR_PTP_ROLE_MONITOR) ||
        g_receiver_running.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }

    g_receiver_stop_requested.store(false, std::memory_order_release);
    g_receiver_accept_rx.store(false, std::memory_order_release);
    g_pdelay_requests_sent.store(0U, std::memory_order_release);
    g_peer_delay_frames_observed.store(0U, std::memory_order_release);
    g_receiver_tx_failures.store(0U, std::memory_order_release);
    g_receiver_context.eth_handle = eth_handle;
    g_receiver_context.config = config;
    g_receiver_context.task_handle = nullptr;
    g_receiver_context.receiver.reset();
    g_receiver_context.discipline.reset();
    g_receiver_context.applied_frequency_ppb = 0;
    g_receiver_context.pdelay_sequence = 1U;

    if (g_receiver_context.rx_queue == nullptr) {
        g_receiver_context.rx_queue = xQueueCreate(kRxQueueDepth, sizeof(PtpRxEvent));
    } else {
        xQueueReset(g_receiver_context.rx_queue);
    }
    if (g_receiver_context.rx_queue == nullptr) {
        g_receiver_running.store(false, std::memory_order_release);
        return false;
    }

    if (xTaskCreatePinnedToCore(
            &receiver_task,
            config.role == AR_PTP_ROLE_TIME_RECEIVER ? "ar_ptp_rx" : "ar_ptp_mon",
            8192,
            &g_receiver_context,
            4,
            &g_receiver_context.task_handle,
            0) != pdPASS) {
        g_receiver_context.task_handle = nullptr;
        g_receiver_running.store(false, std::memory_order_release);
        return false;
    }
    update_status(g_receiver_context);
    return true;
}

void ptp_receiver_stop() noexcept {
    if (!g_receiver_running.load(std::memory_order_acquire)) return;
    g_receiver_stop_requested.store(true, std::memory_order_release);
    if (g_receiver_context.task_handle != nullptr) {
        xTaskNotifyGive(g_receiver_context.task_handle);
    }
}

bool ptp_receiver_is_running() noexcept {
    return g_receiver_running.load(std::memory_order_acquire);
}

bool ptp_receiver_get_status(ar_ptp_lab_status_t& status) noexcept {
    portENTER_CRITICAL(&g_receiver_status_mux);
    status = g_receiver_status;
    portEXIT_CRITICAL(&g_receiver_status_mux);
    status.is_running = g_receiver_running.load(std::memory_order_acquire);
    return true;
}

} // namespace ar::esp32p4::smv
