// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/rcb_contention.hpp"
#include "ariec61850/mms/rcb_failover.hpp"
#include "ariec61850/mms/rcb_selection.hpp"
#include "ariec61850/mms/report_subscription_runtime.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsStaticReportSessionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MmsStaticReportSessionOptions final {
    MmsRcbStaticSelectionOptions selection;
    MmsRcbContentionProbeOptions contention;
    std::size_t maximum_candidate_attempts{4U};
    MmsReportSubscriptionOptions subscription;
};

struct MmsStaticReportSessionSnapshot final {
    bool prepared{};
    bool active{};
    std::string selected_rcb_reference;
    std::string data_set_reference;
    std::size_t data_set_member_count{};
    MmsRcbPreclaimFailoverSnapshot failover;
    std::vector<std::string> blockers;
    std::optional<MmsReportSubscriptionSnapshot> subscription;
};

// Client-side static DataSet reporting orchestration. prepare() performs only
// bounded Read operations. start() never rewrites DatSet and delegates guarded
// reservation/enable/report/cleanup behavior to MmsReportSubscriptionRuntime.
class MmsStaticReportSessionRuntime final {
public:
    MmsStaticReportSessionRuntime(
        MmsAssociationRuntime& association,
        const MmsLiveDiscoveryResult& discovery,
        MmsStaticReportSessionOptions options = {});

    void prepare(std::stop_token stop_token = {});
    void start(std::stop_token stop_token = {});
    void stop(std::stop_token stop_token = {}) noexcept;
    [[nodiscard]] bool poll_once(std::stop_token stop_token = {});
    [[nodiscard]] std::size_t drain_queued_reports();

    [[nodiscard]] MmsStaticReportSessionSnapshot snapshot() const;
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] bool active() const noexcept {
        return subscription_ != nullptr && subscription_->active();
    }
    [[nodiscard]] const MmsReportControlCandidate* selected_candidate() const noexcept {
        return selected_candidate_ ? &*selected_candidate_ : nullptr;
    }

private:
    void resolve_selected_static_binding();

    MmsAssociationRuntime& association_;
    const MmsLiveDiscoveryResult& discovery_;
    MmsStaticReportSessionOptions options_;
    MmsRcbPreclaimFailoverTracker failover_;
    std::optional<MmsReportControlCandidate> selected_candidate_;
    std::optional<MmsDataSetDirectoryResponse> selected_directory_;
    std::string selected_data_set_reference_;
    std::vector<std::string> blockers_;
    std::unique_ptr<MmsReportSubscriptionRuntime> subscription_;
    bool prepared_{};
};

} // namespace ar::iec61850::mms
