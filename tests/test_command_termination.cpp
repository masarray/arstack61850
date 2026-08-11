// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/command_termination.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

ControlObjectReference object() {
    ControlObjectReference result;
    CHECK(try_parse_control_object_reference("LD0/CSWI1.Pos", result));
    return result;
}

MmsObjectName reference(std::string item) {
    MmsObjectName result;
    result.kind = MmsObjectNameKind::domain_specific;
    result.domain = "LD0";
    result.item = std::move(item);
    return result;
}

MmsObjectName vmd_reference(std::string item) {
    MmsObjectName result;
    result.kind = MmsObjectNameKind::vmd_specific;
    result.item = std::move(item);
    return result;
}

MmsInformationReportItem value_item(MmsDataValue value, const std::size_t index = 0U) {
    MmsInformationReportItem item;
    item.index = index;
    item.value = std::move(value);
    return item;
}

MmsDataValue last_appl_error(
    std::string control_object,
    const std::int64_t error,
    const std::int64_t add_cause) {
    return MmsDataValue::structure({
        MmsDataValue::visible_string(std::move(control_object)),
        MmsDataValue::integer(error),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(2U),
            MmsDataValue::octet_string(std::vector<std::uint8_t>{'H', 'M', 'I'}),
        }),
        MmsDataValue::unsigned_integer(7U),
        MmsDataValue::integer(add_cause),
    });
}

bool allow_all(
    void*,
    ControlAction,
    const ControlObjectReference&,
    const ControlClientIdentity&,
    const ControlSequenceView&) noexcept {
    return true;
}

void reference_matching_follows_csharp_oracle() {
    const auto target = object();
    CHECK(CommandTerminationDecoder::matches_operate_reference(
        target, "LD0/CSWI1$CO$Pos$Oper"));
    CHECK(CommandTerminationDecoder::matches_operate_reference(
        target, "CSWI1.CO.Pos.Oper"));
    CHECK(!CommandTerminationDecoder::matches_operate_reference(
        target, "LD0/CSWI1$ST$Pos$stVal"));

    CHECK(CommandTerminationDecoder::matches_reported_reference(
        target, "LD0/CSWI1.Pos"));
    CHECK(CommandTerminationDecoder::matches_reported_reference(
        target, "LD0/CSWI1.Pos.stVal"));
    CHECK(CommandTerminationDecoder::matches_reported_reference(
        target, "LD0/CSWI1$CO$Pos$Oper"));
    CHECK(CommandTerminationDecoder::matches_reported_reference(
        target, "LD0/CSWI1$CO$Pos"));
    CHECK(!CommandTerminationDecoder::matches_reported_reference(
        target, "LD0/XCBR1.Pos.stVal"));
}

void positive_termination_requires_exact_oper_reference() {
    MmsInformationReport report;
    report.variable_references.push_back(reference("CSWI1$CO$Pos$Oper"));
    auto result = CommandTerminationDecoder::decode(report, object());
    CHECK(result.is_for_control_object);
    CHECK(result.is_termination);
    CHECK(result.positive);
    CHECK(result.control_error == ControlError::no_error);
    CHECK(result.add_cause == AddCause::none);
    CHECK(result.raw_add_cause == 25);

    MmsInformationReport ordinary;
    ordinary.variable_references.push_back(reference("CSWI1$ST$Pos$stVal"));
    ordinary.items.push_back(value_item(MmsDataValue::boolean(true)));
    result = CommandTerminationDecoder::decode(ordinary, object());
    CHECK(!result.is_for_control_object);
    CHECK(!result.is_termination);
}

void negative_last_appl_error_is_correlated_and_mapped() {
    MmsInformationReport report;
    // Embedded ctlObj is sufficient even if the report variable reference itself
    // is not the target object.
    report.variable_references.push_back(reference("LLN0$ST$Mod$stVal"));
    report.items.push_back(value_item(last_appl_error(
        "LD0/CSWI1.Pos", 0, 10)));

    const auto result = CommandTerminationDecoder::decode(report, object());
    CHECK(result.is_for_control_object);
    CHECK(result.is_termination);
    CHECK(!result.positive);
    CHECK(result.control_error == ControlError::no_error);
    CHECK(result.add_cause == AddCause::blocked_by_interlocking);
    CHECK(result.control_error_name == "no-error");
    CHECK(result.add_cause_name == "blocked-by-interlocking");
    CHECK(result.last_appl_error.has_value());
    CHECK(result.last_appl_error->control_object == "LD0/CSWI1.Pos");
}

void omitted_ctl_obj_layout_is_supported_when_report_correlates() {
    MmsInformationReport report;
    report.variable_references.push_back(reference("CSWI1$CO$Pos$Oper"));
    report.items.push_back(value_item(MmsDataValue::structure({
        MmsDataValue::integer(0),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(2U),
            MmsDataValue::octet_string(std::vector<std::uint8_t>{'H', 'M', 'I'}),
        }),
        MmsDataValue::unsigned_integer(7U),
        MmsDataValue::integer(25),
    })));

    const auto result = CommandTerminationDecoder::decode(report, object());
    CHECK(result.is_termination);
    CHECK(result.positive);
    CHECK(result.last_appl_error.has_value());
    CHECK(result.last_appl_error->control_object.empty());
    CHECK(result.add_cause == AddCause::none);
}

