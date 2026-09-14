// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/scl_assisted_connect.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>

namespace ar::iec61850::mms {

enum class MmsSclSynchronizationHealthKind : std::uint8_t {
    matched,
    degraded,
    incompatible,
};

struct MmsSclSynchronizationHealth final {
    MmsSclSynchronizationHealthKind kind{MmsSclSynchronizationHealthKind::matched};
    std::size_t expected_domain_count{};
    std::size_t online_expected_domain_count{};
    std::size_t missing_domain_count{};
    std::size_t extra_domain_count{};
    std::size_t planned_root_count{};
    std::size_t skipped_root_count{};
    std::size_t access_failure_count{};
    std::size_t structural_mapping_failure_count{};
    std::size_t mapped_root_count{};

    [[nodiscard]] bool matched() const noexcept {
        return kind == MmsSclSynchronizationHealthKind::matched;
    }

    [[nodiscard]] bool degraded() const noexcept {
        return kind == MmsSclSynchronizationHealthKind::degraded;
    }

    [[nodiscard]] bool incompatible() const noexcept {
        return kind == MmsSclSynchronizationHealthKind::incompatible;
    }

    [[nodiscard]] const char* name() const noexcept {
        switch (kind) {
        case MmsSclSynchronizationHealthKind::matched: return "matched";
        case MmsSclSynchronizationHealthKind::degraded: return "degraded";
        case MmsSclSynchronizationHealthKind::incompatible: return "incompatible";
        }
        return "incompatible";
    }

    [[nodiscard]] std::string summary() const {
        std::ostringstream stream;
        stream << "SCL online identity " << name()
               << ": expectedDomains=" << expected_domain_count
               << ", onlineExpectedDomains=" << online_expected_domain_count
               << ", missingDomains=" << missing_domain_count
               << ", extraDomains=" << extra_domain_count
               << ", plannedRoots=" << planned_root_count
               << ", skippedRoots=" << skipped_root_count
               << ", accessFailures=" << access_failure_count
               << ", mappingFailures=" << structural_mapping_failure_count
               << ", mappedRoots=" << mapped_root_count << '.';
        return stream.str();
    }
};

class MmsSclSynchronizationHealthClassifier final {
public:
    [[nodiscard]] static MmsSclSynchronizationHealth evaluate(
        const MmsSclAssistedConnectResult& result) noexcept {
        MmsSclSynchronizationHealth health;
        health.expected_domain_count = result.domains.expected.size();
        health.missing_domain_count = result.domains.missing.size();
        health.extra_domain_count = result.domains.extra.size();
        health.online_expected_domain_count =
            health.missing_domain_count >= health.expected_domain_count
                ? 0U
                : health.expected_domain_count - health.missing_domain_count;
        health.planned_root_count = result.plan.reference_count();

        std::size_t successful_read_root_count{};
        for (const auto& batch : result.batches) {
            if (batch.skipped_missing_domain) {
                health.skipped_root_count += batch.roots.size();
                continue;
            }
            for (const auto& root : batch.roots) {
                if (root.access_failure_code.has_value()) {
                    ++health.access_failure_count;
                    continue;
                }
                if (!root.root_value.has_value()) {
                    // No AccessResult value and no explicit access failure is a
                    // degraded evidence condition, not proof of structural mismatch.
                    ++health.access_failure_count;
                    continue;
                }
                ++successful_read_root_count;
                if (!root.mapping_error.empty()) {
                    ++health.structural_mapping_failure_count;
                } else if (root.mapping_success()) {
                    ++health.mapped_root_count;
                }
            }
        }

        const bool no_expected_domain_online =
            health.expected_domain_count != 0U &&
            health.online_expected_domain_count == 0U;

        // Broad structural mismatch is intentionally narrow: every FC root that
        // actually returned MMS Data failed the local SCL shape and there was no
        // access-denied/read-failure evidence that could explain the absence of a
        // mapped root. Access policy problems therefore remain degraded instead
        // of being mislabeled as a different IED/model.
        const bool all_readable_roots_structurally_incompatible =
            successful_read_root_count != 0U &&
            health.mapped_root_count == 0U &&
            health.structural_mapping_failure_count == successful_read_root_count &&
            health.access_failure_count == 0U &&
            health.skipped_root_count == 0U;

        if (no_expected_domain_online || all_readable_roots_structurally_incompatible) {
            health.kind = MmsSclSynchronizationHealthKind::incompatible;
            return health;
        }

        const bool has_degradation =
            health.missing_domain_count != 0U ||
            health.extra_domain_count != 0U ||
            health.skipped_root_count != 0U ||
            health.access_failure_count != 0U ||
            health.structural_mapping_failure_count != 0U;
        health.kind = has_degradation
            ? MmsSclSynchronizationHealthKind::degraded
            : MmsSclSynchronizationHealthKind::matched;
        return health;
    }

    static void require_compatible(const MmsSclSynchronizationHealth& health) {
        if (health.incompatible()) {
            throw MmsSclAssistedConnectError(
                health.summary() +
                " Trusted-SCL synchronization stopped; explicit live discovery is required to inspect a different online model.");
        }
    }
};

} // namespace ar::iec61850::mms
