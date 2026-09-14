// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/scl/model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

class MmsSclAssistedConnectError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

struct MmsInitialFcReadReference final {
    MmsObjectName variable;
    std::string ied_name;
    std::string logical_device;
    std::string logical_node;
    std::string functional_constraint;
    std::vector<std::size_t> model_entry_indices;

    [[nodiscard]] std::string reference() const {
        return variable.reference();
    }
};

struct MmsInitialFcReadBatch final {
    std::string domain;
    std::string logical_node;
    std::vector<MmsInitialFcReadReference> references;
};

struct MmsInitialFcReadPlan final {
    std::string ied_name;
    std::vector<std::string> expected_domains;
    std::vector<MmsInitialFcReadBatch> batches;

    [[nodiscard]] std::size_t reference_count() const noexcept {
        std::size_t count = 0U;
        for (const auto& batch : batches) count += batch.references.size();
        return count;
    }
};

struct MmsInitialFcReadPlannerOptions final {
    // Compatibility-first default observed in the reference SCL-assisted trace.
    // This is intentionally independent from MMS negotiated outstanding calls.
    std::size_t maximum_references_per_read{10U};
    std::size_t maximum_total_references{65'536U};
};

class InitialFcReadPlanner final {
public:
    [[nodiscard]] static MmsInitialFcReadPlan build(
        const scl::SclDocument& document,
        std::string ied_name = {},
        const MmsInitialFcReadPlannerOptions& options = {});
};

struct MmsSclDomainValidation final {
    std::vector<std::string> expected;
    std::vector<std::string> online;
    std::vector<std::string> missing;
    std::vector<std::string> extra;

    [[nodiscard]] bool all_expected_online() const noexcept {
        return missing.empty();
    }
};

class MmsSclDomainValidator final {
public:
    [[nodiscard]] static MmsSclDomainValidation compare(
        std::span<const std::string> expected,
        std::span<const std::string> online);
};

struct MmsSclMappedLeaf final {
    std::size_t model_entry_index{};
    std::string signal_reference;
    std::string basic_type;
    bool quality{};
    bool timestamp{};
    std::optional<MmsDataValue> value;
};

struct MmsInitialFcRootResult final {
    MmsInitialFcReadReference reference;
    std::optional<std::uint32_t> access_failure_code;
    std::optional<MmsDataValue> root_value;
    std::vector<MmsSclMappedLeaf> mapped_leaves;
    std::string mapping_error;

    [[nodiscard]] bool read_success() const noexcept {
        return root_value.has_value() && !access_failure_code.has_value();
    }

    [[nodiscard]] bool mapping_success() const noexcept {
        return read_success() && mapping_error.empty();
    }
};

struct MmsInitialFcBatchResult final {
    std::uint32_t invoke_id{};
    std::string domain;
    std::string logical_node;
    bool skipped_missing_domain{};
    std::vector<MmsInitialFcRootResult> roots;
};

struct MmsSclAssistedConnectOptions final {
    std::size_t maximum_domain_pages{256U};
    std::size_t maximum_domains{4'096U};
    MmsInitialFcReadPlannerOptions planner{};

    // When false, missing domains are surfaced and their batches are skipped.
    // The SCL model identity is preserved; network evidence never rewrites it.
    bool require_all_expected_domains{};
};

struct MmsSclAssistedConnectResult final {
    MmsEndpoint endpoint;
    std::string ied_name;
    MmsSclDomainValidation domains;
    MmsInitialFcReadPlan plan;
    std::vector<MmsInitialFcBatchResult> batches;
    std::vector<std::string> diagnostics;
    std::size_t domain_request_count{};
    std::size_t read_request_count{};
    std::size_t successful_root_count{};
    std::size_t failed_root_count{};
    std::size_t mapped_leaf_count{};
    bool associated_after_snapshot{};

    [[nodiscard]] std::size_t confirmed_request_count() const noexcept {
        return domain_request_count + read_request_count;
    }

