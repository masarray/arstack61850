// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"

#include "ariec61850/mms/services.hpp"

#include <algorithm>
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
        options.maximum_names_per_domain == 0U) {
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
           << endpoint.port << ", domains=" << domain_count()
           << ", variables=" << variable_count()
           << ", DataSets=" << report_inventory.data_sets.size()
           << ", RCBs=" << report_inventory.report_controls.size()
           << ", typeProbes=" << successful_types << '/' << variable_types.size()
           << ", directoryReads=" << successful_directories << '/'
           << data_set_directories.size()
           << ", rcbReads=" << successful_controls << '/'
           << report_controls.size()
           << ", diagnostics=" << diagnostics.size() << '.';
    return stream.str();
}

MmsLiveDiscoveryClient::MmsLiveDiscoveryClient(MmsAssociationRuntime& association)
    : association_{association} {}

std::vector<std::string> MmsLiveDiscoveryClient::get_name_list(
    const MmsGetNameListObjectClass object_class,
    const std::string& domain,
    const MmsLiveDiscoveryOptions& options,
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
        const auto exchange = association_.exchange_confirmed(
            encoded, request.invoke_id, stop_token);
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

    const auto domains = get_name_list(
        MmsGetNameListObjectClass::domain, {}, options, stop_token);
    if (domains.size() > options.maximum_domains) {
        throw MmsLiveDiscoveryError(
            "Domain discovery exceeded the configured domain bound.");
    }

    for (const auto& domain : domains) {
        try {
            result.names.domain_variables.emplace(
                domain,
                get_name_list(
                    MmsGetNameListObjectClass::named_variable,
                    domain,
                    options,
                    stop_token));
        } catch (const std::exception& exception) {
            if (!options.continue_on_optional_probe_error) {
                throw;
            }
            result.names.domain_variables.emplace(domain, std::vector<std::string>{});
            result.diagnostics.push_back(optional_probe_error(
                "NamedVariable", domain, exception));
        }

        try {
            result.names.domain_variable_lists.emplace(
                domain,
                get_name_list(
                    MmsGetNameListObjectClass::named_variable_list,
                    domain,
                    options,
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

    result.report_inventory = MmsReportInventoryBuilder::build(result.names);

    if (options.probe_variable_types) {
        std::size_t probe_count = 0U;
        for (const auto& [domain, variables] : result.names.domain_variables) {
            for (const auto& item : variables) {
                if (probe_count >= options.maximum_variable_type_probes) {
                    break;
                }
                ++probe_count;

                MmsVariableTypeEvidence evidence;
                evidence.variable = MmsObjectName::domain_specific(domain, item);
                try {
                    MmsVariableAccessAttributesRequest request;
                    request.invoke_id = association_.next_invoke_id();
                    request.name = evidence.variable;
                    const auto encoded = MmsServiceCodec::
                        encode_variable_access_attributes_request_p_data(
                            request,
                            association_.negotiated().presentation_context_id);
                    const auto exchange = association_.exchange_confirmed(
                        encoded, request.invoke_id, stop_token);
                    const auto payload = response_payload(
                        exchange, "GetVariableAccessAttributes");
                    evidence.attributes = MmsServiceCodec::
                        decode_variable_access_attributes_response(
                            payload, request.invoke_id);
                } catch (const std::exception& exception) {
                    evidence.error = exception.what();
                    result.diagnostics.push_back(optional_probe_error(
                        "Variable type", evidence.variable.reference(), exception));
                    if (!options.continue_on_optional_probe_error) {
                        throw;
                    }
                }
                result.variable_types.push_back(std::move(evidence));
            }
            if (probe_count >= options.maximum_variable_type_probes) {
                break;
            }
        }
    }

    if (options.read_data_set_directories) {
        const std::size_t count = std::min(
            options.maximum_data_set_directories,
            result.report_inventory.data_sets.size());
        result.data_set_directories.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            MmsDataSetDirectoryEvidence evidence;
            evidence.candidate = result.report_inventory.data_sets[index];
            try {
                MmsDataSetDirectoryRequest request;
                request.invoke_id = association_.next_invoke_id();
                request.data_set_name = MmsDataSetDirectoryCodec::parse_data_set_reference(
                    evidence.candidate.reference);
                const auto encoded = MmsDataSetDirectoryCodec::encode_request_p_data(
                    request, association_.negotiated().presentation_context_id);
                const auto exchange = association_.exchange_confirmed(
                    encoded, request.invoke_id, stop_token);
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
    }

    if (options.probe_report_controls) {
        const std::size_t count = std::min(
            options.maximum_report_control_probes,
            result.report_inventory.report_controls.size());
        result.report_controls.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
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
                const auto exchange = association_.exchange_confirmed(
                    encoded, invoke_id, stop_token);
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
