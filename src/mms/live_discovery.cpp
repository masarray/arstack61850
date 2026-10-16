// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"

#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ar::iec61850::mms {
namespace {

void validate_options(const MmsLiveDiscoveryOptions& options) {
    if (options.maximum_pages_per_query == 0U ||
        options.maximum_domains == 0U ||
        options.maximum_names_per_domain == 0U ||
        options.max_concurrent_name_list_chains == 0U ||
        options.max_concurrent_name_list_chains > 8U) {
        throw std::invalid_argument("Live MMS discovery name-list limits are invalid.");
    }
    if (options.probe_variable_types &&
        options.maximum_variable_type_probes == 0U) {
        throw std::invalid_argument(
            "Live MMS discovery variable-type probe limit must be positive.");
    }
    if (options.read_data_set_directories &&
        options.maximum_data_set_directories == 0U) {
        throw std::invalid_argument(
            "Live MMS discovery DataSet-directory limit must be positive.");
    }
    if (options.probe_report_controls &&
        options.maximum_report_control_probes == 0U) {
        throw std::invalid_argument(
            "Live MMS discovery report-control probe limit must be positive.");
    }
    if (options.use_verified_data_set_type_hints &&
        (!options.probe_variable_types || !options.read_data_set_directories ||
         options.maximum_data_set_type_hint_roots == 0U)) {
        throw std::invalid_argument(
            "Verified DataSet type closure requires type and directory reads "
            "and a positive extra-LN budget.");
    }
}

[[nodiscard]] std::string object_class_name(
    const MmsGetNameListObjectClass object_class) {
    switch (object_class) {
    case MmsGetNameListObjectClass::domain:
        return "Domain";
    case MmsGetNameListObjectClass::named_variable:
        return "NamedVariable";
    case MmsGetNameListObjectClass::named_variable_list:
        return "NamedVariableList";
    }
    return "Unknown";
}

[[nodiscard]] std::string service_failure(
    const MmsConfirmedExchangeResult& exchange,
    const std::string_view operation) {
    std::ostringstream stream;
    stream << operation << " returned MMS PDU kind="
           << static_cast<int>(exchange.envelope.kind);
    if (exchange.envelope.invoke_id) {
        stream << ", invokeID=" << *exchange.envelope.invoke_id;
    }
    if (exchange.envelope.service_tag) {
        stream << ", serviceTag=" << *exchange.envelope.service_tag;
    }
    stream << '.';
    return stream.str();
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const MmsConfirmedExchangeResult& exchange,
    const std::string_view operation) {
    if (exchange.envelope.kind != MmsPduKind::confirmed_response) {
        throw MmsLiveDiscoveryError(service_failure(exchange, operation));
    }
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    if (!exchange.envelope.mms_payload.empty()) {
        return exchange.envelope.mms_payload;
    }
    throw MmsLiveDiscoveryError(
        std::string{operation} + " returned no decodable MMS response payload.");
}

[[nodiscard]] std::vector<std::string> select_report_attributes(
    const MmsReportControlCandidate& candidate,
    const MmsLiveDiscoveryOptions& options) {
    std::vector<std::string> selected;
    selected.reserve(options.report_control_attributes.size());
    for (const auto& requested : options.report_control_attributes) {
        if (std::find(
                candidate.attributes.begin(),
                candidate.attributes.end(),
                requested) != candidate.attributes.end()) {
            selected.push_back(requested);
        }
    }
    return selected;
}

[[nodiscard]] std::string optional_probe_error(
    const std::string_view category,
    const std::string& reference,
    const std::exception& exception) {
    return std::string{category} + " probe failed for " + reference +
           ": " + exception.what();
}

// Mirrors ARIEC61850 LiveIedVariableTypeProbePlanner: probe one MMS type tree at
// each Logical Node root instead of probing every LN$FC$DO$DA leaf. Standards-
// shaped servers usually advertise the Logical Node root directly (for example
// "GGIO1"), while older/flat profiles may advertise only aliases such as
// "GGIO1$ST$Ind1$stVal". Accept both forms and de-duplicate by domain/LN so a
// hierarchical TypeSpecification is still requested exactly once per LN.
[[nodiscard]] std::vector<MmsObjectName> build_logical_node_type_probe_candidates(
    const MmsDiscoverySnapshot& snapshot,
    const std::size_t maximum_probes) {
    std::vector<MmsObjectName> candidates;
    candidates.reserve(std::min<std::size_t>(maximum_probes, 256U));
    std::set<std::string, std::less<>> seen;

    for (const auto& [domain, variables] : snapshot.domain_variables) {
        for (const auto& item : variables) {
            if (item.empty()) continue;
            const auto separator = item.find('$');
            if (separator == 0U) continue;
            const auto logical_node = separator == std::string::npos
                ? item
                : item.substr(0U, separator);
            if (logical_node.empty()) continue;
            // IEC 61850/MMS references are case-sensitive identifiers.
            const auto identity = domain + "\n" + logical_node;
            if (!seen.insert(identity).second) {
                continue;
            }
            candidates.push_back(
                MmsObjectName::domain_specific(domain, logical_node));
            if (candidates.size() >= maximum_probes) {
                return candidates;
            }
        }
    }
    return candidates;
}


struct VerifiedTypeHintPlan final {
    std::vector<MmsObjectName> roots;
    bool budget_exceeded{};
};

// Trust only observed, non-deletable MMS DataSet directories, never an SCL
// declaration or a guessed LN name. An exact FC root proves a candidate for
// a targeted type probe; its type remains unknown until GVA actually succeeds.
// Reject the whole optional extension on budget overflow, preserving the
// original NameList-only discovery unchanged.
[[nodiscard]] VerifiedTypeHintPlan plan_verified_data_set_type_roots(
    const MmsDiscoverySnapshot& snapshot,
    const std::vector<MmsDataSetDirectoryEvidence>& directories,
    const std::vector<MmsObjectName>& existing_roots,
    const std::size_t maximum_total,
    const std::size_t maximum_extra) {
    VerifiedTypeHintPlan plan;
    std::set<std::string, std::less<>> seen;
    for (const auto& root : existing_roots) seen.insert(root.reference());

    for (const auto& evidence : directories) {
        if (!evidence.success() || evidence.directory->deletable ||
            snapshot.domain_variables.find(evidence.candidate.domain) ==
                snapshot.domain_variables.end()) {
            continue;
        }
        for (const auto& member : evidence.directory->members) {
            const auto& name = member.object_name;
            if (name.kind != MmsObjectNameKind::domain_specific ||
                name.domain != evidence.candidate.domain) continue;
            const auto first = name.item.find('$');
            if (first == std::string::npos || first == 0U) continue;
            const auto second = name.item.find('$', first + 1U);
            if (second == std::string::npos || second == first + 1U ||
                second + 1U >= name.item.size()) continue;
            const auto fc = std::string_view{name.item}.substr(
                first + 1U, second - first - 1U);
            if (fc != "ST" && fc != "MX") continue;
            if (member.functional_constraint != fc ||
                (!member.logical_node.empty() &&
                 member.logical_node != name.item.substr(0U, first)) ||
                (!member.mms_reference.empty() &&
                 member.mms_reference != name.reference())) {
                continue;
            }
            const auto root = MmsObjectName::domain_specific(
                name.domain, name.item.substr(0U, first));
            if (!seen.insert(root.reference()).second) continue;
            if (plan.roots.size() >= maximum_extra ||
                existing_roots.size() + plan.roots.size() >= maximum_total) {
                plan.roots.clear();
                plan.budget_exceeded = true;
                return plan;
            }
            plan.roots.push_back(root);
        }
    }
    return plan;
}

} // namespace