void generic_omitted_ctl_obj_requires_exact_sequence_correlation() {
    MmsInformationReport report;
    report.variable_references.push_back(vmd_reference("LastApplError"));
    report.items.push_back(value_item(MmsDataValue::structure({
        MmsDataValue::integer(3),
        MmsDataValue::structure({
            MmsDataValue::integer(7),
            MmsDataValue::octet_string(
                std::vector<std::uint8_t>{'A', 'R', 'S', 'T', 'A', 'C', 'K'}),
        }),
        MmsDataValue::unsigned_integer(41U),
        MmsDataValue::integer(8),
    })));

    const std::vector<std::uint8_t> expected_origin{
        'A', 'R', 'S', 'T', 'A', 'C', 'K'};
    const CommandCorrelation matching{7, expected_origin, 41U};
    auto result = CommandTerminationDecoder::decode(report, object(), &matching);
    CHECK(result.is_for_control_object);
    CHECK(result.is_termination);
    CHECK(!result.positive);
    CHECK(result.raw_control_error == 3);
    CHECK(result.control_error == ControlError::operator_test);
    CHECK(result.raw_add_cause == 8);
    CHECK(result.add_cause == AddCause::blocked_by_mode);
    CHECK(result.last_appl_error.has_value());
    CHECK(result.last_appl_error->control_object.empty());
    CHECK(result.last_appl_error->origin_category == 7);
    CHECK(result.last_appl_error->origin_identifier == expected_origin);
    CHECK(result.last_appl_error->control_number == 41U);

    const CommandCorrelation wrong_number{7, expected_origin, 42U};
    result = CommandTerminationDecoder::decode(report, object(), &wrong_number);
    CHECK(!result.is_for_control_object);
    CHECK(!result.is_termination);

    const std::vector<std::uint8_t> wrong_origin{'O', 'T', 'H', 'E', 'R'};
    const CommandCorrelation wrong_identity{7, wrong_origin, 41U};
    result = CommandTerminationDecoder::decode(report, object(), &wrong_identity);
    CHECK(!result.is_for_control_object);
    CHECK(!result.is_termination);

    result = CommandTerminationDecoder::decode(report, object());
    CHECK(!result.is_for_control_object);
    CHECK(!result.is_termination);
}

void uncorrelated_last_appl_error_cannot_complete_command() {
    MmsInformationReport report;
    report.variable_references.push_back(reference("XCBR1$ST$Pos$stVal"));
    report.items.push_back(value_item(last_appl_error(
        "LD0/XCBR1.Pos", 0, 11)));
    const auto result = CommandTerminationDecoder::decode(report, object());
    CHECK(!result.is_for_control_object);
    CHECK(!result.is_termination);
}

void unknown_numeric_codes_are_preserved_without_false_positive() {
    MmsInformationReport report;
    report.items.push_back(value_item(last_appl_error(
        "LD0/CSWI1.Pos", 99, 88)));
    const auto result = CommandTerminationDecoder::decode(report, object());
    CHECK(result.is_termination);
    CHECK(!result.positive);
    CHECK(result.raw_control_error == 99);
    CHECK(result.raw_add_cause == 88);
    CHECK(result.control_error == ControlError::unknown);
    CHECK(result.add_cause == AddCause::unknown);
    CHECK(result.control_error_name == "control-error-99");
    CHECK(result.add_cause_name == "add-cause-88");
}

void decoder_output_drives_enhanced_planner_completion() {
    GuardedControlPolicy policy;
    policy.authorize = allow_all;
    const auto target = object();
    GuardedControlPlanner planner{target, ControlModel::direct_enhanced, policy};
    constexpr std::uint8_t encoded_boolean[] = {0x83U, 0x01U, 0xFFU};
    constexpr std::uint8_t origin[] = {'H', 'M', 'I'};
    ControlSequenceView sequence;
    sequence.control_value = encoded_boolean;
    sequence.origin_identifier = origin;
    sequence.timestamp_token = 1U;
    const ControlClientIdentity client{0x1234U};
    auto decision = planner.operate(client, sequence, 100U);
    CHECK(decision.status == GuardedControlStatus::accepted_waiting_termination);

    MmsInformationReport report;
    report.items.push_back(value_item(last_appl_error(
        "LD0/CSWI1.Pos", 0, 11)));
    const auto termination = CommandTerminationDecoder::decode(report, target);
    CHECK(termination.is_termination);
    CHECK(!termination.positive);

    decision = planner.command_termination(
        termination.control_error, termination.add_cause);
    CHECK(decision.status == GuardedControlStatus::negative_termination);
    CHECK(decision.add_cause == AddCause::blocked_by_synchrocheck);
    CHECK(!planner.state(101U).waiting_for_termination);
}

void last_appl_error_needs_two_numeric_fields() {
    const auto invalid = MmsDataValue::structure({
        MmsDataValue::visible_string("LD0/CSWI1.Pos"),
        MmsDataValue::integer(0),
    });
    CHECK(!CommandTerminationDecoder::try_decode_last_appl_error(invalid).has_value());
}

} // namespace

int main() {
    try {
        reference_matching_follows_csharp_oracle();
        positive_termination_requires_exact_oper_reference();
        negative_last_appl_error_is_correlated_and_mapped();
        omitted_ctl_obj_layout_is_supported_when_report_correlates();
        generic_omitted_ctl_obj_requires_exact_sequence_correlation();
        uncorrelated_last_appl_error_cannot_complete_command();
        unknown_numeric_codes_are_preserved_without_false_positive();
        decoder_output_drives_enhanced_planner_completion();
        last_appl_error_needs_two_numeric_fields();
        std::cout << "CommandTermination tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
