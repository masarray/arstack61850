// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

bool allow_all(
    void*,
    ControlAction,
    const ControlObjectReference&,
    const ControlClientIdentity&,
    const ControlSequenceView&) noexcept {
    return true;
}

MmsTypeSpecification scalar(
    const MmsTypeKind kind,
    std::string name,
    const std::optional<std::uint32_t> size = std::nullopt) {
    MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    result.size = size;
    return result;
}

MmsTypeSpecification structure(
    std::string name,
    std::vector<MmsTypeSpecification> children) {
    MmsTypeSpecification result;
    result.kind = MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

MmsTypeSpecification origin_spec() {
    return structure("origin", {
        scalar(MmsTypeKind::unsigned_integer, "orCat"),
        scalar(MmsTypeKind::octet_string, "orIdent", 64U),
    });
}

MmsTypeSpecification oper_spec(const bool include_oper_tm = false) {
    std::vector<MmsTypeSpecification> children{
        scalar(MmsTypeKind::bit_string, "ctlVal", 2U),
    };
    if (include_oper_tm) {
        children.push_back(scalar(MmsTypeKind::utc_time, "operTm"));
    }
    children.push_back(origin_spec());
    children.push_back(scalar(MmsTypeKind::unsigned_integer, "ctlNum"));
    children.push_back(scalar(MmsTypeKind::utc_time, "T"));
    children.push_back(scalar(MmsTypeKind::boolean, "Test"));
    children.push_back(scalar(MmsTypeKind::bit_string, "Check", 2U));
    return structure("Oper", std::move(children));
}

MmsTypeSpecification cancel_spec() {
    return structure("Cancel", {
        scalar(MmsTypeKind::bit_string, "ctlVal", 2U),
        origin_spec(),
        scalar(MmsTypeKind::unsigned_integer, "ctlNum"),
        scalar(MmsTypeKind::utc_time, "T"),
        scalar(MmsTypeKind::boolean, "Test"),
        scalar(MmsTypeKind::bit_string, "Check", 2U),
    });
}

MmsObjectName name(std::string item) {
    return MmsObjectName::domain_specific("LD0", std::move(item));
}

MmsInformationReport positive_oper_report() {
    MmsInformationReport report;
    report.variable_references.push_back(name("CSWI1$CO$Pos$Oper"));
    return report;
}

MmsInformationReport ordinary_status_report() {
    MmsInformationReport report;
    report.variable_references.push_back(name("CSWI1$ST$Pos$stVal"));
    MmsInformationReportItem item;
    item.index = 0U;
    item.value = MmsDataValue::boolean(true);
    report.items.push_back(std::move(item));
    return report;
}

MmsInformationReport negative_error_report(const std::int64_t add_cause) {
    MmsInformationReport report;
    MmsInformationReportItem item;
    item.index = 0U;
    item.value = MmsDataValue::structure({
        MmsDataValue::visible_string("LD0/CSWI1.Pos"),
        MmsDataValue::integer(0),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(2U),
            MmsDataValue::octet_string(std::vector<std::uint8_t>{'H', 'M', 'I'}),
        }),
        MmsDataValue::unsigned_integer(7U),
        MmsDataValue::integer(add_cause),
    });
    report.items.push_back(std::move(item));
    return report;
}

class ScriptedControlTransport final : public ControlTransport {
public:
    bool is_associated{true};
    std::uint64_t id{0xA55AU};
    std::map<std::string, MmsDataValue> reads;
    std::map<std::string, MmsTypeSpecification> specifications;
    std::map<std::string, std::vector<std::string>> names;
    std::deque<ControlTransportWriteResult> write_results;
    std::deque<MmsInformationReport> reports;
    std::vector<std::string> events;
    std::vector<std::string> writes;
    std::size_t clear_count{};

    [[nodiscard]] bool associated() const noexcept override { return is_associated; }
    [[nodiscard]] std::uint64_t association_id() const noexcept override { return id; }

