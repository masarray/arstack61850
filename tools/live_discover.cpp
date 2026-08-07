// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/rcb_availability.hpp"
#include "ariec61850/mms/rcb_selection.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ar::iec61850;

enum class RcbPlanMode : std::uint8_t {
    none,
    dynamic_data_set,
    static_data_set,
};

struct RcbPlanCliOptions final {
    RcbPlanMode mode{RcbPlanMode::none};
    std::string preferred_rcb_reference;
    std::string preferred_logical_device;
    std::string preferred_data_set_reference;
    bool strict_rcb{};
    bool allow_urcb_fallback{true};
    bool allow_polling_fallback{true};
    std::vector<std::string> excluded_rcb_references;
    std::size_t maximum_candidates_to_print{10U};

    [[nodiscard]] bool requested() const noexcept {
        return mode != RcbPlanMode::none;
    }

    [[nodiscard]] bool has_policy_options() const noexcept {
        return !preferred_rcb_reference.empty() ||
               !preferred_logical_device.empty() ||
               !preferred_data_set_reference.empty() ||
               strict_rcb || !allow_urcb_fallback || !allow_polling_fallback ||
               !excluded_rcb_references.empty() || maximum_candidates_to_print != 10U;
    }
};

struct RcbAvailabilityCliOptions final {
    bool requested{};
    std::size_t maximum_snapshots_to_print{10U};
};

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) output << '?';
            else output << character;
            break;
        }
    }
    return output.str();
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::size_t parse_limit(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] RcbPlanMode parse_rcb_plan_mode(const std::string& value) {
    if (value == "dynamic" || value == "dynamic-dataset") {
        return RcbPlanMode::dynamic_data_set;
    }
    if (value == "static" || value == "static-dataset") {
        return RcbPlanMode::static_data_set;
    }
    throw std::invalid_argument(
        "--rcb-plan requires 'dynamic' or 'static'.");
}

[[nodiscard]] std::string_view text_or_dash(const std::string& value) noexcept {
    return value.empty() ? std::string_view{"-"} : std::string_view{value};
}

[[nodiscard]] std::size_t availability_count(
    const mms::MmsRcbSelectionEvidence& selection,
    const mms::MmsRcbAvailabilityKind kind) {
    return static_cast<std::size_t>(std::count_if(
        selection.candidates.begin(), selection.candidates.end(),
        [kind](const auto& candidate) { return candidate.availability == kind; }));
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_live_discover <host> [port] [options]\n"
        << "Options:\n"
        << "  --json                 Emit compact discovery summary JSON.\n"
        << "  --model-json           Emit C#-compatible live-ied-model-v1 JSON.\n"
        << "  --manifest             Emit deterministic parity manifest.\n"
        << "  --ied-name NAME        Optional explicit IED identity override.\n"
        << "  --no-types             Skip GetVariableAccessAttributes probes.\n"
        << "  --no-datasets          Skip DataSet directory reads.\n"
        << "  --no-rcb               Skip read-only RCB attribute probes.\n"
        << "  --max-types N          Bound all type probes.\n"
        << "  --max-datasets N       Bound DataSet directory reads.\n"
        << "  --max-rcb N            Bound RCB read probes.\n"
        << "  --timeout-ms N         Connect/request timeout in milliseconds.\n"
        << "Read-only Smart RCB planning:\n"
        << "  --rcb-plan MODE        Analyze RCB evidence as dynamic or static.\n"
        << "  --rcb-ld LD            Prefer this logical device for dynamic mode.\n"
        << "  --rcb-dataset REF      Prefer this DataSet for static mode.\n"
        << "  --preferred-rcb REF    Prefer this RCB reference.\n"
        << "  --strict-rcb           Do not fall back from the preferred RCB.\n"
        << "  --no-urcb-fallback     Exclude URCB fallback candidates.\n"
        << "  --no-polling-fallback  Record polling fallback as disallowed.\n"
        << "  --rcb-exclude REF      Exclude an RCB; may be repeated.\n"
        << "  --max-rcb-candidates N Limit human candidate output (default 10).\n"
        << "Read-only operational RCB evidence:\n"
        << "  --rcb-availability     Evaluate C#-parity ownership/availability.\n"
        << "  --max-rcb-availability N\n"
        << "                         Limit human availability output (default 10).\n"
        << "\n"
        << "RCB planning/availability are evidence-only. They never send Write, Resv,\n"
        << "RptEna, GI, or dynamic DataSet mutation requests.\n";
}

