// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/dynamic_data_set.hpp"
#include "ariec61850/mms/dynamic_report_planner.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/rcb_contention.hpp"
#include "ariec61850/mms/rcb_failover.hpp"
#include "ariec61850/mms/rcb_selection.hpp"
#include "ariec61850/mms/report_subscription_runtime.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

constexpr std::string_view k_arm_token = "IEC61850-LAB-DYNAMIC-RCB";

struct CliOptions final {
    mms::MmsEndpoint endpoint;
    std::chrono::milliseconds timeout{5'000};
    std::string preferred_logical_device;
    std::string preferred_rcb_reference;
    std::string data_set_reference;
    std::vector<std::string> explicit_members;
    std::size_t auto_member_count{4U};
    std::size_t maximum_type_probes{2'048U};
    std::size_t maximum_rcb_probes{4'096U};
    std::size_t maximum_claim_candidates{4U};
    std::size_t preclaim_probe_count{3U};
    std::chrono::milliseconds preclaim_probe_delay{250};
    int contention_cooldown_seconds{60};
    std::size_t probe_cycles{3U};
    std::chrono::milliseconds probe_delay{150};
    bool allow_urcb_fallback{true};
    bool trigger_gi{true};
    bool armed{};
};

[[nodiscard]] std::size_t parse_positive(const std::string& option, const std::string& text) {
    std::size_t consumed = 0U;
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value == 0ULL ||
        value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive integer.");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint16_t parse_port(const std::string& text) {
    const auto value = parse_positive("port", text);
    if (value > 65'535U) throw std::invalid_argument("port must be 1..65535.");
    return static_cast<std::uint16_t>(value);
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_dynamic_rcb_trial <host> [port] [options]\n\n"
        << "Default mode is READ-ONLY: discover the IED, rank empty dynamic RCB slots,\n"
        << "and show the member set that would be used. No MMS Write is sent.\n\n"
        << "Options:\n"
        << "  --rcb-ld LD              Prefer a logical device.\n"
        << "  --preferred-rcb REF      Prefer one RCB reference.\n"
        << "  --no-urcb-fallback       Use BRCB candidates only.\n"
        << "  --dataset-ref REF        Dynamic DataSet reference (default auto-generated).\n"
        << "  --member LD/ITEM         Exact MMS member; may be repeated.\n"
        << "  --auto-members N         Auto-pick N scalar ST/MX members (default 4).\n"
        << "  --max-types N            Bound GVAA probes used for auto-members (default 2048).\n"
        << "  --max-rcb N              Bound RCB probes (default 4096).\n"
        << "  --max-claim-candidates N Bound pre-claim candidates (default 4).\n"
        << "  --preclaim-probes N      Read-only probes per candidate (default 3).\n"
        << "  --preclaim-delay-ms N    Delay between pre-claim probes (default 250).\n"
        << "  --contention-cooldown N  Recorded skip cooldown seconds (default 60).\n"
        << "  --probe-cycles N         Confirmed read cycles after GI (default 3).\n"
        << "  --probe-delay-ms N       Delay between confirmation reads (default 150).\n"
        << "  --timeout-ms N           Connect/request timeout (default 5000).\n"
        << "  --no-gi                  Do not request GI after enabling.\n"
        << "  --arm " << k_arm_token << "\n"
        << "                            Enable mutation: Define DataSet -> bind RCB -> enable/GI\n"
        << "                            -> observe -> disable -> unbind -> Delete DataSet.\n"
        << "  -h, --help                Show this help.\n\n"
        << "Busy/flapping candidates are skipped before mutation. This harness never\n"
        << "auto-retries a failed mutation and never disables a busy RCB.\n";
}

[[nodiscard]] CliOptions parse_cli(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("host is required.");
    CliOptions options;
    options.endpoint.host = argv[1];
    options.endpoint.port = 102U;
    int index = 2;
    if (index < argc && std::string_view{argv[index]}.rfind("--", 0) != 0U) {
        options.endpoint.port = parse_port(argv[index++]);
    }
    while (index < argc) {
        const std::string option = argv[index++];
        if (option == "--no-urcb-fallback") options.allow_urcb_fallback = false;
        else if (option == "--no-gi") options.trigger_gi = false;
        else if (option == "--rcb-ld" || option == "--preferred-rcb" ||
                 option == "--dataset-ref" || option == "--member" ||
                 option == "--auto-members" || option == "--max-types" ||
                 option == "--max-rcb" || option == "--max-claim-candidates" ||
                 option == "--preclaim-probes" || option == "--preclaim-delay-ms" ||
                 option == "--contention-cooldown" || option == "--probe-cycles" ||
                 option == "--probe-delay-ms" || option == "--timeout-ms" ||
                 option == "--arm") {
            if (index >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[index++];
            if (option == "--rcb-ld") options.preferred_logical_device = value;
            else if (option == "--preferred-rcb") options.preferred_rcb_reference = value;
            else if (option == "--dataset-ref") options.data_set_reference = value;
            else if (option == "--member") options.explicit_members.push_back(value);
            else if (option == "--auto-members") options.auto_member_count = parse_positive(option, value);
            else if (option == "--max-types") options.maximum_type_probes = parse_positive(option, value);
            else if (option == "--max-rcb") options.maximum_rcb_probes = parse_positive(option, value);
            else if (option == "--max-claim-candidates") options.maximum_claim_candidates = parse_positive(option, value);
            else if (option == "--preclaim-probes") options.preclaim_probe_count = parse_positive(option, value);
            else if (option == "--preclaim-delay-ms") {
                options.preclaim_probe_delay = std::chrono::milliseconds{static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--contention-cooldown") {
                const auto seconds = parse_positive(option, value);
                if (seconds > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                    throw std::invalid_argument(option + " exceeds the supported integer range.");
                }
                options.contention_cooldown_seconds = static_cast<int>(seconds);
            }
            else if (option == "--probe-cycles") options.probe_cycles = parse_positive(option, value);
            else if (option == "--probe-delay-ms") {
                options.probe_delay = std::chrono::milliseconds{static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--timeout-ms") {
                options.timeout = std::chrono::milliseconds{static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--arm") {
                if (value != k_arm_token) throw std::invalid_argument("invalid --arm token.");
                options.armed = true;
            }
        } else {
            throw std::invalid_argument("Unknown option: " + option);
        }
    }
    return options;
}

[[nodiscard]] bool contains_attribute(const mms::MmsReportControlCandidate& candidate, std::string_view name) {
    return std::find(candidate.attributes.begin(), candidate.attributes.end(), name) != candidate.attributes.end();
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>> split_domain_item(const std::string& reference) {
    const auto slash = reference.find('/');
    if (slash == std::string::npos || slash == 0U || slash + 1U >= reference.size()) return std::nullopt;
    return std::pair{reference.substr(0U, slash), reference.substr(slash + 1U)};
}

[[nodiscard]] mms::MmsObjectName parse_member(const std::string& reference) {
    const auto parts = split_domain_item(reference);
    if (!parts) throw std::invalid_argument("--member must use exact MMS form LD/LN$FC$DO$DA...");
    return mms::MmsObjectName::domain_specific(parts->first, parts->second);
}

[[nodiscard]] std::string make_trial_data_set_reference(const std::string& domain) {
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream name;
    name << domain << "/LLN0.ARTRIAL" << std::uppercase << std::hex
         << static_cast<std::uint32_t>(epoch & 0xFFFF'FFFFLL);
    return name.str();
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(const mms::MmsConfirmedExchangeResult& exchange) {
    if (!exchange.presentation_payload.empty()) return exchange.presentation_payload;
    return exchange.envelope.mms_payload;
}

void require_write_success(
    mms::MmsAssociationRuntime& association,
    const mms::MmsReportControlCandidate& candidate,
    const std::string& attribute,
    mms::MmsDataValue value) {
    const auto invoke_id = association.next_invoke_id();
    mms::MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(candidate.attribute_object_name(attribute));
    request.values.push_back(std::move(value));
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("RCB " + attribute + " write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(response_payload(exchange), invoke_id);
    if (response.results.size() != 1U || !response.results.front().success) {
        const auto failure = response.results.size() == 1U
            ? response.results.front().failure_code
            : std::nullopt;
        throw std::runtime_error(
            "RCB " + attribute + " write was rejected" +
            (failure ? " (DataAccessError=" + std::to_string(*failure) + ")."
                     : "."));
    }
}

void confirmation_read(mms::MmsAssociationRuntime& association, const mms::MmsReportControlCandidate& candidate) {
    const auto invoke_id = association.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(candidate.attribute_object_name("RptEna"));
    request.variables.push_back(candidate.attribute_object_name("DatSet"));
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("RCB confirmation read did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(response_payload(exchange), invoke_id);
    if (response.results.size() != 2U || !response.results[0].success() || !response.results[1].success()) {
        throw std::runtime_error("RCB confirmation read returned incomplete results.");
    }
}

void print_candidates(const mms::MmsRcbSelectionEvidence& selection) {
    std::cout << selection.summary() << '\n';
    const auto count = std::min<std::size_t>(12U, selection.candidates.size());
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& item = selection.candidates[index];
        std::cout << "  [" << mms::mms_rcb_selection_decision_name(item.decision) << "] "
                  << item.reference << " score=" << item.score
                  << " availability=" << mms::mms_rcb_availability_kind_name(item.availability)
                  << " DatSet=" << (item.data_set_reference.empty() ? "<empty>" : item.data_set_reference)
                  << " RptEna=" << (item.report_enabled.empty() ? "?" : item.report_enabled)
                  << "\n";
    }
}

void best_effort_cleanup(
    mms::MmsAssociationRuntime& association,
    const mms::MmsReportControlCandidate* candidate,
    mms::MmsDynamicDataSetRuntime* dynamic_data_set,
    const std::string& data_set_reference,
    const bool data_set_created) noexcept {
    if (!association.associated()) return;
    if (candidate != nullptr) {
        try { require_write_success(association, *candidate, "RptEna", mms::MmsDataValue::boolean(false)); }
        catch (const std::exception& exception) { std::cerr << "CLEANUP warning: RptEna=false failed: " << exception.what() << '\n'; }
        if (!candidate->buffered && contains_attribute(*candidate, "Resv")) {
            try { require_write_success(association, *candidate, "Resv", mms::MmsDataValue::boolean(false)); }
            catch (const std::exception& exception) { std::cerr << "CLEANUP warning: Resv=false failed: " << exception.what() << '\n'; }
        }
        if (contains_attribute(*candidate, "DatSet")) {
            try { require_write_success(association, *candidate, "DatSet", mms::MmsDataValue::visible_string("")); }
            catch (const std::exception& exception) { std::cerr << "CLEANUP warning: DatSet unbind failed: " << exception.what() << '\n'; }
        }
    }
    if (data_set_created && dynamic_data_set != nullptr) {
        try {
            const auto response = dynamic_data_set->remove(data_set_reference);
            std::cerr << "CLEANUP Dynamic DataSet delete matched=" << response.number_matched.value_or(0U)
                      << " deleted=" << response.number_deleted.value_or(0U) << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "CLEANUP warning: Dynamic DataSet delete failed: " << exception.what() << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && (std::string_view{argv[1]} == "--help" || std::string_view{argv[1]} == "-h")) {
            print_usage();
            return 0;
        }
        auto cli = parse_cli(argc, argv);

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = cli.timeout;
        association_options.request_timeout = cli.timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(cli.endpoint);

        mms::MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = true;
        discovery_options.maximum_variable_type_probes = cli.maximum_type_probes;
        discovery_options.read_data_set_directories = false;
        discovery_options.probe_report_controls = true;
        discovery_options.maximum_report_control_probes = cli.maximum_rcb_probes;
        auto discovery = session.discover(discovery_options);

        mms::MmsRcbDynamicSelectionOptions selection_options;
        selection_options.preferred_logical_device = cli.preferred_logical_device;
        selection_options.preferred_rcb_reference = cli.preferred_rcb_reference;
        selection_options.allow_urcb_fallback = cli.allow_urcb_fallback;
        selection_options.allow_polling_fallback = false;
        mms::MmsRcbPreclaimFailoverTracker failover{cli.maximum_claim_candidates};
        mms::MmsRcbContentionProbeClient contention_probe{session.association()};
        const mms::MmsReportControlCandidate* candidate = nullptr;
        while (failover.may_attempt()) {
            selection_options.excluded_rcb_references =
                failover.excluded_rcb_references();
            const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
                discovery, selection_options);
            print_candidates(selection);
            candidate = mms::MmsRcbPoolSelector::select_report_control(
                discovery, selection);
            if (candidate == nullptr) {
                break;
            }

            mms::MmsRcbContentionProbeOptions probe_options;
            probe_options.probe_count = cli.preclaim_probe_count;
            probe_options.probe_delay = cli.preclaim_probe_delay;
            probe_options.cooldown_seconds = cli.contention_cooldown_seconds;
            const auto probe = contention_probe.probe(*candidate, probe_options);
            const auto outcome = failover.observe(probe);
            const auto& attempt = failover.snapshot().attempts.back();
            std::cout << "PRECLAIM_ATTEMPT attempt=" << attempt.attempt_number
                      << " rcb=" << attempt.rcb_reference
                      << " outcome=" << mms::mms_rcb_preclaim_outcome_name(outcome)
                      << " busy=" << (attempt.busy ? "true" : "false")
                      << " flapping=" << (attempt.flapping ? "true" : "false")
                      << " cooldownSeconds=" << attempt.cooldown_seconds << '\n';
            if (outcome == mms::MmsRcbPreclaimOutcome::stable_proceed) {
                break;
            }
            std::cout << "RCB_FAILOVER_SKIP rcb=" << attempt.rcb_reference
                      << " reason=" << attempt.reason << '\n';
            candidate = nullptr;
        }
        if (candidate == nullptr) {
            session.disconnect();
            std::cerr << "BLOCKED: no stable empty dynamic RCB slot remained after "
                      << failover.snapshot().attempts.size()
                      << " bounded pre-claim attempt(s). No mutation sent.\n";
            return 3;
        }
        if (!contains_attribute(*candidate, "DatSet") || !contains_attribute(*candidate, "RptEna")) {
            session.disconnect();
            std::cerr << "BLOCKED: selected RCB does not expose DatSet + RptEna. No mutation sent.\n";
            return 3;
        }

        std::vector<mms::MmsObjectName> members;
        std::optional<mms::MmsDynamicReportMemberSelection> auto_selection;
        if (!cli.explicit_members.empty()) {
            members.reserve(cli.explicit_members.size());
            for (const auto& reference : cli.explicit_members) members.push_back(parse_member(reference));
        } else {
            auto_selection = mms::MmsDynamicReportMemberSelector::select(
                discovery, *candidate, cli.auto_member_count);
            members = auto_selection->members;
        }
        if (members.empty()) {
            session.disconnect();
            std::cerr << "BLOCKED: no safe scalar ST/MX members were selected";
            if (auto_selection) {
                std::cerr << " (successfulTypeProbes="
                          << auto_selection->successful_type_probes
                          << ", scalarLeafCandidates="
                          << auto_selection->scalar_leaf_candidates << ')';
            }
            std::cerr << ". Use --member explicitly.\n";
            return 3;
        }

        const auto data_set_reference = cli.data_set_reference.empty()
            ? make_trial_data_set_reference(candidate->domain)
            : cli.data_set_reference;

        std::cout << "SMART_DYNAMIC_RCB_PLAN\n"
                  << "  endpoint=" << cli.endpoint.host << ':' << cli.endpoint.port << '\n'
                  << "  rcb=" << candidate->reference << " mode=" << candidate->mode() << '\n'
                  << "  preclaimAttempts=" << failover.snapshot().attempts.size()
                  << " skipped=" << failover.snapshot().excluded_rcb_references.size() << '\n'
                  << "  dataset=" << data_set_reference << '\n'
                  << "  members=" << members.size() << '\n';
        if (auto_selection) {
            std::cout << "  autoMemberEvidence=typeProbes:"
                      << auto_selection->successful_type_probes
                      << ",scalarLeaves:"
                      << auto_selection->scalar_leaf_candidates << '\n';
        }
        for (const auto& member : members) std::cout << "    - " << member.reference() << '\n';

        if (!cli.armed) {
            std::cout << "READ_ONLY_PLAN_OK: no MMS Write/Define/Delete was sent.\n"
                      << "Re-run with --arm " << k_arm_token << " to execute the lab lifecycle.\n";
            session.disconnect();
            return 0;
        }

        mms::MmsDynamicDataSetRuntime dynamic_data_set{session.association()};
        bool data_set_created = false;
        std::unique_ptr<mms::MmsReportSubscriptionRuntime> subscription;
        try {
            const auto created = dynamic_data_set.create(data_set_reference, members);
            data_set_created = true;
            if (!created.verified_directory) throw std::runtime_error("Dynamic DataSet create returned no verified directory.");
            std::cout << "DEFINE_OK dataset=" << created.data_set_reference
                      << " verifiedMembers=" << created.verified_directory->members.size() << '\n';

            mms::MmsReportSubscriptionOptions subscription_options;
            subscription_options.write_data_set_reference = true;
            subscription_options.data_set_reference = created.data_set_reference;
            subscription_options.trigger_general_interrogation = cli.trigger_gi && contains_attribute(*candidate, "GI");
            subscription_options.reserve_unbuffered_rcb = true;
            subscription = std::make_unique<mms::MmsReportSubscriptionRuntime>(
                session.association(), *candidate, *created.verified_directory, subscription_options);
            subscription->start();
            std::cout << "RCB_ENABLE_OK gi="
                      << (subscription_options.trigger_general_interrogation ? "requested" : "not-requested") << '\n';

            for (std::size_t cycle = 0U; cycle < cli.probe_cycles; ++cycle) {
                std::this_thread::sleep_for(cli.probe_delay);
                confirmation_read(session.association(), *candidate);
                const auto drained = subscription->drain_queued_reports();
                std::cout << "CONFIRM_READ cycle=" << (cycle + 1U)
                          << " queuedReportsDrained=" << drained << '\n';
            }

            const auto active = subscription->snapshot();
            std::cout << "REPORT_EVIDENCE received=" << active.received_reports
                      << " decodeFailures=" << active.decode_failures
                      << " streams=" << active.streams.size() << '\n';
            for (const auto& event : active.events) {
                if (event.kind == mms::MmsReportSubscriptionEventKind::report_decode_failed) {
                    std::cout << "REPORT_DIAGNOSTIC " << event.message << '\n';
                }
            }

            subscription->stop();
            const auto stopped = subscription->snapshot();
            if (stopped.cleanup_required) {
                throw std::runtime_error("RCB stop left cleanup_required=true; refusing normal DataSet deletion.");
            }
            std::cout << "RCB_DISABLE_OK\n";

            require_write_success(
                session.association(), *candidate, "DatSet", mms::MmsDataValue::visible_string(""));
            std::cout << "RCB_UNBIND_OK\n";

            const auto deleted = dynamic_data_set.remove(created.data_set_reference);
            data_set_created = false;
            std::cout << "DELETE_OK matched=" << deleted.number_matched.value_or(0U)
                      << " deleted=" << deleted.number_deleted.value_or(0U) << '\n';
            session.disconnect();

            if (subscription_options.trigger_general_interrogation && active.received_reports == 0U) {
                std::cout << "TRIAL_LIFECYCLE_PASS_REPORT_PENDING: mutation lifecycle passed, but no GI report was observed during bounded confirmation reads.\n";
                return 4;
            }
            std::cout << "SMART_DYNAMIC_RCB_TRIAL_PASS\n";
            return 0;
        } catch (...) {
            if (subscription) subscription->stop();
            best_effort_cleanup(
                session.association(), candidate, &dynamic_data_set,
                data_set_reference, data_set_created);
            session.disconnect();
            throw;
        }
    } catch (const std::exception& exception) {
        std::cerr << "Dynamic RCB trial failed: " << exception.what() << '\n';
        return 2;
    }
}
