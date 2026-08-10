// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

constexpr std::string_view kArmToken{"IEC61850-LAB-CONTROL"};

struct Options final {
    MmsEndpoint endpoint{};
    std::string object_reference;
    std::string action{"discover"};
    std::string value;
    std::string value_kind;
    std::string origin_identifier{"ARSTACK-C5"};
    OriginCategory origin_category{OriginCategory::station_control};
    std::chrono::milliseconds timeout{5'000};
    std::optional<std::chrono::milliseconds> termination_timeout;
    bool test{};
    bool interlock_check{true};
    bool synchro_check{true};
    bool auto_select{true};
    bool armed{};
    std::string evidence_path;
    bool self_test{};
};

struct LabAuthorization final {
    bool armed{};
};

[[nodiscard]] bool authorize_lab_control(
    void* context,
    ControlAction,
    const ControlObjectReference&,
    const ControlClientIdentity&,
    const ControlSequenceView&) noexcept {
    const auto* authorization = static_cast<const LabAuthorization*>(context);
    return authorization != nullptr && authorization->armed;
}

[[nodiscard]] std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return value;
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::chrono::milliseconds parse_timeout(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument(option + " requires a positive millisecond value.");
    }
    return std::chrono::milliseconds{static_cast<std::int64_t>(parsed)};
}

[[nodiscard]] bool parse_switch(const std::string& option, const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "on" || normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "off" || normalized == "false" || normalized == "0") {
        return false;
    }
    throw std::invalid_argument(option + " requires on/off.");
}

[[nodiscard]] OriginCategory parse_origin_category(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "bay") return OriginCategory::bay_control;
    if (normalized == "station") return OriginCategory::station_control;
    if (normalized == "remote") return OriginCategory::remote_control;
    if (normalized == "automatic-bay") return OriginCategory::automatic_bay;
    if (normalized == "automatic-station") return OriginCategory::automatic_station;
    if (normalized == "automatic-remote") return OriginCategory::automatic_remote;
    if (normalized == "maintenance") return OriginCategory::maintenance;
    if (normalized == "process") return OriginCategory::process;
    throw std::invalid_argument(
        "--origin-category requires bay, station, remote, automatic-bay, "
        "automatic-station, automatic-remote, maintenance, or process.");
}