void print_rcb_plan(
    const mms::MmsRcbSelectionEvidence& selection,
    const std::size_t maximum_candidates) {
    std::cout << selection.summary() << '\n'
              << "RCB candidate availability: static="
              << availability_count(selection, mms::MmsRcbAvailabilityKind::available_static)
              << ", dynamicEmpty="
              << availability_count(
                     selection, mms::MmsRcbAvailabilityKind::available_dynamic_empty)
              << ", busyEnabled="
              << availability_count(selection, mms::MmsRcbAvailabilityKind::busy_enabled)
              << ", busyReserved="
              << availability_count(selection, mms::MmsRcbAvailabilityKind::busy_reserved)
              << ", unknown="
              << availability_count(selection, mms::MmsRcbAvailabilityKind::unknown_needs_probe)
              << ", notApplicable="
              << availability_count(selection, mms::MmsRcbAvailabilityKind::not_applicable)
              << ".\n";

    if (selection.selected()) {
        std::cout << "Selected RCB candidate: "
                  << selection.selected_rcb_reference << '\n';
    } else {
        std::cout << "Selected RCB candidate: -\n";
    }

    const auto count = std::min(maximum_candidates, selection.candidates.size());
    if (count != 0U) {
        std::cout << "RCB candidates (top " << count << " of "
                  << selection.candidates.size() << "):\n";
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& candidate = selection.candidates[index];
        std::cout << "  ["
                  << mms::mms_rcb_selection_decision_name(candidate.decision)
                  << "] " << candidate.reference
                  << " score=" << candidate.score
                  << " availability="
                  << mms::mms_rcb_availability_kind_name(candidate.availability)
                  << " mode=" << candidate.mode
                  << " DatSet=" << text_or_dash(candidate.data_set_reference)
                  << " RptEna=" << text_or_dash(candidate.report_enabled)
                  << " Resv=" << text_or_dash(candidate.reservation_state)
                  << " ResvTms=" << text_or_dash(candidate.reservation_time_seconds)
                  << " reason=" << candidate.reason << '\n';
    }

    if (!selection.warnings.empty()) {
        std::cout << "RCB planner warnings:\n";
        for (const auto& warning : selection.warnings) {
            std::cout << "  - " << warning << '\n';
        }
    }
    if (!selection.blockers.empty()) {
        std::cout << "RCB planner blockers:\n";
        for (const auto& blocker : selection.blockers) {
            std::cout << "  - " << blocker << '\n';
        }
    }
}

void print_rcb_operational_availability(
    const mms::MmsRcbOperationalAvailabilityResult& availability,
    const std::size_t maximum_snapshots) {
    std::cout << availability.summary() << '\n'
              << "RCB operational evidence: usedByCaller="
              << availability.count(
                     mms::MmsRcbOperationalAvailability::used_by_caller)
              << ", noDataSet="
              << availability.count(
                     mms::MmsRcbOperationalAvailability::no_data_set)
              << ", dataSetEmpty="
              << availability.count(
                     mms::MmsRcbOperationalAvailability::data_set_empty)
              << ", dataSetUnreadable="
              << availability.count(
                     mms::MmsRcbOperationalAvailability::data_set_unreadable)
              << ".\n";

    const auto count = std::min(maximum_snapshots, availability.report_controls.size());
    if (count != 0U) {
        std::cout << "Operational RCB snapshots (top " << count << " of "
                  << availability.report_controls.size() << "):\n";
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& snapshot = availability.report_controls[index];
        std::cout << "  ["
                  << mms::mms_rcb_operational_availability_name(snapshot.availability)
                  << '/' << mms::mms_rcb_operational_confidence_name(snapshot.confidence)
                  << "] " << snapshot.reference
                  << " mode=" << snapshot.mode
                  << " DatSet=" << text_or_dash(snapshot.data_set_reference)
                  << " RptEna=" << text_or_dash(snapshot.enabled_state)
                  << " Resv=" << text_or_dash(snapshot.reservation_state)
                  << " ResvTms=" << text_or_dash(snapshot.reservation_time_seconds)
                  << " Owner=" << text_or_dash(snapshot.owner)
                  << " members=" << snapshot.data_set_member_count
                  << " reason=" << snapshot.reason << '\n';
    }

    if (availability.count(mms::MmsRcbOperationalAvailability::no_data_set) != 0U) {
        std::cout
            << "  Note: NoDataSet is a populated/static-report availability result; "
            << "empty dynamic-slot eligibility is evaluated separately by "
            << "--rcb-plan dynamic.\n";
    }
    for (const auto& warning : availability.warnings) {
        std::cout << "  - " << warning << '\n';
    }
}

