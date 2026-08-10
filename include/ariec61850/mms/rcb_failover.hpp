// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::mms {

struct MmsRcbContentionProbeResult;

enum class MmsRcbPreclaimOutcome {
    stable_proceed,
    skipped_contended,
    attempts_exhausted,
};

[[nodiscard]] std::string_view mms_rcb_preclaim_outcome_name(
    MmsRcbPreclaimOutcome value) noexcept;

struct MmsRcbPreclaimAttemptEvidence final {
    std::size_t attempt_number{};
    std::string rcb_reference;
    MmsRcbPreclaimOutcome outcome{MmsRcbPreclaimOutcome::skipped_contended};
    bool busy{};
    bool flapping{};
    int cooldown_seconds{};
    std::string reason;
};

struct MmsRcbPreclaimFailoverSnapshot final {
    std::size_t maximum_candidate_attempts{};
    std::vector<MmsRcbPreclaimAttemptEvidence> attempts;
    std::vector<std::string> excluded_rcb_references;
    std::string selected_rcb_reference;
    bool exhausted{};

    [[nodiscard]] bool selected() const noexcept {
        return !selected_rcb_reference.empty();
    }
};

// Bounded policy state for the read-only pre-claim window. A contended RCB is
// excluded from the current command so the pool selector can rank another
// candidate. This tracker intentionally stops being usable after one candidate
// is accepted; mutation failures are not failover signals.
class MmsRcbPreclaimFailoverTracker final {
public:
    explicit MmsRcbPreclaimFailoverTracker(
        std::size_t maximum_candidate_attempts = 4U);

    [[nodiscard]] bool may_attempt() const noexcept;
    [[nodiscard]] const std::vector<std::string>& excluded_rcb_references() const noexcept {
        return snapshot_.excluded_rcb_references;
    }

    [[nodiscard]] MmsRcbPreclaimOutcome observe(
        const MmsRcbContentionProbeResult& result);
    [[nodiscard]] const MmsRcbPreclaimFailoverSnapshot& snapshot() const noexcept {
        return snapshot_;
    }

private:
    MmsRcbPreclaimFailoverSnapshot snapshot_;
};

} // namespace ar::iec61850::mms
