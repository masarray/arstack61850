// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/scl_sync_health.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ar::iec61850;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

[[nodiscard]] mms::MmsInitialFcReadReference reference(
    const std::string& domain,
    const std::string& item) {
    mms::MmsInitialFcReadReference value;
    value.variable = mms::MmsObjectName::domain_specific(domain, item);
    value.ied_name = "IED1";
    value.logical_device = domain;
    value.logical_node = "LLN0";
    value.functional_constraint = "ST";
    return value;
}

[[nodiscard]] mms::MmsSclAssistedConnectResult base_result(
    const std::size_t root_count = 1U) {
    mms::MmsSclAssistedConnectResult result;
    result.ied_name = "IED1";
    result.domains.expected = {"IED1LD0"};
    result.domains.online = {"IED1LD0"};
    result.plan.ied_name = "IED1";
    result.plan.expected_domains = {"IED1LD0"};

    mms::MmsInitialFcReadBatch plan_batch;
    plan_batch.domain = "IED1LD0";
    plan_batch.logical_node = "LLN0";
    for (std::size_t index = 0U; index < root_count; ++index) {
        plan_batch.references.push_back(reference(
            "IED1LD0", "LLN0$ST$Do" + std::to_string(index)));
    }
    result.plan.batches.push_back(std::move(plan_batch));

    mms::MmsInitialFcBatchResult batch;
    batch.domain = "IED1LD0";
    batch.logical_node = "LLN0";
    for (std::size_t index = 0U; index < root_count; ++index) {
        mms::MmsInitialFcRootResult root;
        root.reference = result.plan.batches.front().references[index];
        root.root_value = mms::MmsDataValue::boolean(true);
        batch.roots.push_back(std::move(root));
    }
    result.batches.push_back(std::move(batch));
    result.successful_root_count = root_count;
    result.associated_after_snapshot = true;
    return result;
}

void exact_evidence_is_matched() {
    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(
        base_result(2U));
    CHECK(health.matched());
    CHECK(!health.degraded());
    CHECK(!health.incompatible());
    CHECK(health.expected_domain_count == 1U);
    CHECK(health.online_expected_domain_count == 1U);
    CHECK(health.missing_domain_count == 0U);
    CHECK(health.extra_domain_count == 0U);
    CHECK(health.planned_root_count == 2U);
    CHECK(health.mapped_root_count == 2U);
}

void partial_domain_evidence_is_degraded() {
    auto result = base_result(1U);
    result.domains.expected = {"IED1LD0", "IED1LD1"};
    result.domains.online = {"IED1LD0", "EXTRA_LD"};
    result.domains.missing = {"IED1LD1"};
    result.domains.extra = {"EXTRA_LD"};
    result.plan.expected_domains = result.domains.expected;

    mms::MmsInitialFcReadBatch skipped_plan;
    skipped_plan.domain = "IED1LD1";
    skipped_plan.logical_node = "LLN0";
    skipped_plan.references.push_back(reference("IED1LD1", "LLN0$ST$Missing"));
    result.plan.batches.push_back(skipped_plan);

    mms::MmsInitialFcBatchResult skipped;
    skipped.domain = "IED1LD1";
    skipped.logical_node = "LLN0";
    skipped.skipped_missing_domain = true;
    mms::MmsInitialFcRootResult root;
    root.reference = skipped_plan.references.front();
    root.mapping_error = "MMS domain is unavailable online; Read was not issued.";
    skipped.roots.push_back(std::move(root));
    result.batches.push_back(std::move(skipped));

    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(result);
    CHECK(health.degraded());
    CHECK(!health.incompatible());
    CHECK(health.online_expected_domain_count == 1U);
    CHECK(health.missing_domain_count == 1U);
    CHECK(health.extra_domain_count == 1U);
    CHECK(health.skipped_root_count == 1U);
    CHECK(health.mapped_root_count == 1U);
}

void access_denied_only_is_degraded_not_incompatible() {
    auto result = base_result(2U);
    result.successful_root_count = 0U;
    result.failed_root_count = 2U;
    for (auto& root : result.batches.front().roots) {
        root.root_value.reset();
        root.access_failure_code = 3U;
    }

    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(result);
    CHECK(health.degraded());
    CHECK(!health.incompatible());
    CHECK(health.access_failure_count == 2U);
    CHECK(health.structural_mapping_failure_count == 0U);
    CHECK(health.mapped_root_count == 0U);
}

void mixed_structural_mapping_is_degraded() {
    auto result = base_result(2U);
    result.successful_root_count = 1U;
    result.failed_root_count = 1U;
    result.batches.front().roots[1].mapping_error =
        "MMS Data structure child count does not match the SCL type tree.";

    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(result);
    CHECK(health.degraded());
    CHECK(!health.incompatible());
    CHECK(health.structural_mapping_failure_count == 1U);
    CHECK(health.mapped_root_count == 1U);
}

void zero_expected_domains_online_is_incompatible() {
    auto result = base_result(1U);
    result.domains.online = {"OTHERLD0"};
    result.domains.missing = {"IED1LD0"};
    result.domains.extra = {"OTHERLD0"};
    result.successful_root_count = 0U;
    result.failed_root_count = 1U;
    result.batches.front().skipped_missing_domain = true;
    result.batches.front().roots.front().root_value.reset();
    result.batches.front().roots.front().mapping_error =
        "MMS domain is unavailable online; Read was not issued.";

    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(result);
    CHECK(health.incompatible());
    CHECK(health.online_expected_domain_count == 0U);

    bool rejected{};
    try {
        mms::MmsSclSynchronizationHealthClassifier::require_compatible(health);
    } catch (const mms::MmsSclAssistedConnectError&) {
        rejected = true;
    }
    CHECK(rejected);
}

void all_readable_roots_structurally_mismatched_is_incompatible() {
    auto result = base_result(2U);
    result.successful_root_count = 0U;
    result.failed_root_count = 2U;
    for (auto& root : result.batches.front().roots) {
        root.mapping_error =
            "FC-root MMS Data child count does not match the SCL type tree.";
    }

    const auto health = mms::MmsSclSynchronizationHealthClassifier::evaluate(result);
    CHECK(health.incompatible());
    CHECK(health.access_failure_count == 0U);
    CHECK(health.structural_mapping_failure_count == 2U);
    CHECK(health.mapped_root_count == 0U);
}

} // namespace

int main() {
    try {
        exact_evidence_is_matched();
        partial_domain_evidence_is_degraded();
        access_denied_only_is_degraded_not_incompatible();
        mixed_structural_mapping_is_degraded();
        zero_expected_domains_online_is_incompatible();
        all_readable_roots_structurally_mismatched_is_incompatible();
        std::cout
            << "SCL_SYNC_HEALTH_PASS matched=pass degraded=pass "
               "access_denied_not_incompatible=true zero_domains=incompatible "
               "all_structural_mismatch=incompatible silent_fallback=false\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
