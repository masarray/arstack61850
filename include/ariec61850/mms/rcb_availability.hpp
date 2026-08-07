// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_discovery.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsRcbOperationalAvailability : std::uint8_t {
    available,
    in_use,
    used_by_caller,
    unknown,
    no_data_set,
    data_set_empty,
    data_set_unreadable,
};

enum class MmsRcbOperationalAvailabilityConfidence : std::uint8_t {
    exact,
    reduced,
    unknown,
};

[[nodiscard]] inline std::string_view mms_rcb_operational_availability_name(
    const MmsRcbOperationalAvailability value) noexcept {
    switch (value) {
    case MmsRcbOperationalAvailability::available: return "Available";
    case MmsRcbOperationalAvailability::in_use: return "InUse";
    case MmsRcbOperationalAvailability::used_by_caller: return "UsedByCaller";
    case MmsRcbOperationalAvailability::unknown: return "Unknown";
    case MmsRcbOperationalAvailability::no_data_set: return "NoDataSet";
    case MmsRcbOperationalAvailability::data_set_empty: return "DataSetEmpty";
    case MmsRcbOperationalAvailability::data_set_unreadable: return "DataSetUnreadable";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string_view mms_rcb_operational_confidence_name(
    const MmsRcbOperationalAvailabilityConfidence value) noexcept {
    switch (value) {
    case MmsRcbOperationalAvailabilityConfidence::exact: return "Exact";
    case MmsRcbOperationalAvailabilityConfidence::reduced: return "Reduced";
    case MmsRcbOperationalAvailabilityConfidence::unknown: return "Unknown";
    }
    return "Unknown";
}

struct MmsRcbOperationalAvailabilityOptions final {
    std::size_t maximum_report_controls{512U};
    std::vector<std::string> caller_owned_rcb_references;
};

struct MmsRcbOperationalAvailabilitySnapshot final {
    std::string reference;
    std::string domain;
    std::string logical_node;
    std::string name;
    std::string mode;
    bool buffered{};
    std::string data_set_reference;
    std::string report_id;
    std::string configuration_revision;
    std::string enabled_state;
    std::string reservation_state;
    std::string reservation_time_seconds;
    std::string owner;
    bool data_set_directory_read{};
    bool data_set_directory_success{};
    std::optional<bool> data_set_is_deletable;
    std::size_t data_set_member_count{};
    MmsRcbOperationalAvailability availability{
        MmsRcbOperationalAvailability::unknown};
    MmsRcbOperationalAvailabilityConfidence confidence{
        MmsRcbOperationalAvailabilityConfidence::unknown};
    std::string reason;
    std::vector<std::string> probe_diagnostics;

    [[nodiscard]] bool selectable() const noexcept {
        return availability == MmsRcbOperationalAvailability::available ||
               availability == MmsRcbOperationalAvailability::used_by_caller;
    }

    [[nodiscard]] std::string summary() const {
        return reference + ": " +
            std::string{mms_rcb_operational_availability_name(availability)} +
            " (" + std::string{mms_rcb_operational_confidence_name(confidence)} +
            ") - " + reason;
    }
};

struct MmsRcbOperationalAvailabilityResult final {
    std::vector<MmsRcbOperationalAvailabilitySnapshot> report_controls;
    std::vector<std::string> warnings;

    [[nodiscard]] std::size_t count(
        const MmsRcbOperationalAvailability value) const noexcept {
        return static_cast<std::size_t>(std::count_if(
            report_controls.begin(), report_controls.end(),
            [value](const auto& item) { return item.availability == value; }));
    }

    [[nodiscard]] std::size_t available_count() const noexcept {
        return count(MmsRcbOperationalAvailability::available);
    }
    [[nodiscard]] std::size_t in_use_count() const noexcept {
        return count(MmsRcbOperationalAvailability::in_use);
    }
    [[nodiscard]] std::size_t unknown_count() const noexcept {
        return count(MmsRcbOperationalAvailability::unknown);
    }

    [[nodiscard]] std::string summary() const {
        return "RCB availability checked: total=" +
            std::to_string(report_controls.size()) +
            ", available=" + std::to_string(available_count()) +
            ", in-use=" + std::to_string(in_use_count()) +
            ", unknown=" + std::to_string(unknown_count()) + ".";
    }
};

class MmsRcbOperationalAvailabilityEvaluator final {
public:
    [[nodiscard]] static MmsRcbOperationalAvailabilityResult evaluate(
        const MmsLiveDiscoveryResult& discovery,
        const MmsRcbOperationalAvailabilityOptions& options = {}) {
        MmsRcbOperationalAvailabilityResult result;
        const auto limit = std::clamp<std::size_t>(
            options.maximum_report_controls, 1U, 4'096U);
        const auto count = std::min(
            limit, discovery.report_inventory.report_controls.size());
        result.report_controls.reserve(count);

        if (discovery.report_inventory.report_controls.size() > count) {
            result.warnings.push_back(
                "Availability evaluation was bounded to " + std::to_string(count) +
                " of " +
                std::to_string(discovery.report_inventory.report_controls.size()) +
                " discovered RCBs.");
        }

        std::vector<std::string> caller_owned;
        caller_owned.reserve(options.caller_owned_rcb_references.size());
        for (const auto& reference : options.caller_owned_rcb_references) {
            const auto normalized = normalize_reference(reference);
            if (!normalized.empty() &&
                std::none_of(
                    caller_owned.begin(), caller_owned.end(),
                    [&normalized](const auto& existing) {
                        return same(existing, normalized);
                    })) {
                caller_owned.push_back(normalized);
            }
        }

        for (std::size_t index = 0U; index < count; ++index) {
            const auto& candidate = discovery.report_inventory.report_controls[index];
            const auto normalized_reference = normalize_reference(candidate.reference);
            const bool owned = std::any_of(
                caller_owned.begin(), caller_owned.end(),
                [&normalized_reference](const auto& reference) {
                    return same(reference, normalized_reference);
                });
            result.report_controls.push_back(
                evaluate_one(discovery, candidate, owned));
        }

        std::sort(
            result.report_controls.begin(), result.report_controls.end(),
            [](const auto& left, const auto& right) {
                if ((left.data_set_member_count > 0U) !=
                    (right.data_set_member_count > 0U)) {
                    return left.data_set_member_count > 0U;
                }
                const auto left_rank = availability_rank(left.availability);
                const auto right_rank = availability_rank(right.availability);
                if (left_rank != right_rank) return left_rank < right_rank;
                if (left.buffered != right.buffered) return left.buffered;
                return lower(left.reference) < lower(right.reference);
            });
        return result;
    }

private:
    [[nodiscard]] static MmsRcbOperationalAvailabilitySnapshot evaluate_one(
        const MmsLiveDiscoveryResult& discovery,
        const MmsReportControlCandidate& candidate,
        const bool caller_owned) {
        MmsRcbOperationalAvailabilitySnapshot snapshot;
        snapshot.reference = candidate.reference;
        snapshot.domain = candidate.domain;
        snapshot.logical_node = candidate.logical_node;
        snapshot.name = candidate.name;
        snapshot.mode = candidate.mode();
        snapshot.buffered = candidate.buffered;

        const auto evidence = std::find_if(
            discovery.report_controls.begin(), discovery.report_controls.end(),
            [&candidate](const auto& item) {
                return same(item.candidate.reference, candidate.reference);
            });
        if (evidence == discovery.report_controls.end() || !evidence->state) {
            snapshot.availability = MmsRcbOperationalAvailability::unknown;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::unknown;
            snapshot.reason = evidence != discovery.report_controls.end() &&
                    !evidence->error.empty()
                ? "Runtime RCB state could not be read: " + evidence->error
                : "Runtime ownership state was not read for this RCB.";
            return snapshot;
        }

        const auto& state = *evidence->state;
        snapshot.data_set_reference = normalize_reference(state.data_set_reference);
        snapshot.report_id = state.report_id;
        snapshot.configuration_revision = optional_unsigned(state.configuration_revision);
        snapshot.enabled_state = optional_bool(state.report_enabled);
        snapshot.reservation_state = optional_bool(state.reserved);
        snapshot.reservation_time_seconds = optional_unsigned(
            state.reservation_time_seconds);
        snapshot.owner = owner_text(state.owner);
        snapshot.probe_diagnostics = state.diagnostics;

        const auto directory = find_directory(
            discovery, snapshot.data_set_reference);
        snapshot.data_set_directory_read = directory != nullptr;
        if (directory != nullptr) {
            snapshot.data_set_directory_success = directory->success();
            if (directory->success()) {
                snapshot.data_set_is_deletable = directory->directory->deletable;
                snapshot.data_set_member_count = directory->directory->members.size();
            }
        }

        const bool has_owner_value = has_owner(state.owner);
        const bool enabled = state.report_enabled == true;
        const bool reserved = state.reserved == true;
        const bool timed_reservation =
            state.reservation_time_seconds.has_value() &&
            *state.reservation_time_seconds > 0U;
        const bool has_data_set = !snapshot.data_set_reference.empty();

        if (caller_owned) {
            snapshot.availability = MmsRcbOperationalAvailability::used_by_caller;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::exact;
            snapshot.reason =
                "This RCB is active in the caller's current association/session.";
            return snapshot;
        }

        if (enabled || reserved || timed_reservation || has_owner_value) {
            snapshot.availability = MmsRcbOperationalAvailability::in_use;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::exact;
            snapshot.reason = build_busy_reason(
                state, has_owner_value);
            return snapshot;
        }

        if (!has_data_set) {
            snapshot.availability = MmsRcbOperationalAvailability::no_data_set;
            snapshot.confidence = state.report_enabled == false
                ? MmsRcbOperationalAvailabilityConfidence::exact
                : MmsRcbOperationalAvailabilityConfidence::reduced;
            snapshot.reason =
                "The RCB does not reference a static DataSet and cannot be "
                "exported as a populated legacy-SAS report block.";
            return snapshot;
        }

        if (directory != nullptr && !directory->success()) {
            snapshot.availability =
                MmsRcbOperationalAvailability::data_set_unreadable;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::exact;
            snapshot.reason = directory->error.empty()
                ? "The referenced DataSet directory could not be read."
                : "The referenced DataSet directory could not be read: " +
                    directory->error;
            return snapshot;
        }

        if (directory != nullptr && directory->success() &&
            directory->directory->members.empty()) {
            snapshot.availability = MmsRcbOperationalAvailability::data_set_empty;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::exact;
            snapshot.reason = "The referenced DataSet is empty.";
            return snapshot;
        }

        if (state.report_enabled == false &&
            reservation_is_explicitly_free(candidate, state, has_owner_value)) {
            snapshot.availability = MmsRcbOperationalAvailability::available;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::exact;
            snapshot.reason =
                "RptEna is false, reservation state is explicitly free, and "
                "the RCB references a DataSet.";
            return snapshot;
        }

        if (state.report_enabled == false && candidate.buffered &&
            directory != nullptr && directory->success() &&
            !directory->directory->members.empty()) {
            snapshot.availability = MmsRcbOperationalAvailability::unknown;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::reduced;
            snapshot.reason =
                "RptEna is false and the DataSet is populated, but this BRCB "
                "does not expose enough reservation evidence to prove availability.";
            return snapshot;
        }

        if (state.report_enabled == false && !candidate.buffered &&
            !state.reserved.has_value()) {
            snapshot.availability = MmsRcbOperationalAvailability::unknown;
            snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::reduced;
            snapshot.reason =
                "RptEna is false, but the URCB Resv state was not returned.";
            return snapshot;
        }

        snapshot.availability = MmsRcbOperationalAvailability::unknown;
        snapshot.confidence = MmsRcbOperationalAvailabilityConfidence::unknown;
        snapshot.reason =
            "Runtime ownership state could not be proven from the exposed RCB attributes.";
        return snapshot;
    }

    [[nodiscard]] static const MmsDataSetDirectoryEvidence* find_directory(
        const MmsLiveDiscoveryResult& discovery,
        const std::string& reference) {
        if (reference.empty()) return nullptr;
        const auto found = std::find_if(
            discovery.data_set_directories.begin(),
            discovery.data_set_directories.end(),
            [&reference](const auto& item) {
                return same(normalize_reference(item.candidate.reference), reference);
            });
        return found == discovery.data_set_directories.end() ? nullptr : &*found;
    }

    [[nodiscard]] static bool reservation_is_explicitly_free(
        const MmsReportControlCandidate& candidate,
        const MmsReportControlState& state,
        const bool has_owner_value) noexcept {
        if (has_owner_value) return false;
        if (candidate.buffered) {
            return state.reservation_time_seconds.has_value() &&
                   *state.reservation_time_seconds == 0U;
        }
        return state.reserved.has_value() && !*state.reserved;
    }

    [[nodiscard]] static bool has_owner(
        const std::vector<std::uint8_t>& owner) noexcept {
        return std::any_of(owner.begin(), owner.end(), [](const auto byte) {
            return byte != 0U;
        });
    }

    [[nodiscard]] static std::string owner_text(
        const std::vector<std::uint8_t>& owner) {
        if (!has_owner(owner)) return {};
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (std::size_t index = 0U; index < owner.size(); ++index) {
            if (index != 0U) output << ':';
            output << std::setw(2) << static_cast<unsigned int>(owner[index]);
        }
        return output.str();
    }

    [[nodiscard]] static std::string build_busy_reason(
        const MmsReportControlState& state,
        const bool has_owner_value) {
        std::vector<std::string> evidence;
        if (state.report_enabled == true) evidence.emplace_back("RptEna=true");
        if (state.reserved == true) evidence.emplace_back("Resv=true");
        if (state.reservation_time_seconds.has_value() &&
            *state.reservation_time_seconds > 0U) {
            evidence.push_back(
                "ResvTms=" + std::to_string(*state.reservation_time_seconds));
        }
        if (has_owner_value) {
            evidence.push_back("Owner=" + owner_text(state.owner));
        }
        std::string reason = "The RCB is in use or reserved";
        if (!evidence.empty()) {
            reason += " (";
            for (std::size_t index = 0U; index < evidence.size(); ++index) {
                if (index != 0U) reason += ", ";
                reason += evidence[index];
            }
            reason += ')';
        }
        reason += '.';
        return reason;
    }

    [[nodiscard]] static std::string optional_bool(
        const std::optional<bool> value) {
        if (!value.has_value()) return {};
        return *value ? "true" : "false";
    }

    [[nodiscard]] static std::string optional_unsigned(
        const std::optional<std::uint64_t> value) {
        return value.has_value() ? std::to_string(*value) : std::string{};
    }

    [[nodiscard]] static std::string normalize_reference(
        std::string reference) {
        reference.erase(
            reference.begin(),
            std::find_if(reference.begin(), reference.end(), [](const char value) {
                return value != ' ' && value != '\t' && value != '\r' && value != '\n';
            }));
        reference.erase(
            std::find_if(reference.rbegin(), reference.rend(), [](const char value) {
                return value != ' ' && value != '\t' && value != '\r' && value != '\n';
            }).base(),
            reference.end());
        std::replace(reference.begin(), reference.end(), '$', '.');
        return reference;
    }

    [[nodiscard]] static std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](const char value) {
            if (value >= 'A' && value <= 'Z') {
                return static_cast<char>(value - 'A' + 'a');
            }
            return value;
        });
        return value;
    }

    [[nodiscard]] static bool same(
        const std::string_view left,
        const std::string_view right) {
        return lower(std::string{left}) == lower(std::string{right});
    }

    [[nodiscard]] static int availability_rank(
        const MmsRcbOperationalAvailability value) noexcept {
        switch (value) {
        case MmsRcbOperationalAvailability::available: return 0;
        case MmsRcbOperationalAvailability::used_by_caller: return 1;
        case MmsRcbOperationalAvailability::in_use: return 2;
        case MmsRcbOperationalAvailability::unknown: return 3;
        case MmsRcbOperationalAvailability::data_set_unreadable: return 4;
        case MmsRcbOperationalAvailability::data_set_empty: return 5;
        case MmsRcbOperationalAvailability::no_data_set: return 6;
        }
        return 9;
    }
};

} // namespace ar::iec61850::mms