std::size_t MmsLiveDiscoveryResult::domain_count() const noexcept {
    return names.domain_variables.size();
}

std::size_t MmsLiveDiscoveryResult::variable_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& [domain, variables] : names.domain_variables) {
        static_cast<void>(domain);
        count += variables.size();
    }
    return count;
}

std::size_t MmsLiveDiscoveryResult::variable_list_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& [domain, variable_lists] : names.domain_variable_lists) {
        static_cast<void>(domain);
        count += variable_lists.size();
    }
    return count;
}

std::string MmsLiveDiscoveryResult::summary() const {
    const auto successful_types = static_cast<std::size_t>(std::count_if(
        variable_types.begin(), variable_types.end(),
        [](const auto& evidence) { return evidence.success(); }));
    const auto successful_directories = static_cast<std::size_t>(std::count_if(
        data_set_directories.begin(), data_set_directories.end(),
        [](const auto& evidence) { return evidence.success(); }));
    const auto successful_controls = static_cast<std::size_t>(std::count_if(
        report_controls.begin(), report_controls.end(),
        [](const auto& evidence) { return evidence.success(); }));

    std::ostringstream stream;
    stream << "Live read-only MMS discovery: endpoint=" << endpoint.host << ':'
           << endpoint.port << ", associationProfile="
           << (association_profile.empty() ? std::string{"unknown"} : association_profile)
           << ", associationAttempts=" << association_attempts.size()
           << ", domains=" << domain_count()
           << ", variables=" << variable_count()
           << ", DataSets=" << report_inventory.data_sets.size()
           << ", RCBs=" << report_inventory.report_controls.size()
           << ", typeProbes=" << successful_types << '/' << variable_types.size()
           << ", directoryReads=" << successful_directories << '/'
           << data_set_directories.size()
           << ", rcbReads=" << successful_controls << '/'
           << report_controls.size()
           << ", wireRequests=" << wire_budget.total()
           << " [NameList=" << wire_budget.get_name_list
           << ", GVA=" << wire_budget.get_variable_access_attributes
           << ", DataSetDirectory=" << wire_budget.get_named_variable_list_attributes
           << ", RCBRead=" << wire_budget.read_report_controls << ']'
           << ", nameListPeakOutstanding="
           << wire_budget.name_list_peak_outstanding
           << ", diagnostics=" << diagnostics.size() << '.';
    return stream.str();
}