void print_human(
    const mms::MmsLiveDiscoveryResult& result,
    const mms::MmsLiveModelDocument& model,
    const std::optional<mms::MmsRcbSelectionEvidence>& rcb_selection,
    const std::optional<mms::MmsRcbOperationalAvailabilityResult>& rcb_availability,
    const std::size_t maximum_rcb_candidates,
    const std::size_t maximum_rcb_availability) {
    std::cout << result.summary() << '\n'
              << model.summary << '\n'
              << "IED identity: " << model.identity.ied_name
              << " (source=" << model.identity.source << ")\n"
              << "Model fingerprint: " << model.canonical_fingerprint_hex() << '\n';
    for (const auto& logical_device : model.logical_devices) {
        std::cout << "  LD " << logical_device.mms_domain
                  << " (alias=" << logical_device.instance << ")\n";
        for (const auto& logical_node : logical_device.logical_nodes) {
            std::cout << "    LN " << logical_node.name
                      << " class=" << logical_node.logical_node_class
                      << " DO=" << logical_node.data_objects.size() << '\n';
        }
    }
    if (!model.warnings.empty()) {
        std::cout << "Warnings:\n";
        for (const auto& warning : model.warnings) {
            std::cout << "  - [" << warning.code << "] " << warning.message << '\n';
        }
    }
    if (rcb_selection) {
        print_rcb_plan(*rcb_selection, maximum_rcb_candidates);
    }
    if (rcb_availability) {
        print_rcb_operational_availability(
            *rcb_availability, maximum_rcb_availability);
    }
}

void print_summary_json(
    const mms::MmsLiveDiscoveryResult& result,
    const mms::MmsLiveModelDocument& model,
    const std::optional<mms::MmsRcbSelectionEvidence>& rcb_selection,
    const std::optional<mms::MmsRcbOperationalAvailabilityResult>& rcb_availability) {
    std::cout << '{'
              << "\"endpoint\":\"" << json_escape(result.endpoint.host) << ':'
              << result.endpoint.port << "\","
              << "\"iedName\":\"" << json_escape(model.identity.ied_name) << "\","
              << "\"iedNameSource\":\"" << json_escape(model.identity.source) << "\","
              << "\"domains\":" << result.domain_count() << ','
              << "\"variables\":" << result.variable_count() << ','
              << "\"logicalDevices\":" << model.coverage.logical_device_count << ','
              << "\"logicalNodes\":" << model.coverage.logical_node_count << ','
              << "\"dataObjects\":" << model.coverage.data_object_count << ','
              << "\"dataAttributes\":" << model.coverage.data_attribute_count << ','
              << "\"dataSets\":" << model.coverage.data_set_count << ','
              << "\"reportControls\":" << model.coverage.report_control_count << ','
              << "\"exactMmsTypes\":" << model.coverage.exact_mms_type_count << ','
              << "\"fingerprint\":\"" << model.canonical_fingerprint_hex() << "\","
              << "\"partial\":" << (result.partial() ? "true" : "false") << ','
              << "\"summary\":\"" << json_escape(model.summary) << "\"";

    if (rcb_selection) {
        std::cout << ",\"rcbPlan\":{"
                  << "\"mode\":\""
                  << mms::mms_rcb_selection_mode_name(rcb_selection->mode) << "\","
                  << "\"selectedRcbReference\":\""
                  << json_escape(rcb_selection->selected_rcb_reference) << "\","
                  << "\"fallbackUsed\":"
                  << (rcb_selection->fallback_used ? "true" : "false") << ','
                  << "\"candidateCount\":" << rcb_selection->candidates.size() << ','
                  << "\"availableStatic\":"
                  << availability_count(
                         *rcb_selection, mms::MmsRcbAvailabilityKind::available_static) << ','
                  << "\"availableDynamicEmpty\":"
                  << availability_count(
                         *rcb_selection,
                         mms::MmsRcbAvailabilityKind::available_dynamic_empty) << ','
                  << "\"busyEnabled\":"
                  << availability_count(
                         *rcb_selection, mms::MmsRcbAvailabilityKind::busy_enabled) << ','
                  << "\"busyReserved\":"
                  << availability_count(
                         *rcb_selection, mms::MmsRcbAvailabilityKind::busy_reserved) << ','
                  << "\"unknownNeedsProbe\":"
                  << availability_count(
                         *rcb_selection,
                         mms::MmsRcbAvailabilityKind::unknown_needs_probe) << ','
                  << "\"warningCount\":" << rcb_selection->warnings.size() << ','
                  << "\"blockerCount\":" << rcb_selection->blockers.size()
                  << '}';
    }
    if (rcb_availability) {
        std::cout << ",\"rcbAvailability\":{"
                  << "\"total\":" << rcb_availability->report_controls.size() << ','
                  << "\"available\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::available) << ','
                  << "\"inUse\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::in_use) << ','
                  << "\"usedByCaller\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::used_by_caller) << ','
                  << "\"unknown\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::unknown) << ','
                  << "\"noDataSet\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::no_data_set) << ','
                  << "\"dataSetEmpty\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::data_set_empty) << ','
                  << "\"dataSetUnreadable\":"
                  << rcb_availability->count(
                         mms::MmsRcbOperationalAvailability::data_set_unreadable)
                  << '}';
    }
    std::cout << "}\n";
}