    [[nodiscard]] std::optional<MmsDataValue> read(
        const MmsObjectName& object,
        std::stop_token = {}) override {
        events.push_back("read:" + object.reference());
        const auto found = reads.find(object.reference());
        return found == reads.end() ? std::nullopt
                                    : std::optional<MmsDataValue>{found->second};
    }

    [[nodiscard]] std::optional<MmsTypeSpecification> variable_specification(
        const MmsObjectName& object,
        std::stop_token = {}) override {
        events.push_back("gvaa:" + object.reference());
        const auto found = specifications.find(object.reference());
        return found == specifications.end()
            ? std::nullopt
            : std::optional<MmsTypeSpecification>{found->second};
    }

    [[nodiscard]] std::vector<std::string> domain_variable_names(
        const std::string& domain,
        std::stop_token = {}) override {
        events.push_back("names:" + domain);
        const auto found = names.find(domain);
        return found == names.end() ? std::vector<std::string>{} : found->second;
    }

    [[nodiscard]] ControlTransportWriteResult write(
        const MmsObjectName& object,
        MmsDataValue,
        std::stop_token = {}) override {
        events.push_back("write:" + object.reference());
        writes.push_back(object.reference());
        if (write_results.empty()) {
            return {true, std::nullopt};
        }
        const auto result = write_results.front();
        write_results.pop_front();
        return result;
    }

    void clear_information_reports() override {
        ++clear_count;
        events.push_back("clear-reports");
        reports.clear();
        for (auto& report : reports_after_clear) {
            reports.push_back(std::move(report));
        }
        reports_after_clear.clear();
    }

    [[nodiscard]] bool wait_information_report(
        std::chrono::milliseconds,
        MmsInformationReport& report,
        std::stop_token = {}) override {
        events.push_back("wait-report");
        if (reports.empty()) {
            return false;
        }
        report = std::move(reports.front());
        reports.pop_front();
        return true;
    }

    std::vector<MmsInformationReport> reports_after_clear;
};

ControlObjectReference parsed_object() {
    ControlObjectReference object;
    CHECK(try_parse_control_object_reference("LD0/CSWI1.Pos", object));
    return object;
}