namespace {

// Capture the per-exchange send receipt even when the response times out.
// This scoped counter is independent of shared event history and keeps the
// correct service category if a later optional probe fails before sending.
struct ConfirmedSendCount final {
    std::size_t& service_count;
    bool sent{};
    ~ConfirmedSendCount() {
        if (sent) ++service_count;
    }
};

[[nodiscard]] MmsConfirmedExchangeResult exchange_counted(
    MmsAssociationRuntime& association,
    const std::span<const std::uint8_t> request,
    const std::uint32_t invoke_id,
    const std::stop_token stop_token,
    std::size_t& service_count) {
    ConfirmedSendCount receipt{service_count};
    return association.exchange_confirmed(request, invoke_id, stop_token, &receipt.sent);
}

} // namespace

MmsLiveDiscoveryClient::MmsLiveDiscoveryClient(MmsAssociationRuntime& association)
    : association_{association} {}

std::vector<std::string> MmsLiveDiscoveryClient::get_name_list(
    const MmsGetNameListObjectClass object_class,
    const std::string& domain,
    const MmsLiveDiscoveryOptions& options,
    MmsLiveDiscoveryWireBudget& wire_budget,
    const std::stop_token stop_token) {
    std::vector<std::string> names;
    std::set<std::string, std::less<>> seen;
    std::string continue_after;

    for (std::size_t page = 0U; page < options.maximum_pages_per_query; ++page) {
        MmsGetNameListRequest request;
        request.invoke_id = association_.next_invoke_id();
        request.object_class = object_class;
        request.scope = domain.empty()
            ? MmsObjectScopeKind::vmd_specific
            : MmsObjectScopeKind::domain_specific;
        request.domain_id = domain;
        request.continue_after = continue_after;

        const auto encoded = MmsServiceCodec::encode_get_name_list_request_p_data(
            request, association_.negotiated().presentation_context_id);
        const auto exchange = exchange_counted(
            association_, encoded, request.invoke_id, stop_token,
            wire_budget.get_name_list);
        const auto payload = response_payload(exchange, "GetNameList");
        const auto response = MmsServiceCodec::decode_get_name_list_response(
            payload, request.invoke_id);

        const std::size_t count_before = names.size();
        for (const auto& name : response.names) {
            if (seen.insert(name).second) {
                if (names.size() >= options.maximum_names_per_domain) {
                    throw MmsLiveDiscoveryError(
                        "GetNameList " + object_class_name(object_class) + '/' +
                        (domain.empty() ? std::string{"VMD"} : domain) +
                        " exceeded the configured name bound.");
                }
                names.push_back(name);
            }
        }

        if (!response.more_follows) {
            return names;
        }
        if (response.names.empty() || names.size() == count_before) {
            throw MmsLiveDiscoveryError(
                "GetNameList " + object_class_name(object_class) + '/' +
                (domain.empty() ? std::string{"VMD"} : domain) +
                " reported moreFollows without forward progress.");
        }
        continue_after = response.names.back();
    }

    throw MmsLiveDiscoveryError(
        "GetNameList " + object_class_name(object_class) + '/' +
        (domain.empty() ? std::string{"VMD"} : domain) +
        " exceeded the configured page bound.");
}