[[nodiscard]] std::optional<mms::MmsRcbSelectionEvidence> build_rcb_plan(
    const mms::MmsLiveDiscoveryResult& result,
    const RcbPlanCliOptions& cli_options) {
    if (!cli_options.requested()) {
        return std::nullopt;
    }

    if (cli_options.mode == RcbPlanMode::dynamic_data_set) {
        mms::MmsRcbDynamicSelectionOptions options;
        options.preferred_logical_device = cli_options.preferred_logical_device;
        options.preferred_rcb_reference = cli_options.preferred_rcb_reference;
        options.strict_rcb = cli_options.strict_rcb;
        options.allow_urcb_fallback = cli_options.allow_urcb_fallback;
        options.allow_polling_fallback = cli_options.allow_polling_fallback;
        options.excluded_rcb_references = cli_options.excluded_rcb_references;
        return mms::MmsRcbPoolSelector::build_dynamic_selection(result, options);
    }

    mms::MmsRcbStaticSelectionOptions options;
    options.preferred_rcb_reference = cli_options.preferred_rcb_reference;
    options.preferred_data_set_reference = cli_options.preferred_data_set_reference;
    options.strict_rcb = cli_options.strict_rcb;
    options.allow_urcb_fallback = cli_options.allow_urcb_fallback;
    options.allow_polling_fallback = cli_options.allow_polling_fallback;
    options.excluded_rcb_references = cli_options.excluded_rcb_references;
    return mms::MmsRcbPoolSelector::build_static_selection(result, options);
}