[[nodiscard]] const char* model_name(const ControlModel model) noexcept {
    switch (model) {
    case ControlModel::status_only: return "status-only";
    case ControlModel::direct_normal: return "direct-normal";
    case ControlModel::select_before_operate_normal: return "sbo-normal";
    case ControlModel::direct_enhanced: return "direct-enhanced";
    case ControlModel::select_before_operate_enhanced: return "sbo-enhanced";
    case ControlModel::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] const char* completion_name(const ControlCompletionState state) noexcept {
    switch (state) {
    case ControlCompletionState::accepted: return "accepted";
    case ControlCompletionState::positive_termination: return "positive-termination";
    case ControlCompletionState::negative_termination: return "negative-termination";
    case ControlCompletionState::rejected: return "rejected";
    case ControlCompletionState::unsupported: return "unsupported";
    case ControlCompletionState::timed_out: return "timed-out";
    case ControlCompletionState::association_lost: return "association-lost";
    case ControlCompletionState::cancelled: return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] const char* action_name(const ControlAction action) noexcept {
    switch (action) {
    case ControlAction::select: return "select";
    case ControlAction::select_with_value: return "select-with-value";
    case ControlAction::operate: return "operate";
    case ControlAction::cancel: return "cancel";
    }
    return "unknown";
}

[[nodiscard]] const char* add_cause_name(const AddCause cause) noexcept {
    switch (cause) {
    case AddCause::unknown: return "unknown";
    case AddCause::not_supported: return "not-supported";
    case AddCause::blocked_by_switching_hierarchy: return "blocked-by-switching-hierarchy";
    case AddCause::select_failed: return "select-failed";
    case AddCause::invalid_position: return "invalid-position";
    case AddCause::position_reached: return "position-reached";
    case AddCause::parameter_change_in_execution: return "parameter-change-in-execution";
    case AddCause::step_limit: return "step-limit";
    case AddCause::blocked_by_mode: return "blocked-by-mode";
    case AddCause::blocked_by_process: return "blocked-by-process";
    case AddCause::blocked_by_interlocking: return "blocked-by-interlocking";
    case AddCause::blocked_by_synchrocheck: return "blocked-by-synchrocheck";
    case AddCause::command_already_in_execution: return "command-already-in-execution";
    case AddCause::blocked_by_health: return "blocked-by-health";
    case AddCause::one_of_n_control: return "one-of-n-control";
    case AddCause::abortion_by_cancel: return "abortion-by-cancel";
    case AddCause::time_limit_over: return "time-limit-over";
    case AddCause::abortion_by_trip: return "abortion-by-trip";
    case AddCause::object_not_selected: return "object-not-selected";
    case AddCause::object_already_selected: return "object-already-selected";
    case AddCause::no_access_authority: return "no-access-authority";
    case AddCause::ended_with_overshoot: return "ended-with-overshoot";
    case AddCause::abortion_due_to_deviation: return "abortion-due-to-deviation";
    case AddCause::abortion_by_communication_loss: return "abortion-by-communication-loss";
    case AddCause::abortion_by_command: return "abortion-by-command";
    case AddCause::none: return "none";
    case AddCause::inconsistent_parameters: return "inconsistent-parameters";
    case AddCause::locked_by_other_client: return "locked-by-other-client";
    }
    return "unknown";
}

[[nodiscard]] const char* data_access_error_name(const std::uint32_t code) noexcept {
    switch (code) {
    case 0U: return "object-invalidated";
    case 1U: return "hardware-fault";
    case 2U: return "temporarily-unavailable";
    case 3U: return "object-access-denied";
    case 4U: return "object-undefined";
    case 5U: return "invalid-address";
    case 6U: return "type-unsupported";
    case 7U: return "type-inconsistent";
    case 8U: return "object-attribute-inconsistent";
    case 9U: return "object-access-unsupported";
    case 10U: return "object-non-existent";
    case 11U: return "object-value-invalid";
    default: return "unknown";
    }
}

[[nodiscard]] std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const auto ch : value) {
        switch (ch) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(static_cast<unsigned char>(ch))
                       << std::dec;
            } else {
                output << ch;
            }
        }
    }
    return output.str();
}

[[nodiscard]] std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

[[nodiscard]] std::string value_text(const MmsDataValue& value) {
    switch (value.kind()) {
    case MmsDataKind::boolean:
        return std::get<bool>(value.value()) ? "true" : "false";
    case MmsDataKind::integer:
        return std::to_string(std::get<std::int64_t>(value.value()));
    case MmsDataKind::unsigned_integer:
        return std::to_string(std::get<std::uint64_t>(value.value()));
    case MmsDataKind::floating_point: {
        std::ostringstream output;
        if (const auto* float_value = std::get_if<float>(&value.value())) {
            output << *float_value;
        } else if (const auto* double_value = std::get_if<double>(&value.value())) {
            output << *double_value;
        }
        return output.str();
    }
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        return std::get<std::string>(value.value());
    case MmsDataKind::array:
    case MmsDataKind::structure: {
        std::ostringstream output;
        output << '[';
        for (std::size_t index = 0U; index < value.children().size(); ++index) {
            if (index != 0U) output << ',';
            output << value_text(value.children()[index]);
        }
        output << ']';
        return output.str();
    }
    case MmsDataKind::bit_string:
    case MmsDataKind::octet_string:
    case MmsDataKind::binary_time:
    case MmsDataKind::bcd:
    case MmsDataKind::boolean_array:
    case MmsDataKind::object_id:
    case MmsDataKind::unknown:
        return "0x" + hex_bytes(value.raw_value());
    case MmsDataKind::utc_time:
        return "<utc-time>";
    }
    return "<unsupported>";
}