void MmsLiveDiscoveryClient::get_name_lists_pipelined(
    const std::vector<std::string>& domains,
    const MmsLiveDiscoveryOptions& options,
    MmsLiveDiscoveryResult& result,
    const std::stop_token stop_token,
    const std::size_t window) {
    struct Chain final {
        std::size_t domain_index{};
        MmsGetNameListObjectClass object_class{};
        std::vector<std::string> names;
        std::set<std::string, std::less<>> seen;
        std::string continue_after;
        std::size_t pages{};
    };
    // Each domain/class is its own sequential continuation chain; only
    // independent chains may be outstanding together. All socket ownership
    // stays on this one call stack and one MMS association.
    std::vector<Chain> chains;
    chains.reserve(domains.size() * 2U);
    std::deque<std::size_t> ready;
    for (std::size_t i = 0U; i < domains.size(); ++i) {
        for (const auto cls : {MmsGetNameListObjectClass::named_variable,
                               MmsGetNameListObjectClass::named_variable_list}) {
            ready.push_back(chains.size());
            chains.push_back(Chain{i, cls});
        }
    }

    auto publish = [&](Chain& chain) {
        const auto& domain = domains[chain.domain_index];
        if (chain.object_class == MmsGetNameListObjectClass::named_variable) {
            result.names.domain_variables.at(domain) = std::move(chain.names);
        } else {
            result.names.domain_variable_lists.emplace(domain, std::move(chain.names));
        }
    };
    auto failed = [&](const Chain& chain, const std::exception& error) {
        if (!options.continue_on_optional_probe_error) throw;
        const auto& domain = domains[chain.domain_index];
        result.diagnostics.push_back(
            optional_probe_error(object_class_name(chain.object_class), domain, error));
        if (chain.object_class == MmsGetNameListObjectClass::named_variable_list) {
            result.names.domain_variable_lists.try_emplace(domain);
        }
    };

    while (!ready.empty() && association_.associated()) {
        std::vector<std::size_t> chosen;
        std::vector<MmsConfirmedBatchRequest> requests;
        chosen.reserve(window);
        requests.reserve(window);
        while (!ready.empty() && chosen.size() < window) {
            const auto index = ready.front();
            ready.pop_front();
            auto& chain = chains[index];
            MmsGetNameListRequest request;
            request.invoke_id = association_.next_invoke_id();
            request.object_class = chain.object_class;
            request.scope = MmsObjectScopeKind::domain_specific;
            request.domain_id = domains[chain.domain_index];
            request.continue_after = chain.continue_after;
            ++chain.pages;
            chosen.push_back(index);
            requests.push_back(MmsConfirmedBatchRequest{
                MmsServiceCodec::encode_get_name_list_request_p_data(
                    request, association_.negotiated().presentation_context_id),
                request.invoke_id});
        }

        // Preserve actual wire send receipts even if the batch times out or a
        // later segmented write fails. No extra network request for metrics.
        struct Receipt final {
            MmsLiveDiscoveryWireBudget& budget;
            std::size_t sent{};
            ~Receipt() {
                budget.get_name_list += sent;
                budget.name_list_peak_outstanding =
                    std::max(budget.name_list_peak_outstanding, sent);
            }
        } receipt{result.wire_budget};
        std::vector<MmsConfirmedExchangeResult> exchanges;
        try {
            if (requests.size() == 1U) {
                bool sent = false;
                try {
                    exchanges.push_back(association_.exchange_confirmed(
                        requests.front().presentation_payload,
                        requests.front().invoke_id, stop_token, &sent));
                } catch (...) {
                    receipt.sent = sent ? 1U : 0U;
                    throw;
                }
                receipt.sent = sent ? 1U : 0U;
            } else {
                exchanges = association_.exchange_confirmed_batch(
                    requests, stop_token, &receipt.sent);
            }
        } catch (const std::exception& error) {
            if (!options.continue_on_optional_probe_error) throw;
            // No unconfirmed page is published. Preserve completed prior
            // chains/domains and one concrete diagnostic for this batch.
            result.diagnostics.push_back(
                std::string{"NameList pipeline fault: "} + error.what());
            if (!association_.associated()) break;
            // A non-fatal local failure is terminal for this selected group;
            // other unsent groups are not guessed or fabricated.
            break;
        }

        for (std::size_t pos = 0U; pos < chosen.size(); ++pos) {
            auto& chain = chains[chosen[pos]];
            try {
                const auto payload = response_payload(exchanges[pos], "GetNameList");
                const auto response = MmsServiceCodec::decode_get_name_list_response(
                    payload, requests[pos].invoke_id);
                const auto count_before = chain.names.size();
                for (const auto& name : response.names) {
                    if (chain.seen.insert(name).second) {
                        if (chain.names.size() >= options.maximum_names_per_domain) {
                            throw MmsLiveDiscoveryError(
                                "GetNameList name bound exceeded for " +
                                domains[chain.domain_index]);
                        }
                        chain.names.push_back(name);
                    }
                }
                if (!response.more_follows) {
                    publish(chain);
                } else {
                    if (response.names.empty() || chain.names.size() == count_before) {
                        throw MmsLiveDiscoveryError(
                            "GetNameList moreFollows without forward progress.");
                    }
                    if (chain.pages >= options.maximum_pages_per_query) {
                        throw MmsLiveDiscoveryError(
                            "GetNameList continuation page bound exceeded.");
                    }
                    chain.continue_after = response.names.back();
                    ready.push_back(chosen[pos]);
                }
            } catch (const std::exception& error) {
                failed(chain, error);
            }
        }
    }
}