    [[nodiscard]] std::string summary() const {
        std::ostringstream stream;
        stream << "SCL-assisted MMS connect: endpoint=" << endpoint.host << ':'
               << endpoint.port << ", IED=" << ied_name
               << ", domains=" << domains.online.size() << '/'
               << domains.expected.size()
               << ", missing=" << domains.missing.size()
               << ", extra=" << domains.extra.size()
               << ", FC-roots=" << plan.reference_count()
               << ", ReadRequests=" << read_request_count
               << ", successfulRoots=" << successful_root_count
               << ", failedRoots=" << failed_root_count
               << ", mappedLeaves=" << mapped_leaf_count
               << ", associated=" << (associated_after_snapshot ? "true" : "false")
               << '.';
        return stream.str();
    }
};

namespace detail {

[[nodiscard]] inline std::string resolve_scl_ied_name(
    const scl::SclDocument& document,
    std::string requested) {
    if (!requested.empty()) {
        const bool known = std::any_of(
            document.ieds.begin(), document.ieds.end(),
            [&](const auto& ied) { return ied.name == requested; }) ||
            std::any_of(
                document.logical_nodes.begin(), document.logical_nodes.end(),
                [&](const auto& logical_node) {
                    return logical_node.ied_name == requested;
                });
        if (!known) {
            throw MmsSclAssistedConnectError(
                "Requested IED '" + requested + "' is not present in the SCL model.");
        }
        return requested;
    }

    std::set<std::string, std::less<>> names;
    for (const auto& logical_node : document.logical_nodes) {
        if (!logical_node.ied_name.empty()) names.insert(logical_node.ied_name);
    }
    if (names.empty()) {
        for (const auto& ied : document.ieds) {
            if (!ied.name.empty()) names.insert(ied.name);
        }
    }
    if (names.size() != 1U) {
        throw MmsSclAssistedConnectError(
            "SCL-assisted connect requires an explicit IED when the SCL model does not resolve to exactly one IED.");
    }
    return *names.begin();
}

[[nodiscard]] inline std::string logical_node_name(
    const scl::SclLogicalNode& logical_node) {
    if (!logical_node.name.empty()) return logical_node.name;
    return logical_node.prefix + logical_node.ln_class + logical_node.ln_inst;
}

[[nodiscard]] inline bool model_entry_matches_ln(
    const scl::SclDataSetEntry& entry,
    const scl::SclLogicalNode& logical_node) noexcept {
    return entry.ied_name == logical_node.ied_name &&
        entry.ld_inst == logical_node.ld_inst &&
        entry.prefix == logical_node.prefix &&
        entry.ln_class == logical_node.ln_class &&
        entry.ln_inst == logical_node.ln_inst;
}

[[nodiscard]] inline bool report_control_matches_ln(
    const scl::SclReportControl& control,
    const scl::SclLogicalNode& logical_node) {
    if (control.ied_name != logical_node.ied_name ||
        control.ld_inst != logical_node.ld_inst) {
        return false;
    }
    const auto name = logical_node_name(logical_node);
    const auto domain = logical_node.mms_domain();
    const auto reference_prefix = domain + "/" + name;
    if (control.control_block_reference.starts_with(reference_prefix)) return true;
    if (!control.logical_node_path.empty() &&
        control.logical_node_path.find(name) != std::string::npos) {
        return true;
    }
    return false;
}

[[nodiscard]] inline std::vector<std::string> split_scl_path(
    const std::string_view value) {
    std::vector<std::string> parts;
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const auto separator = value.find('.', offset);
        const auto end = separator == std::string_view::npos ? value.size() : separator;
        if (end > offset) parts.emplace_back(value.substr(offset, end - offset));
        if (separator == std::string_view::npos) break;
        offset = separator + 1U;
    }
    return parts;
}

struct SclValueNode final {
    std::string name;
    std::optional<std::size_t> model_entry_index;
    std::vector<SclValueNode> children;
};

[[nodiscard]] inline SclValueNode* find_or_append_child(
    SclValueNode& parent,
    const std::string& name) {
    const auto found = std::find_if(
        parent.children.begin(), parent.children.end(),
        [&](const auto& child) { return child.name == name; });
    if (found != parent.children.end()) return &*found;
    parent.children.push_back(SclValueNode{name, std::nullopt, {}});
    return &parent.children.back();
}

[[nodiscard]] inline bool build_scl_value_tree(
    const scl::SclDocument& document,
    const MmsInitialFcReadReference& reference,
    SclValueNode& root,
    std::string& error) {
    for (const auto model_index : reference.model_entry_indices) {
        if (model_index >= document.model_entries.size()) {
            error = "SCL model-entry index is out of range.";
            return false;
        }
        const auto& entry = document.model_entries[model_index];
        auto path = split_scl_path(entry.do_name);
        auto da_path = split_scl_path(entry.da_name);
        path.insert(path.end(), da_path.begin(), da_path.end());
        if (path.empty()) {
            error = "SCL FC-root mapping contains an empty DO/DA path.";
            return false;
        }

        auto* node = &root;
        for (const auto& component : path) {
            node = find_or_append_child(*node, component);
        }
        if (node->model_entry_index.has_value() &&
            *node->model_entry_index != model_index) {
            error = "SCL FC-root mapping contains duplicate semantic leaf paths.";
            return false;
        }
        node->model_entry_index = model_index;
    }
    return true;
}

[[nodiscard]] inline bool container_value(const MmsDataValue& value) noexcept {
    return value.kind() == MmsDataKind::structure || value.kind() == MmsDataKind::array;
}

[[nodiscard]] inline bool map_scl_node(
    const scl::SclDocument& document,
    const SclValueNode& node,
    const MmsDataValue& value,
    std::vector<MmsSclMappedLeaf>& mapped,
    std::string& error) {
    if (node.children.empty()) {
        if (!node.model_entry_index.has_value() ||
            *node.model_entry_index >= document.model_entries.size()) {
            error = "SCL type tree ended without a valid leaf identity.";
            return false;
        }
        const auto& entry = document.model_entries[*node.model_entry_index];
        MmsSclMappedLeaf leaf;
        leaf.model_entry_index = *node.model_entry_index;
        leaf.signal_reference = entry.signal_reference;
        leaf.basic_type = entry.basic_type;
        leaf.quality = entry.is_quality;
        leaf.timestamp = entry.is_timestamp;
        leaf.value = value;
        mapped.push_back(std::move(leaf));
        return true;
    }

    if (node.model_entry_index.has_value()) {
        error = "SCL type tree contains a node that is both a leaf and a structure.";
        return false;
    }
    if (!container_value(value)) {
        error = "MMS Data is scalar where the SCL type tree requires a structure.";
        return false;
    }
    if (value.children().size() != node.children.size()) {
        error = "MMS Data structure child count does not match the SCL type tree.";
        return false;
    }
    for (std::size_t index = 0U; index < node.children.size(); ++index) {
        if (!map_scl_node(
                document, node.children[index], value.children()[index], mapped, error)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool map_scl_root_value(
    const scl::SclDocument& document,
    const MmsInitialFcReadReference& reference,
    const MmsDataValue& value,
    std::vector<MmsSclMappedLeaf>& mapped,
    std::string& error) {
    mapped.clear();
    error.clear();
    if (reference.model_entry_indices.empty()) {
        // Control-block FC roots can be structurally useful without DA leaves in
        // the generic model-entry catalog. Preserve the root value as evidence.
        return true;
    }

    SclValueNode root;
    if (!build_scl_value_tree(document, reference, root, error)) return false;
    if (!container_value(value)) {
        error = "FC-root MMS Data is not a structure/array.";
        return false;
    }
    if (value.children().size() != root.children.size()) {
        error = "FC-root MMS Data child count does not match the SCL type tree.";
        return false;
    }

    std::vector<MmsSclMappedLeaf> staged;
    staged.reserve(reference.model_entry_indices.size());
    for (std::size_t index = 0U; index < root.children.size(); ++index) {
        if (!map_scl_node(
                document, root.children[index], value.children()[index], staged, error)) {
            return false;
        }
    }
    if (staged.size() != reference.model_entry_indices.size()) {
        error = "FC-root mapping did not resolve every SCL leaf exactly once.";
        return false;
    }
    mapped = std::move(staged);
    return true;
}

[[nodiscard]] inline std::span<const std::uint8_t> scl_response_payload(
    const MmsConfirmedExchangeResult& exchange,
    const std::string_view operation) {
    if (exchange.envelope.kind != MmsPduKind::confirmed_response) {
        throw MmsSclAssistedConnectError(
            std::string{operation} + " did not return a Confirmed-ResponsePDU.");
    }
    if (!exchange.presentation_payload.empty()) return exchange.presentation_payload;
    if (!exchange.envelope.mms_payload.empty()) return exchange.envelope.mms_payload;
    throw MmsSclAssistedConnectError(
        std::string{operation} + " returned no decodable MMS payload.");
}

} // namespace detail

inline MmsInitialFcReadPlan InitialFcReadPlanner::build(
    const scl::SclDocument& document,
    std::string ied_name,
    const MmsInitialFcReadPlannerOptions& options) {
    if (options.maximum_references_per_read == 0U ||
        options.maximum_references_per_read > MmsServiceCodec::maximum_variables ||
        options.maximum_total_references == 0U) {
        throw std::invalid_argument("Initial FC-read planner bounds are invalid.");
    }

    MmsInitialFcReadPlan plan;
    plan.ied_name = detail::resolve_scl_ied_name(document, std::move(ied_name));
    std::set<std::string, std::less<>> seen_domains;
    std::set<std::string, std::less<>> seen_logical_nodes;

    for (const auto& logical_node : document.logical_nodes) {
        if (logical_node.ied_name != plan.ied_name) continue;
        const auto domain = logical_node.mms_domain();
        const auto ln_name = detail::logical_node_name(logical_node);
        if (domain.empty() || ln_name.empty()) continue;

        if (seen_domains.insert(domain).second) plan.expected_domains.push_back(domain);
        const auto ln_identity = domain + "\n" + ln_name;
        if (!seen_logical_nodes.insert(ln_identity).second) continue;

        std::map<std::string, std::vector<std::size_t>, std::less<>> by_fc;
        for (std::size_t index = 0U; index < document.model_entries.size(); ++index) {
            const auto& entry = document.model_entries[index];
            if (!detail::model_entry_matches_ln(entry, logical_node) ||
                entry.functional_constraint.empty()) {
                continue;
            }
            by_fc[entry.functional_constraint].push_back(index);
        }

        for (const auto& control : document.report_controls) {
            if (!detail::report_control_matches_ln(control, logical_node)) continue;
            static_cast<void>(by_fc[control.buffered ? "BR" : "RP"]);
        }

        std::vector<MmsInitialFcReadReference> ln_references;
        ln_references.reserve(by_fc.size());
        for (auto& [functional_constraint, model_indices] : by_fc) {
            if (functional_constraint.empty()) continue;
            MmsInitialFcReadReference reference;
            reference.variable = MmsObjectName::domain_specific(
                domain, ln_name + "$" + functional_constraint);
            reference.ied_name = plan.ied_name;
            reference.logical_device = logical_node.ld_inst;
            reference.logical_node = ln_name;
            reference.functional_constraint = functional_constraint;
            reference.model_entry_indices = std::move(model_indices);
            ln_references.push_back(std::move(reference));
        }

        for (std::size_t offset = 0U; offset < ln_references.size();) {
            const auto remaining = ln_references.size() - offset;
            const auto take = std::min(options.maximum_references_per_read, remaining);
            if (plan.reference_count() + take > options.maximum_total_references) {
                throw MmsSclAssistedConnectError(
                    "Initial FC-read plan exceeded the configured total-reference bound.");
            }
            MmsInitialFcReadBatch batch;
            batch.domain = domain;
            batch.logical_node = ln_name;
            batch.references.reserve(take);
            for (std::size_t index = 0U; index < take; ++index) {
                batch.references.push_back(std::move(ln_references[offset + index]));
            }
            plan.batches.push_back(std::move(batch));
            offset += take;
        }
    }

    if (plan.expected_domains.empty()) {
        throw MmsSclAssistedConnectError(
            "Selected SCL IED contains no Logical Device/MMS domain inventory.");
    }
    if (plan.batches.empty()) {
        throw MmsSclAssistedConnectError(
            "Selected SCL IED contains no readable Functional Constraint roots.");
    }
    return plan;
}

inline MmsSclDomainValidation MmsSclDomainValidator::compare(
    const std::span<const std::string> expected,
    const std::span<const std::string> online) {
    MmsSclDomainValidation result;
    result.expected.assign(expected.begin(), expected.end());
    result.online.assign(online.begin(), online.end());
    const std::set<std::string, std::less<>> expected_set(expected.begin(), expected.end());
    const std::set<std::string, std::less<>> online_set(online.begin(), online.end());
    for (const auto& domain : expected) {
        if (!online_set.contains(domain)) result.missing.push_back(domain);
    }
    for (const auto& domain : online) {
        if (!expected_set.contains(domain)) result.extra.push_back(domain);
    }
    return result;
}

class MmsSclAssistedConnectClient final {
public:
    explicit MmsSclAssistedConnectClient(MmsAssociationRuntime& association)
        : association_{association} {}

    [[nodiscard]] MmsSclAssistedConnectResult synchronize(
        const scl::SclDocument& document,
        std::string ied_name = {},
        const MmsSclAssistedConnectOptions& options = {},
        const std::stop_token stop_token = {}) {
        if (!association_.associated()) {
            throw MmsSclAssistedConnectError(
                "SCL-assisted synchronization requires an active MMS association.");
        }
        if (options.maximum_domain_pages == 0U || options.maximum_domains == 0U) {
            throw std::invalid_argument("SCL-assisted domain-query bounds are invalid.");
        }

        MmsSclAssistedConnectResult result;
        result.endpoint = association_.endpoint();
        result.plan = InitialFcReadPlanner::build(
            document, std::move(ied_name), options.planner);
        result.ied_name = result.plan.ied_name;

        std::vector<std::string> online_domains;
        std::set<std::string, std::less<>> seen_domains;
        std::string continue_after;
        for (std::size_t page = 0U; page < options.maximum_domain_pages; ++page) {
            MmsGetNameListRequest request;
            request.invoke_id = association_.next_invoke_id();
            request.object_class = MmsGetNameListObjectClass::domain;
            request.scope = MmsObjectScopeKind::vmd_specific;
            request.continue_after = continue_after;
            const auto encoded = MmsServiceCodec::encode_get_name_list_request_p_data(
                request, association_.negotiated().presentation_context_id);
            const auto exchange = association_.exchange_confirmed(
                encoded, request.invoke_id, stop_token);
            ++result.domain_request_count;
            const auto response = MmsServiceCodec::decode_get_name_list_response(
                detail::scl_response_payload(exchange, "SCL domain validation"),
                request.invoke_id);

            const auto count_before = online_domains.size();
            for (const auto& domain : response.names) {
                if (!seen_domains.insert(domain).second) continue;
                if (online_domains.size() >= options.maximum_domains) {
                    throw MmsSclAssistedConnectError(
                        "SCL-assisted domain validation exceeded the configured domain bound.");
                }
                online_domains.push_back(domain);
            }
            if (!response.more_follows) break;
            if (response.names.empty() || online_domains.size() == count_before) {
                throw MmsSclAssistedConnectError(
                    "SCL-assisted domain validation reported moreFollows without forward progress.");
            }
            continue_after = response.names.back();
            if (page + 1U == options.maximum_domain_pages) {
                throw MmsSclAssistedConnectError(
                    "SCL-assisted domain validation exceeded the configured page bound.");
            }
        }

        result.domains = MmsSclDomainValidator::compare(
            result.plan.expected_domains, online_domains);
        for (const auto& domain : result.domains.missing) {
            result.diagnostics.push_back(
                "Expected SCL domain is unavailable online: " + domain + '.');
        }
        for (const auto& domain : result.domains.extra) {
            result.diagnostics.push_back(
                "Additional online domain is not merged into the trusted SCL model: " +
                domain + '.');
        }
        if (options.require_all_expected_domains && !result.domains.missing.empty()) {
            throw MmsSclAssistedConnectError(
                "SCL-assisted synchronization stopped because one or more expected domains are unavailable online.");
        }

        const std::set<std::string, std::less<>> online_set(
            online_domains.begin(), online_domains.end());
        result.batches.reserve(result.plan.batches.size());
        for (const auto& batch : result.plan.batches) {
            MmsInitialFcBatchResult batch_result;
            batch_result.domain = batch.domain;
            batch_result.logical_node = batch.logical_node;
            batch_result.roots.reserve(batch.references.size());

            if (!online_set.contains(batch.domain)) {
                batch_result.skipped_missing_domain = true;
                for (const auto& reference : batch.references) {
                    MmsInitialFcRootResult root;
                    root.reference = reference;
                    root.mapping_error = "MMS domain is unavailable online; Read was not issued.";
                    batch_result.roots.push_back(std::move(root));
                    ++result.failed_root_count;
                }
                result.batches.push_back(std::move(batch_result));
                continue;
            }

            MmsReadRequest request;
            request.invoke_id = association_.next_invoke_id();
            request.variables.reserve(batch.references.size());
            for (const auto& reference : batch.references) {
                request.variables.push_back(reference.variable);
            }
            const auto encoded = MmsServiceCodec::encode_read_request_p_data(
                request, association_.negotiated().presentation_context_id);
            const auto exchange = association_.exchange_confirmed(
                encoded, request.invoke_id, stop_token);
            ++result.read_request_count;
            batch_result.invoke_id = request.invoke_id;
            const auto response = MmsServiceCodec::decode_read_response(
                detail::scl_response_payload(exchange, "SCL initial FC-root Read"),
                request.invoke_id);
            if (response.results.size() != batch.references.size()) {
                throw MmsSclAssistedConnectError(
                    "Initial FC-root Read result count does not match request order; refusing to map an ambiguous response into the SCL model.");
            }

            for (std::size_t index = 0U; index < batch.references.size(); ++index) {
                MmsInitialFcRootResult root;
                root.reference = batch.references[index];
                const auto& access = response.results[index];
                if (!access.success()) {
                    root.access_failure_code = access.failure_code.value_or(0U);
                    ++result.failed_root_count;
                    result.diagnostics.push_back(
                        "FC-root Read failed for " + root.reference.reference() +
                        " with access-result code " +
                        std::to_string(*root.access_failure_code) + '.');
                    batch_result.roots.push_back(std::move(root));
                    continue;
                }

                root.root_value = *access.value;
                std::vector<MmsSclMappedLeaf> mapped;
                std::string mapping_error;
                if (!detail::map_scl_root_value(
                        document, root.reference, *root.root_value,
                        mapped, mapping_error)) {
                    root.mapping_error = std::move(mapping_error);
                    ++result.failed_root_count;
                    result.diagnostics.push_back(
                        "FC-root SCL mapping rejected for " + root.reference.reference() +
                        ": " + root.mapping_error);
                } else {
                    root.mapped_leaves = std::move(mapped);
                    ++result.successful_root_count;
                    result.mapped_leaf_count += root.mapped_leaves.size();
                }
                batch_result.roots.push_back(std::move(root));
            }
            result.batches.push_back(std::move(batch_result));
        }

        result.associated_after_snapshot = association_.associated();
        if (!result.associated_after_snapshot) {
            throw MmsSclAssistedConnectError(
                "MMS association did not remain active after the initial SCL snapshot.");
        }
        return result;
    }

private:
    MmsAssociationRuntime& association_;
};

} // namespace ar::iec61850::mms