class RecordingControlTransport final : public ControlTransport {
public:
    explicit RecordingControlTransport(ControlTransport& inner) noexcept : inner_{inner} {}

    [[nodiscard]] bool associated() const noexcept override { return inner_.associated(); }
    [[nodiscard]] std::uint64_t association_id() const noexcept override {
        return inner_.association_id();
    }

    [[nodiscard]] std::optional<MmsDataValue> read(
        const MmsObjectName& object,
        std::stop_token stop_token = {}) override {
        reads.push_back(object.reference());
        return inner_.read(object, stop_token);
    }

    [[nodiscard]] std::optional<MmsTypeSpecification> variable_specification(
        const MmsObjectName& object,
        std::stop_token stop_token = {}) override {
        specifications.push_back(object.reference());
        return inner_.variable_specification(object, stop_token);
    }

    [[nodiscard]] std::vector<std::string> domain_variable_names(
        const std::string& domain,
        std::stop_token stop_token = {}) override {
        name_lists.push_back(domain);
        return inner_.domain_variable_names(domain, stop_token);
    }

    [[nodiscard]] ControlTransportWriteResult write(
        const MmsObjectName& object,
        MmsDataValue value,
        std::stop_token stop_token = {}) override {
        writes.push_back(object.reference());
        return inner_.write(object, std::move(value), stop_token);
    }

    void clear_information_reports() override {
        ++report_drains;
        inner_.clear_information_reports();
    }

    [[nodiscard]] bool wait_information_report(
        const std::chrono::milliseconds timeout,
        MmsInformationReport& report,
        std::stop_token stop_token = {}) override {
        ++report_waits;
        const auto received = inner_.wait_information_report(timeout, report, stop_token);
        if (received) ++reports_received;
        return received;
    }

    std::vector<std::string> reads;
    std::vector<std::string> specifications;
    std::vector<std::string> name_lists;
    std::vector<std::string> writes;
    std::size_t report_drains{};
    std::size_t report_waits{};
    std::size_t reports_received{};

private:
    ControlTransport& inner_;
};

[[nodiscard]] ControlValue parse_control_value(
    const std::string& raw_kind,
    const std::string& raw_value,
    const std::string& cdc) {
    auto kind = lower(raw_kind);
    if (kind.empty()) {
        const auto normalized_cdc = lower(cdc);
        if (normalized_cdc == "spc") kind = "bool";
        else if (normalized_cdc == "dpc") kind = "dpc";
        else if (normalized_cdc == "apc") kind = "float";
        else if (normalized_cdc == "bsc") kind = "step";
        else if (normalized_cdc == "inc/isc") kind = "int";
        else throw std::invalid_argument(
            "Cannot infer --value-kind for vendor-specific CDC; specify it explicitly.");
    }

    if (kind == "bool" || kind == "boolean") {
        return ControlValue::boolean(parse_switch("--value", raw_value));
    }
    if (kind == "dpc" || kind == "double-point") {
        const auto value = lower(raw_value);
        if (value == "off" || value == "1") return ControlValue::double_point(DoublePointValue::off);
        if (value == "on" || value == "2") return ControlValue::double_point(DoublePointValue::on);
        if (value == "intermediate" || value == "0") return ControlValue::double_point(DoublePointValue::intermediate);
        if (value == "bad" || value == "3") return ControlValue::double_point(DoublePointValue::bad);
        throw std::invalid_argument("DPC --value requires off, on, intermediate, or bad.");
    }
    if (kind == "int" || kind == "integer") {
        std::size_t consumed{};
        const auto parsed = std::stoll(raw_value, &consumed, 0);
        if (consumed != raw_value.size()) throw std::invalid_argument("Invalid signed integer --value.");
        return ControlValue::integer(static_cast<std::int64_t>(parsed));
    }
    if (kind == "uint" || kind == "unsigned") {
        std::size_t consumed{};
        const auto parsed = std::stoull(raw_value, &consumed, 0);
        if (consumed != raw_value.size()) throw std::invalid_argument("Invalid unsigned integer --value.");
        return ControlValue::unsigned_integer(static_cast<std::uint64_t>(parsed));
    }
    if (kind == "float" || kind == "double") {
        std::size_t consumed{};
        const auto parsed = std::stod(raw_value, &consumed);
        if (consumed != raw_value.size()) throw std::invalid_argument("Invalid floating-point --value.");
        return ControlValue::floating_point(parsed);
    }
    if (kind == "step") {
        const auto colon = raw_value.find(':');
        const auto position_text = raw_value.substr(0U, colon);
        std::size_t consumed{};
        const auto parsed = std::stoll(position_text, &consumed, 0);
        if (consumed != position_text.size()) throw std::invalid_argument("Invalid step position --value.");
        bool transient{};
        if (colon != std::string::npos) {
            transient = parse_switch("step transient", raw_value.substr(colon + 1U));
        }
        return ControlValue::step_position({static_cast<std::int64_t>(parsed), transient});
    }
    throw std::invalid_argument(
        "--value-kind requires bool, dpc, int, uint, float, or step.");
}