MmsLiveDiscoveryResult MmsLiveDiscoveryClient::discover(
    const MmsLiveDiscoveryOptions& options,
    const std::stop_token stop_token) {
    validate_options(options);
    if (!association_.associated()) {
        throw MmsLiveDiscoveryError(
            "Live MMS discovery requires an active MMS association.");
    }

    MmsLiveDiscoveryResult result;
    result.endpoint = association_.endpoint();
    result.association_profile = association_.active_association_profile();
    result.association_attempts = association_.association_attempts();

    const auto domains = get_name_list(
        MmsGetNameListObjectClass::domain, {}, options, result.wire_budget, stop_token);
    if (domains.size() > options.maximum_domains) {
        throw MmsLiveDiscoveryError(
            "Domain discovery exceeded the configured domain bound.");
    }

    // VMD Domain names are already observed wire evidence, even if a later
    // per-domain GetNameList faults. Keep every proven domain in the canonical
    // identity inventory without inventing LN variables or DataSet contents.
    for (const auto& domain : domains) {
        result.names.domain_variables.try_emplace(domain);
    }
    // VMD enumeration is always serial. A strict association must prove its
    // calling window; tolerant/unknown peers remain on the original path.
    result.wire_budget.name_list_peak_outstanding =
        result.wire_budget.get_name_list == 0U ? 0U : 1U;
    const auto name_window = std::min<std::size_t>(
        options.max_concurrent_name_list_chains,
        std::min<std::size_t>(8U, association_.verified_outstanding_calling()));
    if (name_window > 1U) {
        get_name_lists_pipelined(domains, options, result, stop_token, name_window);
    } else {
    for (const auto& domain : domains) {
        // A failed confirmed exchange closes the association. Keep earlier
        // successful domain evidence; never manufacture additional failed
        // optional probe entries for an already-disconnected peer.
        if (!association_.associated()) break;
        try {
            result.names.domain_variables.at(domain) = get_name_list(
                MmsGetNameListObjectClass::named_variable,
                domain,
                options,
                result.wire_budget,
                stop_token);
        } catch (const std::exception& exception) {
            if (!options.continue_on_optional_probe_error) {
                throw;
            }
            // Seeded VMD domain remains with no claimed variable evidence.
            result.diagnostics.push_back(optional_probe_error(
                "NamedVariable", domain, exception));
        }
        if (!association_.associated()) break;

        try {
            result.names.domain_variable_lists.emplace(
                domain,
                get_name_list(
                    MmsGetNameListObjectClass::named_variable_list,
                    domain,
                    options,
                    result.wire_budget,
                    stop_token));
        } catch (const std::exception& exception) {
            if (!options.continue_on_optional_probe_error) {
                throw;
            }
            result.names.domain_variable_lists.emplace(
                domain, std::vector<std::string>{});
            result.diagnostics.push_back(optional_probe_error(
                "NamedVariableList", domain, exception));
        }
    }

    }
    result.report_inventory = MmsReportInventoryBuilder::build(result.names);

    const auto readDataSetDirectories = [&] {
        if (!options.read_data_set_directories || !association_.associated()) return;
        const std::size_t count = std::min(
            options.maximum_data_set_directories,
            result.report_inventory.data_sets.size());
        result.data_set_directories.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            if (!association_.associated()) break;
            MmsDataSetDirectoryEvidence evidence;
            evidence.candidate = result.report_inventory.data_sets[index];
            try {
                MmsDataSetDirectoryRequest request;
                request.invoke_id = association_.next_invoke_id();
                request.data_set_name = MmsDataSetDirectoryCodec::parse_data_set_reference(
                    evidence.candidate.reference);
                const auto encoded = MmsDataSetDirectoryCodec::encode_request_p_data(
                    request, association_.negotiated().presentation_context_id);
                const auto exchange = exchange_counted(
                    association_, encoded, request.invoke_id, stop_token,
                    result.wire_budget.get_named_variable_list_attributes);
                const auto payload = response_payload(
                    exchange, "GetNamedVariableListAttributes");
                evidence.directory = MmsDataSetDirectoryCodec::decode_response(
                    payload, request.invoke_id);
            } catch (const std::exception& exception) {
                evidence.error = exception.what();
                result.diagnostics.push_back(optional_probe_error(
                    "DataSet directory", evidence.candidate.reference, exception));
                if (!options.continue_on_optional_probe_error) {
                    throw;
                }
            }
            result.data_set_directories.push_back(std::move(evidence));
        }
    };

    const auto probeVariableTypes = [&] {
        if (!options.probe_variable_types || !association_.associated()) return;
        auto candidates = build_logical_node_type_probe_candidates(
            result.names, options.maximum_variable_type_probes);
        if (options.use_verified_data_set_type_hints) {
            const auto extra = plan_verified_data_set_type_roots(
                result.names, result.data_set_directories, candidates,
                options.maximum_variable_type_probes,
                options.maximum_data_set_type_hint_roots);
            if (extra.budget_exceeded) {
                result.diagnostics.push_back(
                    "Verified DataSet LN type-root budget exceeded; "
                    "optional extra probes skipped without partial selection.");
            } else {
                candidates.insert(
                    candidates.end(), extra.roots.begin(), extra.roots.end());
            }
        }
        result.variable_types.reserve(candidates.size());

        for (const auto& candidate : candidates) {
            if (!association_.associated()) break;
            MmsVariableTypeEvidence evidence;
            evidence.variable = candidate;
            try {
                MmsVariableAccessAttributesRequest request;
                request.invoke_id = association_.next_invoke_id();
                request.name = evidence.variable;
                const auto encoded = MmsServiceCodec::
                    encode_variable_access_attributes_request_p_data(
                        request,
                        association_.negotiated().presentation_context_id);
                const auto exchange = exchange_counted(
                    association_, encoded, request.invoke_id, stop_token,
                    result.wire_budget.get_variable_access_attributes);
                const auto payload = response_payload(
                    exchange, "GetVariableAccessAttributes");
                evidence.attributes = MmsServiceCodec::
                    decode_variable_access_attributes_response(
                        payload, request.invoke_id);
            } catch (const std::exception& exception) {
                evidence.error = exception.what();
                result.diagnostics.push_back(optional_probe_error(
                    "Logical-node variable type",
                    evidence.variable.reference(),
                    exception));
                if (!options.continue_on_optional_probe_error) {
                    throw;
                }
            }
            result.variable_types.push_back(std::move(evidence));
        }
    };

    // Only the opt-in closure path changes phase order. Existing callers
    // retain their wire sequence and unchanged default request count.
    if (options.use_verified_data_set_type_hints) {
        readDataSetDirectories();
        probeVariableTypes();
    } else {
        probeVariableTypes();
        readDataSetDirectories();
    }

    if (options.probe_report_controls && association_.associated()) {
        const std::size_t count = std::min(
            options.maximum_report_control_probes,
            result.report_inventory.report_controls.size());
        result.report_controls.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            if (!association_.associated()) break;
            MmsReportControlEvidence evidence;
            evidence.candidate = result.report_inventory.report_controls[index];
            evidence.requested_attributes = select_report_attributes(
                evidence.candidate, options);
            if (evidence.requested_attributes.empty()) {
                evidence.error = "No configured read-only RCB attributes were discovered.";
                result.diagnostics.push_back(
                    evidence.candidate.reference + ": " + evidence.error);
                result.report_controls.push_back(std::move(evidence));
                continue;
            }

            try {
                const auto invoke_id = association_.next_invoke_id();
                const auto request = MmsReportControlStateMapper::build_read_request(
                    invoke_id,
                    evidence.candidate,
                    evidence.requested_attributes);
                const auto encoded = MmsServiceCodec::encode_read_request_p_data(
                    request, association_.negotiated().presentation_context_id);
                const auto exchange = exchange_counted(
                    association_, encoded, invoke_id, stop_token,
                    result.wire_budget.read_report_controls);
                const auto payload = response_payload(
                    exchange, "Read RCB attributes");
                const auto response = MmsServiceCodec::decode_read_response(
                    payload, invoke_id);
                evidence.state = MmsReportControlStateMapper::map_read_response(
                    evidence.candidate,
                    evidence.requested_attributes,
                    response);
            } catch (const std::exception& exception) {
                evidence.error = exception.what();
                result.diagnostics.push_back(optional_probe_error(
                    "Report control", evidence.candidate.reference, exception));
                if (!options.continue_on_optional_probe_error) {
                    throw;
                }
            }
            result.report_controls.push_back(std::move(evidence));
        }
    }

    return result;
}

MmsTcpLiveDiscoverySession::MmsTcpLiveDiscoverySession(
    TcpMmsTransportOptions transport_options,
    MmsAssociationOptions association_options)
    : transport_{std::move(transport_options)},
      association_{transport_, std::move(association_options)},
      discovery_{association_} {}

void MmsTcpLiveDiscoverySession::connect(
    const MmsEndpoint& endpoint,
    const std::stop_token stop_token) {
    association_.connect(endpoint, stop_token);
}

MmsLiveDiscoveryResult MmsTcpLiveDiscoverySession::discover(
    const MmsLiveDiscoveryOptions& options,
    const std::stop_token stop_token) {
    return discovery_.discover(options, stop_token);
}

void MmsTcpLiveDiscoverySession::disconnect(
    const std::stop_token stop_token) noexcept {
    association_.disconnect(stop_token);
}

} // namespace ar::iec61850::mms
