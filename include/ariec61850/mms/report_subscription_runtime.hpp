// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsReportSubscriptionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MmsReportSubscriptionState : std::uint8_t {
    idle,
    probing,
    reserving,
    configuring,
    enabling,
    active,
    stopping,
    stopped,
    cleanup_required,
    faulted,
};

enum class MmsReportSubscriptionEventKind : std::uint8_t {
    state_changed,
    probe_completed,
    reservation_acquired,
    attribute_written,
    enabled,
    general_interrogation_sent,
    report_received,
    report_decode_failed,
    disabled,
    reservation_released,
    cleanup_deferred,
    faulted,
};

struct MmsReportSubscriptionEvent final {
    MmsReportSubscriptionEventKind kind{MmsReportSubscriptionEventKind::state_changed};
    MmsReportSubscriptionState state{MmsReportSubscriptionState::idle};
    std::string message;
};

struct MmsReportSubscriptionOptions final {
    bool trigger_general_interrogation{true};
    bool reserve_unbuffered_rcb{true};
    bool write_data_set_reference{false};
    bool write_trigger_options{false};
    bool write_optional_fields{false};
    bool allow_reenable_caller_owned{false};
    std::vector<std::uint8_t> trigger_options;
    std::vector<std::uint8_t> optional_fields;
    std::string data_set_reference;
    std::size_t maximum_events{1'024U};
    MmsOfflineReportMonitorOptions monitor_options{};
};

struct MmsReportSubscriptionSnapshot final {
    MmsReportSubscriptionState state{MmsReportSubscriptionState::idle};
    bool enabled_by_runtime{};
    bool reservation_touched{};
    bool cleanup_required{};
    std::uint64_t received_reports{};
    std::uint64_t decode_failures{};
    std::optional<MmsReportControlState> last_rcb_state;
    std::vector<MmsOfflineReportStreamSnapshot> streams;
    std::vector<MmsReportSubscriptionEvent> events;
};

class MmsReportSubscriptionRuntime final {
public:
    MmsReportSubscriptionRuntime(
        MmsAssociationRuntime& association,
        MmsReportControlCandidate candidate,
        MmsDataSetDirectoryResponse directory,
        MmsReportSubscriptionOptions options = {});

    void start(std::stop_token stop_token = {});
    void stop(std::stop_token stop_token = {}) noexcept;
    [[nodiscard]] bool poll_once(std::stop_token stop_token = {});
    void run(std::stop_token stop_token);
    [[nodiscard]] std::size_t drain_queued_reports();
    [[nodiscard]] bool retry_cleanup(std::stop_token stop_token = {}) noexcept;

    [[nodiscard]] MmsReportSubscriptionSnapshot snapshot() const;
    [[nodiscard]] MmsReportSubscriptionState state() const noexcept { return state_; }
    [[nodiscard]] bool active() const noexcept {
        return state_ == MmsReportSubscriptionState::active;
    }

private:
    [[nodiscard]] std::vector<std::string> probe_attributes() const;
    [[nodiscard]] MmsReportControlState probe(std::stop_token stop_token);
    void write_attribute(
        const std::string& attribute,
        MmsDataValue value,
        std::stop_token stop_token);
    [[nodiscard]] bool try_write_attribute_noexcept(
        const std::string& attribute,
        MmsDataValue value,
        std::stop_token stop_token) noexcept;
    void ingest_report(std::span<const std::uint8_t> payload);
    void set_state(MmsReportSubscriptionState state, std::string message);
    void add_event(MmsReportSubscriptionEventKind kind, std::string message);
    void fail(std::string message);

    MmsAssociationRuntime& association_;
    MmsReportControlCandidate candidate_;
    MmsDataSetDirectoryResponse directory_;
    MmsReportSubscriptionOptions options_;
    MmsReportSubscriptionState state_{MmsReportSubscriptionState::idle};
    MmsOfflineReportMonitor monitor_;
    std::vector<MmsReportSubscriptionEvent> events_;
    std::optional<MmsReportControlState> last_rcb_state_;
    bool enabled_by_runtime_{};
    bool reservation_touched_{};
    bool cleanup_required_{};
    std::uint64_t received_reports_{};
    std::uint64_t decode_failures_{};
};

} // namespace ar::iec61850::mms