[[nodiscard]] ControlActionResult execute_action(
    const Options& options,
    const ControlObjectDescriptor& descriptor,
    ControlObjectSession& session,
    const ControlRequest& request) {
    if (options.action == "operate") {
        return session.operate(request);
    }

    if (options.action == "select-operate" || options.action == "select-cancel") {
        if (!descriptor.requires_select()) {
            throw std::invalid_argument(
                options.action + " requires an SBO control model discovered from the IED.");
        }
        ControlActionResult selected;
        if (descriptor.model == ControlModel::select_before_operate_enhanced) {
            selected = session.select_with_value(request);
        } else {
            selected = session.select(request);
        }
        if (!selected.success()) return selected;
        if (options.action == "select-cancel") return session.cancel();

        auto operate_request = request;
        operate_request.auto_select = false;
        return session.operate(operate_request);
    }

    throw std::invalid_argument(
        "--action requires discover, operate, select-operate, or select-cancel.");
}

void print_descriptor(const ControlObjectDescriptor& descriptor) {
    std::cout << "CONTROL_DESCRIPTOR object=" << descriptor.object.domain << '/'
              << descriptor.object.logical_node << '.' << descriptor.object.data_object_path
              << " ctlModel=" << model_name(descriptor.model)
              << " cdc=" << descriptor.cdc
              << " requiresSelect=" << (descriptor.requires_select() ? "true" : "false")
              << " enhanced=" << (descriptor.enhanced() ? "true" : "false")
              << " sboTimeoutMs=" << descriptor.sbo_timeout.count()
              << " operTimeoutMs=" << descriptor.operate_timeout.count()
              << " commandTermination="
              << (descriptor.supports_command_termination ? "true" : "false")
              << '\n';
    if (descriptor.status_object) {
        std::cout << "STATUS_OBJECT " << descriptor.status_object->reference()
                  << " fc=" << descriptor.status_functional_constraint << '\n';
    }
    for (const auto& evidence : descriptor.discovery_evidence) {
        std::cout << "DISCOVERY_EVIDENCE " << evidence << '\n';
    }
}