[[nodiscard]] std::optional<mms::MmsRcbOperationalAvailabilityResult>
build_rcb_operational_availability(
    const mms::MmsLiveDiscoveryResult& result,
    const RcbAvailabilityCliOptions& cli_options) {
    if (!cli_options.requested) return std::nullopt;
    mms::MmsRcbOperationalAvailabilityOptions options;
    options.maximum_report_controls = std::max<std::size_t>(
        1U, result.report_inventory.report_controls.size());
    return mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(result, options);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 &&
            (std::string_view{argv[1]} == "--help" ||
             std::string_view{argv[1]} == "-h")) {
            print_usage();
            return 0;
        }
        if (argc < 2) {
            print_usage();
            return 2;
        }

        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;

        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
            endpoint.port = parse_port(argv[argument]);
            ++argument;
        }

        enum class OutputMode { human, summary_json, model_json, manifest };
        OutputMode output_mode = OutputMode::human;
        std::chrono::milliseconds timeout{5'000};
        mms::MmsLiveDiscoveryOptions discovery_options;
        mms::MmsLiveModelBuildOptions model_options;
        RcbPlanCliOptions rcb_plan_options;
        RcbAvailabilityCliOptions rcb_availability_options;

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--json") output_mode = OutputMode::summary_json;
            else if (option == "--model-json") output_mode = OutputMode::model_json;
            else if (option == "--manifest") output_mode = OutputMode::manifest;
            else if (option == "--no-types") discovery_options.probe_variable_types = false;
            else if (option == "--no-datasets") {
                discovery_options.read_data_set_directories = false;
            } else if (option == "--no-rcb") {
                discovery_options.probe_report_controls = false;
            } else if (option == "--rcb-availability") {
                rcb_availability_options.requested = true;
            } else if (option == "--strict-rcb") {
                rcb_plan_options.strict_rcb = true;
            } else if (option == "--no-urcb-fallback") {
                rcb_plan_options.allow_urcb_fallback = false;
            } else if (option == "--no-polling-fallback") {
                rcb_plan_options.allow_polling_fallback = false;
            } else if (option == "--ied-name" || option == "--max-types" ||
                       option == "--max-datasets" || option == "--max-rcb" ||
                       option == "--timeout-ms" || option == "--rcb-plan" ||
                       option == "--rcb-ld" || option == "--rcb-dataset" ||
                       option == "--preferred-rcb" || option == "--rcb-exclude" ||
                       option == "--max-rcb-candidates" ||
                       option == "--max-rcb-availability") {
                if (argument >= argc) {
                    throw std::invalid_argument(option + " requires a value.");
                }
                const std::string value = argv[argument++];
                if (option == "--ied-name") {
                    model_options.explicit_ied_name = value;
                    continue;
                }
                if (option == "--rcb-plan") {
                    rcb_plan_options.mode = parse_rcb_plan_mode(value);
                    continue;
                }
                if (option == "--rcb-ld") {
                    rcb_plan_options.preferred_logical_device = value;
                    continue;
                }
                if (option == "--rcb-dataset") {
                    rcb_plan_options.preferred_data_set_reference = value;
                    continue;
                }
                if (option == "--preferred-rcb") {
                    rcb_plan_options.preferred_rcb_reference = value;
                    continue;
                }
                if (option == "--rcb-exclude") {
                    rcb_plan_options.excluded_rcb_references.push_back(value);
                    continue;
                }

                const auto parsed = parse_limit(option, value);
                if (option == "--max-types") {
                    discovery_options.maximum_variable_type_probes = parsed;
                } else if (option == "--max-datasets") {
                    discovery_options.maximum_data_set_directories = parsed;
                } else if (option == "--max-rcb") {
                    discovery_options.maximum_report_control_probes = parsed;
                } else if (option == "--max-rcb-candidates") {
                    rcb_plan_options.maximum_candidates_to_print = parsed;
                } else if (option == "--max-rcb-availability") {
                    rcb_availability_options.maximum_snapshots_to_print = parsed;
                } else {
                    if (parsed > static_cast<std::size_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                        throw std::invalid_argument("--timeout-ms is too large.");
                    }
                    timeout = std::chrono::milliseconds{
                        static_cast<std::int64_t>(parsed)};
                }
            } else if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        if (!rcb_plan_options.requested() && rcb_plan_options.has_policy_options()) {
            throw std::invalid_argument(
                "RCB planner policy options require --rcb-plan dynamic|static.");
        }
        if (!rcb_availability_options.requested &&
            rcb_availability_options.maximum_snapshots_to_print != 10U) {
            throw std::invalid_argument(
                "--max-rcb-availability requires --rcb-availability.");
        }
        if ((rcb_plan_options.requested() || rcb_availability_options.requested) &&
            !discovery_options.probe_report_controls) {
            throw std::invalid_argument(
                "RCB planning/availability requires read-only RCB probes; "
                "remove --no-rcb.");
        }
        if (rcb_plan_options.mode == RcbPlanMode::dynamic_data_set &&
            !rcb_plan_options.preferred_data_set_reference.empty()) {
            throw std::invalid_argument(
                "--rcb-dataset applies only to --rcb-plan static.");
        }
        if (rcb_plan_options.mode == RcbPlanMode::static_data_set &&
            !rcb_plan_options.preferred_logical_device.empty()) {
            throw std::invalid_argument(
                "--rcb-ld applies only to --rcb-plan dynamic.");
        }
        if ((rcb_plan_options.requested() || rcb_availability_options.requested) &&
            (output_mode == OutputMode::model_json ||
             output_mode == OutputMode::manifest)) {
            throw std::invalid_argument(
                "RCB runtime evidence cannot be combined with --model-json or "
                "--manifest structural parity output.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);
        const auto result = session.discover(discovery_options);
        session.disconnect();

        const auto model = mms::MmsLiveModelBuilder::build(result, model_options);
        const auto rcb_selection = build_rcb_plan(result, rcb_plan_options);
        const auto rcb_availability = build_rcb_operational_availability(
            result, rcb_availability_options);

        switch (output_mode) {
        case OutputMode::human:
            print_human(
                result, model, rcb_selection, rcb_availability,
                rcb_plan_options.maximum_candidates_to_print,
                rcb_availability_options.maximum_snapshots_to_print);
            break;
        case OutputMode::summary_json:
            print_summary_json(result, model, rcb_selection, rcb_availability);
            break;
        case OutputMode::model_json:
            std::cout << model.to_json() << '\n';
            break;
        case OutputMode::manifest:
            std::cout << model.canonical_manifest();
            break;
        }
        return result.partial() ? 1 : 0;
    } catch (const std::exception& exception) {
        std::cerr << "Live MMS discovery failed: " << exception.what() << '\n';
        return 2;
    }
}
