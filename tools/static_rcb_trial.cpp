// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/static_report_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace {

using namespace ar::iec61850;

constexpr std::string_view k_arm_token = "IEC61850-LAB-STATIC-RCB";

struct CliOptions final {
    mms::MmsEndpoint endpoint;
    std::chrono::milliseconds timeout{5'000};
    std::string preferred_rcb_reference;
    std::string preferred_data_set_reference;
    std::size_t maximum_data_set_directories{4'096U};
    std::size_t maximum_rcb_probes{4'096U};
    std::size_t maximum_claim_candidates{4U};
    std::size_t preclaim_probe_count{3U};
    std::chrono::milliseconds preclaim_probe_delay{250};
    int contention_cooldown_seconds{60};
    std::size_t confirmation_cycles{3U};
    std::chrono::milliseconds confirmation_delay{150};
    bool allow_urcb_fallback{true};
    bool trigger_gi{true};
    bool armed{};
};

[[nodiscard]] std::size_t parse_positive(
    const std::string& option,
    const std::string& text) {
    std::size_t consumed = 0U;
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value == 0ULL ||
        value > static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive integer.");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint16_t parse_port(const std::string& text) {
    const auto value = parse_positive("port", text);
    if (value > 65'535U) {
        throw std::invalid_argument("port must be 1..65535.");
    }
    return static_cast<std::uint16_t>(value);
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_static_rcb_trial <host> [port] [options]\n\n"
        << "Default mode is READ-ONLY: discover populated static DataSets, rank\n"
        << "their free BRCB/URCB bindings, and run bounded pre-claim Read probes.\n"
        << "No MMS Write is sent unless the exact --arm token is supplied.\n\n"
        << "Options:\n"
        << "  --preferred-rcb REF      Prefer one static RCB reference.\n"
        << "  --dataset-ref REF        Require a specific bound static DataSet.\n"
        << "  --no-urcb-fallback       Use BRCB candidates only.\n"
        << "  --max-datasets N         Bound DataSet directory reads (default 4096).\n"
        << "  --max-rcb N              Bound RCB state probes (default 4096).\n"
        << "  --max-claim-candidates N Bound pre-claim candidates (default 4).\n"
        << "  --preclaim-probes N      Read-only probes per candidate (default 3).\n"
        << "  --preclaim-delay-ms N    Delay between pre-claim probes (default 250).\n"
        << "  --contention-cooldown N  Recorded skip cooldown seconds (default 60).\n"
        << "  --probe-cycles N         Confirmation reads after enable (default 3).\n"
        << "  --probe-delay-ms N       Delay between confirmation reads (default 150).\n"
        << "  --timeout-ms N           Connect/request timeout (default 5000).\n"
        << "  --no-gi                  Do not request GI after enabling.\n"
        << "  --arm " << k_arm_token << "\n"
        << "                            Enable the selected static RCB, observe reports,\n"
        << "                            then disable/release only state touched here.\n"
        << "  -h, --help               Show this help.\n\n"
        << "This harness never rewrites DatSet, creates/deletes a DataSet, disables a\n"
        << "busy RCB, or switches candidates after a mutation begins.\n";
}

[[nodiscard]] CliOptions parse_cli(const int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("host is required.");
    }
    CliOptions options;
    options.endpoint.host = argv[1];
    options.endpoint.port = 102U;
    int index = 2;
    if (index < argc && std::string_view{argv[index]}.rfind("--", 0) != 0U) {
        options.endpoint.port = parse_port(argv[index++]);
    }
    while (index < argc) {
        const std::string option = argv[index++];
        if (option == "--no-urcb-fallback") {
            options.allow_urcb_fallback = false;
        } else if (option == "--no-gi") {
            options.trigger_gi = false;
        } else if (option == "--preferred-rcb" ||
                   option == "--dataset-ref" ||
                   option == "--max-datasets" ||
                   option == "--max-rcb" ||
                   option == "--max-claim-candidates" ||
                   option == "--preclaim-probes" ||
                   option == "--preclaim-delay-ms" ||
                   option == "--contention-cooldown" ||
                   option == "--probe-cycles" ||
                   option == "--probe-delay-ms" ||
                   option == "--timeout-ms" || option == "--arm") {
            if (index >= argc) {
                throw std::invalid_argument(option + " requires a value.");
            }
            const std::string value = argv[index++];
            if (option == "--preferred-rcb") {
                options.preferred_rcb_reference = value;
            } else if (option == "--dataset-ref") {
                options.preferred_data_set_reference = value;
            } else if (option == "--max-datasets") {
                options.maximum_data_set_directories = parse_positive(option, value);
            } else if (option == "--max-rcb") {
                options.maximum_rcb_probes = parse_positive(option, value);
            } else if (option == "--max-claim-candidates") {
                options.maximum_claim_candidates = parse_positive(option, value);
            } else if (option == "--preclaim-probes") {
                options.preclaim_probe_count = parse_positive(option, value);
            } else if (option == "--preclaim-delay-ms") {
                options.preclaim_probe_delay = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--contention-cooldown") {
                const auto seconds = parse_positive(option, value);
                if (seconds > static_cast<std::size_t>(
                                  std::numeric_limits<int>::max())) {
                    throw std::invalid_argument(
                        option + " exceeds the supported integer range.");
                }
                options.contention_cooldown_seconds = static_cast<int>(seconds);
            } else if (option == "--probe-cycles") {
                options.confirmation_cycles = parse_positive(option, value);
            } else if (option == "--probe-delay-ms") {
                options.confirmation_delay = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--timeout-ms") {
                options.timeout = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parse_positive(option, value))};
            } else if (option == "--arm") {
                if (value != k_arm_token) {
                    throw std::invalid_argument("invalid --arm token.");
                }
                options.armed = true;
            }
        } else {
            throw std::invalid_argument("Unknown option: " + option);
        }
    }
    return options;
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    return exchange.envelope.mms_payload;
}