void write_evidence(
    const Options& options,
    const ControlObjectDescriptor& descriptor,
    const RecordingControlTransport& transport,
    const std::optional<std::string>& status_before,
    const std::optional<std::string>& status_after,
    const std::optional<ControlActionResult>& result,
    const MmsAssociationRuntime& association) {
    if (options.evidence_path.empty()) return;

    std::ofstream output{options.evidence_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error("Cannot open evidence file: " + options.evidence_path);
    }

    output << "{\n"
           << "  \"schema\": \"arstack61850-control-interop-v1\",\n"
           << "  \"endpoint\": \"" << json_escape(options.endpoint.host) << ':'
           << options.endpoint.port << "\",\n"
           << "  \"object\": \"" << json_escape(options.object_reference) << "\",\n"
           << "  \"action\": \"" << json_escape(options.action) << "\",\n"
           << "  \"armed\": " << (options.armed ? "true" : "false") << ",\n"
           << "  \"ctlModel\": \"" << model_name(descriptor.model) << "\",\n"
           << "  \"cdc\": \"" << json_escape(descriptor.cdc) << "\",\n"
           << "  \"associationProfile\": \""
           << json_escape(association.active_association_profile()) << "\",\n"
           << "  \"statusBefore\": ";
    if (status_before) output << '"' << json_escape(*status_before) << '"';
    else output << "null";
    output << ",\n  \"statusAfter\": ";
    if (status_after) output << '"' << json_escape(*status_after) << '"';
    else output << "null";
    output << ",\n  \"mmsControlWriteCount\": " << transport.writes.size() << ",\n"
           << "  \"mmsControlWrites\": [";
    for (std::size_t index = 0U; index < transport.writes.size(); ++index) {
        if (index != 0U) output << ',';
        output << "\"" << json_escape(transport.writes[index]) << "\"";
    }
    output << "],\n"
           << "  \"readCount\": " << transport.reads.size() << ",\n"
           << "  \"gvaaCount\": " << transport.specifications.size() << ",\n"
           << "  \"reportDrainCount\": " << transport.report_drains << ",\n"
           << "  \"reportWaitCount\": " << transport.report_waits << ",\n"
           << "  \"informationReportsReceived\": " << transport.reports_received << ",\n"
           << "  \"associationEventCount\": " << association.events().size();
    if (result) {
        output << ",\n  \"result\": {\n"
               << "    \"action\": \"" << action_name(result->action) << "\",\n"
               << "    \"completion\": \"" << completion_name(result->completion) << "\",\n"
               << "    \"requestAccepted\": "
               << (result->request_accepted ? "true" : "false") << ",\n"
               << "    \"commandTerminationReceived\": "
               << (result->command_termination_received ? "true" : "false") << ",\n"
               << "    \"positiveTermination\": "
               << (result->positive_termination ? "true" : "false") << ",\n"
               << "    \"controlNumber\": " << static_cast<unsigned>(result->control_number) << ",\n"
               << "    \"mmsFailureCode\": ";
        if (result->mms_failure_code) {
            output << *result->mms_failure_code;
        } else {
            output << "null";
        }
        output << ",\n    \"mmsFailure\": ";
        if (result->mms_failure_code) {
            output << '"' << data_access_error_name(*result->mms_failure_code) << '"';
        } else {
            output << "null";
        }
        output << ",\n"
               << "    \"rawControlError\": " << result->raw_control_error << ",\n"
               << "    \"rawAddCause\": " << result->raw_add_cause << ",\n"
               << "    \"addCause\": \"" << add_cause_name(result->add_cause) << "\",\n"
               << "    \"message\": \"" << json_escape(result->message) << "\"\n"
               << "  }";
    }
    output << "\n}\n";
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  ariec61850_control_interop_probe <host> [port] --object LD/LN.DO [options]\n\n"
        << "Read-only default:\n"
        << "  No --action means discover only. Discovery reads ctlModel, exact live GVAA,\n"
        << "  CF timeouts when exposed, and the best matching ST/MX status value.\n\n"
        << "Write actions (LAB ONLY):\n"
        << "  --action operate|select-operate|select-cancel\n"
        << "  --value VALUE                  Required for a write action.\n"
        << "  --value-kind KIND              bool|dpc|int|uint|float|step; inferred from CDC when possible.\n"
        << "  --arm IEC61850-LAB-CONTROL     Mandatory exact token before any control Write.\n\n"
        << "Control sequence options:\n"
        << "  --origin TEXT                  Origin identifier (default ARSTACK-C5, max 64 bytes).\n"
        << "  --origin-category CAT          bay|station|remote|automatic-bay|automatic-station|\n"
        << "                                 automatic-remote|maintenance|process.\n"
        << "  --test on|off                  IEC 61850 Test flag (default off).\n"
        << "  --interlock-check on|off       Check bit 1 (default on).\n"
        << "  --synchro-check on|off         Check bit 0 (default on).\n"
        << "  --auto-select on|off           Operate may select automatically (default on).\n"
        << "  --termination-timeout-ms N     Override enhanced CommandTermination wait.\n"
        << "  --timeout-ms N                 MMS connect/request timeout (default 5000).\n"
        << "  --evidence FILE                Write machine-readable JSON evidence.\n"
        << "  --self-test                    Offline harness safety self-test; no network.\n\n"
        << "Safety: this tool never retries a command automatically. For a live write, use an\n"
        << "isolated/non-operational laboratory IED and capture tcp.port==102 in Wireshark.\n";
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    Options options;
    options.endpoint.port = 102U;

    if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
        options.self_test = true;
        return options;
    }
    if (argc < 2 || std::string_view{argv[1]} == "--help" ||
        std::string_view{argv[1]} == "-h") {
        return options;
    }

    options.endpoint.host = argv[1];
    int argument = 2;
    if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
        options.endpoint.port = parse_port(argv[argument]);
        ++argument;
    }

    while (argument < argc) {
        const std::string option = argv[argument++];
        if (option == "--help" || option == "-h") return Options{};
        if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
        const std::string value = argv[argument++];
        if (option == "--object") options.object_reference = value;
        else if (option == "--action") options.action = lower(value);
        else if (option == "--value") options.value = value;
        else if (option == "--value-kind") options.value_kind = lower(value);
        else if (option == "--origin") options.origin_identifier = value;
        else if (option == "--origin-category") options.origin_category = parse_origin_category(value);
        else if (option == "--timeout-ms") options.timeout = parse_timeout(option, value);
        else if (option == "--termination-timeout-ms") {
            options.termination_timeout = parse_timeout(option, value);
        } else if (option == "--test") options.test = parse_switch(option, value);
        else if (option == "--interlock-check") options.interlock_check = parse_switch(option, value);
        else if (option == "--synchro-check") options.synchro_check = parse_switch(option, value);
        else if (option == "--auto-select") options.auto_select = parse_switch(option, value);
        else if (option == "--arm") options.armed = value == kArmToken;
        else if (option == "--evidence") options.evidence_path = value;
        else throw std::invalid_argument("Unknown option: " + option);
    }
    return options;
}