ControlObjectDescriptor descriptor(const ControlModel model) {
    ControlObjectDescriptor result;
    result.object = parsed_object();
    result.model = model;
    result.ctl_val_specification = scalar(MmsTypeKind::bit_string, "ctlVal", 2U);
    result.operate_specification = oper_spec();
    result.cancel_specification = cancel_spec();
    if (model == ControlModel::select_before_operate_enhanced) {
        auto sbow = oper_spec();
        sbow.name = "SBOw";
        result.select_with_value_specification = sbow;
    }
    result.sbo_timeout = std::chrono::milliseconds{10'000};
    result.operate_timeout = std::chrono::milliseconds{10'000};
    result.supports_command_termination = model_is_enhanced(model);
    result.supports_time_activated_operate = false;
    result.cdc = "DPC";
    return result;
}

ControlSessionOptions options() {
    ControlSessionOptions result;
    result.application_error_grace_period = std::chrono::milliseconds{1};
    result.guarded_policy.authorize = allow_all;
    return result;
}

ControlRequest request(const DoublePointValue value = DoublePointValue::on) {
    ControlRequest result;
    result.control_value = ControlValue::double_point(value);
    result.origin_category = OriginCategory::station_control;
    result.origin_identifier = {'H', 'M', 'I'};
    result.interlock_check = true;
    result.synchro_check = true;
    return result;
}

void discovery_matches_csharp_ctlmodel_gvaa_and_timeout_rules() {
    ScriptedControlTransport transport;
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CF$Pos$ctlModel", MmsDataValue::integer(2));
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CF$Pos$sboTimeout", MmsDataValue::unsigned_integer(1'500U));
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CF$Pos$operTimeout", MmsDataValue::integer(2'500));
    transport.specifications["LD0/CSWI1$CO$Pos$Oper"] = oper_spec();
    transport.specifications["LD0/CSWI1$CO$Pos$Cancel"] = cancel_spec();
    transport.names["LD0"] = {
        "CSWI1$CF$Pos$sboTimeout",
        "CSWI1$CF$Pos$operTimeout",
        "CSWI1$ST$Pos$stVal",
    };

    const auto found = ControlDescriptorDiscovery::discover(
        transport, "LD0/CSWI1.Pos");
    CHECK(found.model == ControlModel::select_before_operate_normal);
    CHECK(found.cdc == "DPC");
    CHECK(found.sbo_timeout == std::chrono::milliseconds{1'500});
    CHECK(found.operate_timeout == std::chrono::milliseconds{2'500});
    CHECK(found.status_object.has_value());
    CHECK(found.status_object->reference() == "LD0/CSWI1$ST$Pos$stVal");
    CHECK(found.status_functional_constraint == "ST");
    CHECK(found.operationally_ready());

    transport.specifications.erase("LD0/CSWI1$CO$Pos$Cancel");
    bool rejected_discovery = false;
    try {
        static_cast<void>(ControlDescriptorDiscovery::discover(
            transport, "LD0/CSWI1.Pos"));
    } catch (const std::runtime_error&) {
        rejected_discovery = true;
    }
    CHECK(rejected_discovery);
}

void direct_normal_is_exactly_one_oper_write() {
    ScriptedControlTransport transport;
    ControlObjectSession session{
        transport, descriptor(ControlModel::direct_normal), options()};
    const auto result = session.operate(request());
    CHECK(result.success());
    CHECK(result.completion == ControlCompletionState::accepted);
    CHECK(result.request_accepted);
    CHECK(result.control_number == 1U);
    CHECK(transport.writes.size() == 1U);
    CHECK(transport.writes.front() == "LD0/CSWI1$CO$Pos$Oper");
    CHECK(transport.clear_count == 1U);
    const auto clear = std::find(
        transport.events.begin(), transport.events.end(), "clear-reports");
    const auto write = std::find(
        transport.events.begin(), transport.events.end(),
        "write:LD0/CSWI1$CO$Pos$Oper");
    CHECK(clear < write);
}

void sbo_normal_select_oper_and_cancel_are_immutable() {
    ScriptedControlTransport transport;
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CO$Pos$SBO", MmsDataValue::visible_string("LD0/CSWI1.Pos"));
    ControlObjectSession session{
        transport, descriptor(ControlModel::select_before_operate_normal), options()};

    auto selected = session.select(request());
    CHECK(selected.success());
    CHECK(session.has_active_selection());
    CHECK(transport.writes.empty());

    const auto operated = session.operate(request());
    CHECK(operated.success());
    CHECK(transport.writes.size() == 1U);
    CHECK(transport.writes.front() == "LD0/CSWI1$CO$Pos$Oper");
    CHECK(!session.has_active_selection());

    selected = session.select(request());
    CHECK(selected.success());
    const auto cancelled = session.cancel();
    CHECK(cancelled.success());
    CHECK(transport.writes.back() == "LD0/CSWI1$CO$Pos$Cancel");
    CHECK(!session.has_active_selection());
}

void stale_sbo_sequence_is_cancelled_not_operated() {
    ScriptedControlTransport transport;
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CO$Pos$SBO", MmsDataValue::boolean(true));
    ControlObjectSession session{
        transport, descriptor(ControlModel::select_before_operate_normal), options()};
    CHECK(session.select(request(DoublePointValue::on)).success());

    const auto result = session.operate(request(DoublePointValue::off));
    CHECK(!result.success());
    CHECK(result.completion == ControlCompletionState::rejected);
    CHECK(transport.writes.size() == 1U);
    CHECK(transport.writes.front() == "LD0/CSWI1$CO$Pos$Cancel");
    CHECK(!session.has_active_selection());
}

void auto_select_performs_sbo_read_then_one_oper_write() {
    ScriptedControlTransport transport;
    transport.reads.insert_or_assign(
        "LD0/CSWI1$CO$Pos$SBO", MmsDataValue::visible_string("SEL-TOKEN"));
    ControlObjectSession session{
        transport, descriptor(ControlModel::select_before_operate_normal), options()};
    const auto result = session.operate(request());
    CHECK(result.success());
    CHECK(transport.writes.size() == 1U);
    CHECK(std::find(transport.events.begin(), transport.events.end(),
                    "read:LD0/CSWI1$CO$Pos$SBO") != transport.events.end());
}

void enhanced_oper_ignores_status_and_requires_exact_termination() {
    ScriptedControlTransport transport;
    transport.reports_after_clear.push_back(ordinary_status_report());
    transport.reports_after_clear.push_back(positive_oper_report());
    ControlObjectSession session{
        transport, descriptor(ControlModel::direct_enhanced), options()};
    const auto result = session.operate(request());
    CHECK(result.success());
    CHECK(result.completion == ControlCompletionState::positive_termination);
    CHECK(result.command_termination_received);
    CHECK(result.positive_termination);
    CHECK(transport.writes.size() == 1U);
}

void enhanced_negative_last_appl_error_is_returned() {
    ScriptedControlTransport transport;
    transport.reports_after_clear.push_back(negative_error_report(10));
    ControlObjectSession session{
        transport, descriptor(ControlModel::direct_enhanced), options()};
    const auto result = session.operate(request());
    CHECK(!result.success());
    CHECK(result.completion == ControlCompletionState::negative_termination);
    CHECK(result.request_accepted);
    CHECK(result.command_termination_received);
    CHECK(result.add_cause == AddCause::blocked_by_interlocking);
    CHECK(transport.writes.size() == 1U);
}

void enhanced_timeout_is_not_association_failure() {
    ScriptedControlTransport transport;
    auto desc = descriptor(ControlModel::direct_enhanced);
    desc.operate_timeout = std::chrono::milliseconds{1};
    ControlObjectSession session{transport, desc, options()};
    const auto result = session.operate(request());
    CHECK(!result.success());
    CHECK(result.completion == ControlCompletionState::timed_out);
    CHECK(result.request_accepted);
    CHECK(transport.associated());
    CHECK(transport.writes.size() == 1U);
}

void sbow_application_error_after_write_rejects_selection() {
    ScriptedControlTransport transport;
    transport.reports_after_clear.push_back(negative_error_report(11));
    ControlObjectSession session{
        transport,
        descriptor(ControlModel::select_before_operate_enhanced),
        options()};
    const auto result = session.select_with_value(request());
    CHECK(!result.success());
    CHECK(result.completion == ControlCompletionState::rejected);
    CHECK(result.request_accepted);
    CHECK(result.add_cause == AddCause::blocked_by_synchrocheck);
    CHECK(!session.has_active_selection());
    CHECK(transport.writes.size() == 1U);
    CHECK(transport.writes.front() == "LD0/CSWI1$CO$Pos$SBOw");
}

void default_authorization_still_fails_closed_at_live_session() {
    ScriptedControlTransport transport;
    ControlSessionOptions denied_options;
    ControlObjectSession session{
        transport, descriptor(ControlModel::direct_normal), denied_options};
    const auto result = session.operate(request());
    CHECK(!result.success());
    CHECK(result.completion == ControlCompletionState::rejected);
    CHECK(result.add_cause == AddCause::no_access_authority);
    CHECK(transport.writes.empty());
}

} // namespace

int main() {
    try {
        discovery_matches_csharp_ctlmodel_gvaa_and_timeout_rules();
        direct_normal_is_exactly_one_oper_write();
        sbo_normal_select_oper_and_cancel_are_immutable();
        stale_sbo_sequence_is_cancelled_not_operated();
        auto_select_performs_sbo_read_then_one_oper_write();
        enhanced_oper_ignores_status_and_requires_exact_termination();
        enhanced_negative_last_appl_error_is_returned();
        enhanced_timeout_is_not_association_failure();
        sbow_application_error_after_write_rejects_selection();
        default_authorization_still_fails_closed_at_live_session();
        std::cout << "Control session parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