void confirmation_read(
    mms::MmsAssociationRuntime& association,
    const mms::MmsReportControlCandidate& candidate) {
    const auto invoke_id = association.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(candidate.attribute_object_name("RptEna"));
    request.variables.push_back(candidate.attribute_object_name("DatSet"));
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error(
            "Static RCB confirmation read did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 2U || !response.results[0].success() ||
        !response.results[1].success()) {
        throw std::runtime_error(
            "Static RCB confirmation read returned incomplete results.");
    }
    const auto* enabled = std::get_if<bool>(&response.results[0].value->value());
    const auto* data_set = std::get_if<std::string>(
        &response.results[1].value->value());
    if (enabled == nullptr || !*enabled || data_set == nullptr ||
        data_set->empty()) {
        throw std::runtime_error(
            "Static RCB confirmation did not preserve RptEna=true plus DatSet.");
    }
}

void print_preclaim_evidence(
    const mms::MmsStaticReportSessionSnapshot& snapshot) {
    for (const auto& attempt : snapshot.failover.attempts) {
        std::cout << "PRECLAIM_ATTEMPT attempt=" << attempt.attempt_number
                  << " rcb=" << attempt.rcb_reference
                  << " outcome="
                  << mms::mms_rcb_preclaim_outcome_name(attempt.outcome)
                  << " busy=" << (attempt.busy ? "true" : "false")
                  << " flapping=" << (attempt.flapping ? "true" : "false")
                  << " cooldownSeconds=" << attempt.cooldown_seconds << '\n';
    }
}

} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc == 2 && (std::string_view{argv[1]} == "--help" ||
                          std::string_view{argv[1]} == "-h")) {
            print_usage();
            return 0;
        }
        const auto cli = parse_cli(argc, argv);

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = cli.timeout;
        association_options.request_timeout = cli.timeout;
        mms::MmsTcpLiveDiscoverySession live_session{{}, association_options};
        live_session.connect(cli.endpoint);

        mms::MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = false;
        discovery_options.read_data_set_directories = true;
        discovery_options.maximum_data_set_directories =
            cli.maximum_data_set_directories;
        discovery_options.probe_report_controls = true;
        discovery_options.maximum_report_control_probes = cli.maximum_rcb_probes;
        auto discovery = live_session.discover(discovery_options);

        mms::MmsStaticReportSessionOptions session_options;
        session_options.selection.preferred_rcb_reference =
            cli.preferred_rcb_reference;
        session_options.selection.preferred_data_set_reference =
            cli.preferred_data_set_reference;
        session_options.selection.allow_urcb_fallback =
            cli.allow_urcb_fallback;
        session_options.selection.allow_polling_fallback = false;
        session_options.contention.probe_count = cli.preclaim_probe_count;
        session_options.contention.probe_delay = cli.preclaim_probe_delay;
        session_options.contention.cooldown_seconds =
            cli.contention_cooldown_seconds;
        session_options.maximum_candidate_attempts =
            cli.maximum_claim_candidates;
        session_options.subscription.trigger_general_interrogation =
            cli.trigger_gi;
        session_options.subscription.reserve_unbuffered_rcb = true;
        session_options.subscription.write_data_set_reference = false;

        mms::MmsStaticReportSessionRuntime report_session{
            live_session.association(), discovery, session_options};
        try {
            report_session.prepare();
            auto prepared = report_session.snapshot();
            print_preclaim_evidence(prepared);
            const auto* candidate = report_session.selected_candidate();
            if (candidate == nullptr) {
                throw std::runtime_error(
                    "Static session prepared without a selected RCB.");
            }
            std::cout << "STATIC_PLAN selectedRcb="
                      << prepared.selected_rcb_reference
                      << " mode=" << (candidate->buffered ? "BRCB" : "URCB")
                      << " dataSet=" << prepared.data_set_reference
                      << " members=" << prepared.data_set_member_count << '\n';

            if (!cli.armed) {
                live_session.disconnect();
                std::cout << "READ_ONLY_STATIC_PLAN_OK\n";
                return 0;
            }

            report_session.start();
            std::cout << "STATIC_RCB_ENABLE_OK gi="
                      << (cli.trigger_gi ? "requested" : "not-requested")
                      << '\n';
            for (std::size_t cycle = 0U;
                 cycle < cli.confirmation_cycles; ++cycle) {
                std::this_thread::sleep_for(cli.confirmation_delay);
                confirmation_read(live_session.association(), *candidate);
                const auto drained = report_session.drain_queued_reports();
                std::cout << "CONFIRM_READ cycle=" << (cycle + 1U)
                          << " queuedReportsDrained=" << drained << '\n';
            }

            const auto active = report_session.snapshot();
            if (!active.subscription) {
                throw std::runtime_error(
                    "Static subscription snapshot is unavailable.");
            }
            std::cout << "REPORT_EVIDENCE received="
                      << active.subscription->received_reports
                      << " decodeFailures="
                      << active.subscription->decode_failures
                      << " streams=" << active.subscription->streams.size()
                      << '\n';

            report_session.stop();
            const auto stopped = report_session.snapshot();
            if (!stopped.subscription || stopped.subscription->cleanup_required) {
                throw std::runtime_error(
                    "Static RCB stop left cleanup_required=true.");
            }
            std::cout << "STATIC_RCB_DISABLE_OK\n";
            live_session.disconnect();

            if (cli.trigger_gi && active.subscription->received_reports == 0U) {
                std::cout
                    << "STATIC_TRIAL_LIFECYCLE_PASS_REPORT_PENDING: enable/disable "
                       "and binding preservation passed, but no GI report was "
                       "observed during bounded confirmation reads.\n";
                return 4;
            }
            std::cout << "SMART_STATIC_RCB_TRIAL_PASS\n";
            return 0;
        } catch (...) {
            report_session.stop();
            live_session.disconnect();
            throw;
        }
    } catch (const std::exception& exception) {
        std::cerr << "Static RCB trial failed: " << exception.what() << '\n';
        return 2;
    }
}