void validate_options(const Options& options) {
    if (options.endpoint.host.empty()) throw std::invalid_argument("Missing host.");
    if (options.object_reference.empty()) throw std::invalid_argument("--object is required.");
    if (options.origin_identifier.size() > 64U) {
        throw std::invalid_argument("--origin must not exceed 64 bytes.");
    }
    if (options.action != "discover" && options.action != "operate" &&
        options.action != "select-operate" && options.action != "select-cancel") {
        throw std::invalid_argument(
            "--action requires discover, operate, select-operate, or select-cancel.");
    }
    if (options.action != "discover") {
        if (!options.armed) {
            throw std::invalid_argument(
                "Control Write is locked. Add exact --arm IEC61850-LAB-CONTROL only for an isolated lab IED.");
        }
        if (options.value.empty()) throw std::invalid_argument("--value is required for a control action.");
    }
}

int run_self_test() {
    Options read_only;
    read_only.endpoint.host = "127.0.0.1";
    read_only.object_reference = "LD0/CSWI1.Pos";
    validate_options(read_only);

    auto blocked = read_only;
    blocked.action = "operate";
    blocked.value = "on";
    bool rejected{};
    try {
        validate_options(blocked);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        std::cerr << "SELF_TEST FAIL: unarmed control action was not rejected.\n";
        return 1;
    }

    blocked.armed = true;
    validate_options(blocked);
    const auto dpc = parse_control_value("dpc", "on", "DPC");
    if (dpc.kind() != ControlValueKind::double_point) {
        std::cerr << "SELF_TEST FAIL: DPC parsing mismatch.\n";
        return 1;
    }

    std::cout << "CONTROL_INTEROP_SELF_TEST PASS readOnlyDefault=true armGate=true noAutoRetry=true\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.self_test) return run_self_test();
        if (options.endpoint.host.empty()) {
            print_usage();
            return argc > 1 ? 0 : 2;
        }
        validate_options(options);

        MmsAssociationOptions association_options;
        association_options.connect_timeout = options.timeout;
        association_options.request_timeout = options.timeout;
        MmsTcpLiveDiscoverySession live_session{{}, association_options};
        live_session.connect(options.endpoint);

        MmsAssociationControlTransport association_transport{live_session.association()};
        RecordingControlTransport transport{association_transport};
        const auto descriptor = ControlDescriptorDiscovery::discover(
            transport, options.object_reference);
        print_descriptor(descriptor);

        std::optional<std::string> status_before;
        if (descriptor.status_object) {
            if (const auto value = transport.read(*descriptor.status_object)) {
                status_before = value_text(*value);
                std::cout << "STATUS_BEFORE " << *status_before << '\n';
            }
        }

        if (options.action == "discover") {
            std::cout << "CONTROL_INTEROP_RESULT mode=read-only writes=0 status=DISCOVERY_PASS\n";
            write_evidence(
                options, descriptor, transport, status_before, std::nullopt,
                std::nullopt, live_session.association());
            live_session.disconnect();
            return 0;
        }

        LabAuthorization authorization{options.armed};
        ControlSessionOptions session_options;
        session_options.guarded_policy.authorization_context = &authorization;
        session_options.guarded_policy.authorize = authorize_lab_control;
        ControlObjectSession control_session{transport, descriptor, session_options};

        ControlRequest request;
        request.control_value = parse_control_value(
            options.value_kind, options.value, descriptor.cdc);
        request.origin_category = options.origin_category;
        request.origin_identifier.assign(
            options.origin_identifier.begin(), options.origin_identifier.end());
        request.test = options.test;
        request.interlock_check = options.interlock_check;
        request.synchro_check = options.synchro_check;
        request.auto_select = options.auto_select;
        request.command_termination_timeout = options.termination_timeout;

        const auto result = execute_action(options, descriptor, control_session, request);

        std::optional<std::string> status_after;
        if (descriptor.status_object && transport.associated()) {
            if (const auto value = transport.read(*descriptor.status_object)) {
                status_after = value_text(*value);
                std::cout << "STATUS_AFTER " << *status_after << '\n';
            }
        }

        std::cout << "CONTROL_RESULT action=" << action_name(result.action)
                  << " completion=" << completion_name(result.completion)
                  << " accepted=" << (result.request_accepted ? "true" : "false")
                  << " termination="
                  << (result.command_termination_received ? "true" : "false")
                  << " ctlNum=" << static_cast<unsigned>(result.control_number)
                  << " mmsFailure=";
        if (result.mms_failure_code) {
            std::cout << *result.mms_failure_code << ':'
                      << data_access_error_name(*result.mms_failure_code);
        } else {
            std::cout << "none";
        }
        std::cout
                  << " addCause=" << add_cause_name(result.add_cause)
                  << " rawAddCause=" << result.raw_add_cause
                  << " message=\"" << result.message << "\"\n";

        std::cout << "NO_RETRY_EVIDENCE controlWrites=" << transport.writes.size();
        for (const auto& write : transport.writes) {
            std::cout << " write=" << write;
        }
        std::cout << '\n';
        std::cout << "PCAP_HINT filter=\"tcp.port == " << options.endpoint.port
                  << " && ip.addr == " << options.endpoint.host << "\"\n";

        write_evidence(
            options, descriptor, transport, status_before, status_after,
            result, live_session.association());
        live_session.disconnect();
        return result.success() ? 0 : 4;
    } catch (const std::exception& exception) {
        std::cerr << "Control interoperability probe failed: " << exception.what() << '\n';
        return 1;
    }
}
