// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/reporting.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ar::iec61850::mms;
using Test = std::pair<std::string, std::function<void()>>;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Fn>
void require_throws(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

MmsDataSetDirectoryResponse directory_fixture() {
    MmsDataSetDirectoryResponse response;
    response.invoke_id = 9U;
    MmsDataSetDirectoryMember first;
    first.object_name = MmsObjectName::domain_specific("LD0", "GGIO1$ST$Ind1$stVal");
    MmsDataSetDirectoryMember second;
    second.object_name = MmsObjectName::domain_specific("LD0", "GGIO1$ST$Ind2$stVal");
    response.members = {std::move(first), std::move(second)};
    return response;
}

MmsInformationReport realistic_report(
    const std::uint64_t sequence = 0U,
    const bool more_segments = false,
    const std::optional<std::uint64_t> sub_sequence = std::nullopt) {
    const std::array<std::uint8_t, 2> option_bytes{
        static_cast<std::uint8_t>(0x7FU),
        static_cast<std::uint8_t>(sub_sequence ? 0xC0U : 0x80U)};
    const std::array<std::uint8_t, 6> time{0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U};
    const std::array<std::uint8_t, 8> entry{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    const std::array<std::uint8_t, 1> inclusion{0xC0U};
    const std::array<std::uint8_t, 1> reason_a{0x80U};
    const std::array<std::uint8_t, 1> reason_b{0x10U};

    MmsInformationReport report;
    auto add = [&report](MmsDataValue value) {
        report.items.push_back({report.items.size(), std::move(value), std::nullopt});
    };
    add(MmsDataValue::visible_string("LD0/LLN0.RP.urcb01"));
    add(MmsDataValue::bit_string(sub_sequence ? 6U : 6U, option_bytes));
    add(MmsDataValue::unsigned_integer(sequence));
    add(MmsDataValue::binary_time(time));
    add(MmsDataValue::visible_string("LD0/LLN0.Events"));
    add(MmsDataValue::boolean(false));
    add(MmsDataValue::octet_string(entry));
    add(MmsDataValue::unsigned_integer(1U));
    if (sub_sequence) {
        add(MmsDataValue::unsigned_integer(*sub_sequence));
        add(MmsDataValue::boolean(more_segments));
    }
    add(MmsDataValue::bit_string(6U, inclusion));
    add(MmsDataValue::boolean(true));
    add(MmsDataValue::boolean(false));
    add(MmsDataValue::visible_string("LD0/GGIO1.Ind1.stVal"));
    add(MmsDataValue::visible_string("LD0/GGIO1.Ind2.stVal"));
    add(MmsDataValue::bit_string(2U, reason_a));
    add(MmsDataValue::bit_string(2U, reason_b));
    return report;
}

void inventory_builder_groups_datasets_and_rcbs() {
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variable_lists["LD0"] = {"LLN0$Events", "LLN0$Events", "LLN0$Measurements"};
    snapshot.domain_variables["LD0"] = {
        "LLN0$BR$brcb01$RptEna", "LLN0$BR$brcb01$DatSet", "LLN0$BR$brcb01$ConfRev",
        "LLN0$RP$urcb01$RptEna", "LLN0$RP$urcb01$DatSet", "GGIO1$ST$Ind1$stVal"};
    const auto inventory = MmsReportInventoryBuilder::build(snapshot);
    require(inventory.data_sets.size() == 2U, "DataSet inventory did not deduplicate names.");
    require(inventory.report_controls.size() == 2U, "RCB inventory did not group attributes.");
    require(inventory.buffered_count() == 1U && inventory.unbuffered_count() == 1U,
            "RCB mode counts are wrong.");
    require(inventory.data_sets[0].reference == "LD0/LLN0.Events", "IEC DataSet reference normalization failed.");
    require(inventory.report_controls[0].attribute_object_name("RptEna").item.find("$RptEna") != std::string::npos,
            "RCB attribute ObjectName is invalid.");
}

void dataset_directory_round_trip_and_normalization() {
    const MmsDataSetDirectoryRequest request{7U, MmsDataSetDirectoryCodec::parse_data_set_reference("LD0/LLN0.Events")};
    require(MmsDataSetDirectoryCodec::to_report_attribute_value("LD0/LLN0.Events") ==
                "LD0/LLN0$Events",
            "RCB DatSet attribute value did not use the MMS wire reference.");
    const auto request_wire = MmsDataSetDirectoryCodec::encode_request_p_data(request);
    const auto request_decoded = MmsDataSetDirectoryCodec::decode_request(request_wire);
    require(request_decoded.invoke_id == 7U && request_decoded.data_set_name.item == "LLN0$Events",
            "DataSet directory request round trip failed.");

    auto response = directory_fixture();
    const auto wire = MmsDataSetDirectoryCodec::encode_response_p_data(response);
    const auto decoded = MmsDataSetDirectoryCodec::decode_response(wire, 9U);
    require(decoded.members.size() == 2U, "DataSet directory member count mismatch.");
    require(decoded.members[0].functional_constraint == "ST", "Functional constraint normalization failed.");
    require(decoded.members[0].user_reference == "LD0/GGIO1.Ind1.stVal", "Member user reference normalization failed.");
    require(decoded.members[0].confidence == 100U, "Member confidence is not exact.");
}

void information_report_round_trip() {
    auto report = realistic_report();
    report.variable_references = {MmsObjectName::domain_specific("LD0", "LLN0$RP$urcb01")};
    const auto encoded = MmsInformationReportCodec::encode_p_data(report);
    require(MmsInformationReportCodec::is_information_report(encoded), "InformationReport was not recognized.");
    const auto decoded = MmsInformationReportCodec::decode(encoded);
    require(decoded.items.size() == report.items.size(), "InformationReport item count changed.");
    require(decoded.variable_references.size() == 1U, "InformationReport variable reference was lost.");
}

void information_report_variable_list_name_is_supported() {
    using ar::iec61850::asn1::BerWriter;
    const auto object_name = MmsServiceCodec::encode_object_name(
        MmsObjectName::domain_specific("LD0", "LLN0$DynamicSet"));
    const auto value = MmsDataCodec::encode(
        MmsDataValue::visible_string("LD0/LLN0.BR.brcbA01"));
    const auto result_list = BerWriter::encode_tlv(0xA0U, value);

    const std::array specifications{
        BerWriter::encode_tlv(0xA1U, object_name),
        object_name};
    for (const auto& specification : specifications) {
        auto body = specification;
        body.insert(body.end(), result_list.begin(), result_list.end());
        const auto information_report = BerWriter::encode_tlv(0xA0U, body);
        const auto pdu = BerWriter::encode_tlv(0xA3U, information_report);
        const auto decoded = MmsInformationReportCodec::decode(pdu);
        require(decoded.variable_references.size() == 1U,
                "InformationReport variableListName was not decoded.");
        require(decoded.variable_references.front().reference() ==
                    "LD0/LLN0$DynamicSet",
                "InformationReport variableListName changed during decode.");
        require(decoded.items.size() == 1U,
                "InformationReport access-result list was not preserved.");
    }
}

void exact_report_mapping_decodes_optional_fields() {
    const auto directory = MmsDataSetDirectoryCodec::decode_response(
        MmsDataSetDirectoryCodec::encode_response_pdu(directory_fixture()), 9U);
    const auto frame = MmsReportFrameMapper::map(realistic_report(17U), directory.members);
    require(frame.header.report_id == "LD0/LLN0.RP.urcb01", "RptID decode failed.");
    require(frame.header.sequence_number == 17U, "SqNum decode failed.");
    require(frame.header.configuration_revision == 1U, "ConfRev decode failed.");
    require(frame.included_data_set_indexes == std::vector<std::size_t>({0U, 1U}), "Inclusion bit string decode failed.");
    require(frame.values.size() == 2U, "Included value count mismatch.");
    require(frame.values[0].data_reference == "LD0/GGIO1.Ind1.stVal", "Data-reference decode failed.");
    require(frame.values[0].reason_for_inclusion.has("data-change"), "Reason-for-inclusion decode failed.");
    require(frame.values[1].reason_for_inclusion.has("integrity"), "Second reason-for-inclusion decode failed.");
    require(frame.decoder_mode == "opt-fields-exact", "Unexpected report decoder mode.");
}


void csharp_exact_report_shape_maps_byte_semantics() {
    const std::array<std::uint8_t, 2> options{0x7BU, 0x80U};
    const std::array<std::uint8_t, 6> time{0x04U, 0xB1U, 0xEEU, 0x6AU, 0x3CU, 0x8FU};
    const std::array<std::uint8_t, 8> entry{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x14U};
    const std::array<std::uint8_t, 1> inclusion{0xC0U};
    const std::array<std::uint8_t, 1> reason{0x04U};
    MmsInformationReport report;
    auto add = [&report](MmsDataValue value) {
        report.items.push_back({report.items.size(), std::move(value), std::nullopt});
    };
    add(MmsDataValue::visible_string("LD0/LLN0$BR$brcbA01"));
    add(MmsDataValue::bit_string(6U, options));
    add(MmsDataValue::unsigned_integer(0U));
    add(MmsDataValue::binary_time(time));
    add(MmsDataValue::visible_string("LD0/LLN0$DataSet"));
    add(MmsDataValue::boolean(false));
    add(MmsDataValue::octet_string(entry));
    add(MmsDataValue::unsigned_integer(1U));
    add(MmsDataValue::bit_string(6U, inclusion));
    add(MmsDataValue::boolean(true));
    add(MmsDataValue::boolean(false));
    add(MmsDataValue::bit_string(2U, reason));
    add(MmsDataValue::bit_string(2U, reason));

    const auto frame = MmsReportFrameMapper::map(report, {});
    require(!frame.header.optional_fields.has("data-reference"),
            "C# exact OptFlds incorrectly enabled data-reference.");
    require(frame.header.data_set_reference == "LD0/LLN0$DataSet", "C# exact DatSet changed.");
    require(frame.values.size() == 2U, "C# exact included value count mismatch.");
    require(frame.values[0].reason_for_inclusion.has("application-trigger"),
            "C# exact reason bit mapping failed.");
}

void report_failure_result_is_preserved() {
    auto report = realistic_report();
    report.items[9U] = {9U, std::nullopt, 3U};
    const auto directory = MmsDataSetDirectoryCodec::decode_response(
        MmsDataSetDirectoryCodec::encode_response_pdu(directory_fixture()), 9U);
    const auto frame = MmsReportFrameMapper::map(report, directory.members);
    require(frame.values[0].failure_code == 3U && !frame.values[0].value,
            "Report DataAccessError was not preserved.");
}

void rcb_read_mapping_classifies_availability() {
    MmsReportControlCandidate candidate;
    candidate.domain = "LD0";
    candidate.logical_node = "LLN0";
    candidate.functional_constraint = "RP";
    candidate.name = "urcb01";
    const std::array<std::string, 6> attrs{"DatSet", "RptID", "ConfRev", "RptEna", "Resv", "OptFlds"};
    const auto request = MmsReportControlStateMapper::build_read_request(20U, candidate, attrs);
    require(request.variables.size() == attrs.size(), "RCB read request variable count mismatch.");

    const std::array<std::uint8_t, 2> options{0x7FU, 0x80U};
    MmsReadResponse response;
    response.invoke_id = 20U;
    response.results = {
        {MmsDataValue::visible_string("LD0/LLN0.Events"), std::nullopt},
        {MmsDataValue::visible_string("LD0/LLN0.RP.urcb01"), std::nullopt},
        {MmsDataValue::unsigned_integer(1U), std::nullopt},
        {MmsDataValue::boolean(false), std::nullopt},
        {MmsDataValue::boolean(false), std::nullopt},
        {MmsDataValue::bit_string(6U, options), std::nullopt},
    };
    const auto directory = MmsDataSetDirectoryCodec::decode_response(
        MmsDataSetDirectoryCodec::encode_response_pdu(directory_fixture()), 9U);
    const auto state = MmsReportControlStateMapper::map_read_response(candidate, attrs, response, &directory);
    require(state.availability == MmsRcbAvailability::available, "URCB availability classification failed.");
    require(state.availability_confidence == MmsRcbAvailabilityConfidence::exact,
            "URCB availability confidence should be exact.");
}

void sequence_tracker_detects_gap_duplicate_and_changes() {
    MmsReportSequenceTracker tracker;
    auto first = MmsReportFrameMapper::map(realistic_report(1U), {});
    const auto first_observation = tracker.observe(first);
    require(first_observation.events.front().kind == MmsReportContinuityEventKind::first_report,
            "First report event missing.");
    auto gap = MmsReportFrameMapper::map(realistic_report(4U), {});
    gap.header.configuration_revision = 2U;
    gap.header.data_set_reference = "LD0/LLN0.Other";
    gap.header.buffer_overflow = true;
    const auto gap_observation = tracker.observe(gap);
    require(gap_observation.state.gap_count == 1U && gap_observation.state.missing_sequence_count == 2U,
            "Sequence gap accounting failed.");
    const auto duplicate = tracker.observe(gap);
    require(duplicate.state.duplicate_count == 1U, "Duplicate sequence accounting failed.");
}

void segmentation_continuity_is_tracked() {
    MmsReportSequenceTracker tracker;
    const auto first = MmsReportFrameMapper::map(realistic_report(5U, true, 0U), {});
    const auto continued = MmsReportFrameMapper::map(realistic_report(5U, true, 1U), {});
    const auto completed = MmsReportFrameMapper::map(realistic_report(5U, false, 2U), {});
    require(tracker.observe(first).events.back().kind == MmsReportContinuityEventKind::segmentation_started,
            "Segmentation start was not detected.");
    require(tracker.observe(continued).events.back().kind == MmsReportContinuityEventKind::segmentation_continued,
            "Segmentation continuation was not detected.");
    require(tracker.observe(completed).events.back().kind == MmsReportContinuityEventKind::segmentation_completed,
            "Segmentation completion was not detected.");
}

void offline_monitor_enforces_frame_and_stream_limits() {
    MmsOfflineReportMonitor monitor{{2U, 2U}};
    for (std::uint64_t seq = 1U; seq <= 3U; ++seq) {
        static_cast<void>(monitor.ingest(MmsReportFrameMapper::map(realistic_report(seq), {})));
    }
    auto other = MmsReportFrameMapper::map(realistic_report(1U), {});
    other.header.report_id = "other";
    static_cast<void>(monitor.ingest(other));
    auto third = other;
    third.header.report_id = "third";
    static_cast<void>(monitor.ingest(third));
    const auto snapshots = monitor.snapshots();
    require(snapshots.size() == 2U, "Monitor stream limit was not enforced.");
    require(std::all_of(snapshots.begin(), snapshots.end(), [](const auto& item) {
        return item.recent_frames.size() <= 2U;
    }), "Monitor frame limit was not enforced.");
}

void malformed_and_bound_paths_are_rejected() {
    require_throws([] {
        static_cast<void>(MmsInformationReportCodec::decode(std::array<std::uint8_t, 2>{0xA3U, 0x00U}));
    }, "Malformed InformationReport was accepted.");
    require_throws([] {
        MmsOfflineReportMonitor monitor{{0U, 1U}};
        static_cast<void>(monitor);
    }, "Zero stream limit was accepted.");
    auto report = realistic_report();
    report.items.push_back({report.items.size(), MmsDataValue::boolean(true), std::nullopt});
    require_throws([&report] {
        static_cast<void>(MmsReportFrameMapper::map(report, {}));
    }, "Trailing report access result was accepted.");
}

} // namespace

int main() {
    const std::vector<Test> tests{
        {"inventory builder", inventory_builder_groups_datasets_and_rcbs},
        {"DataSet directory", dataset_directory_round_trip_and_normalization},
        {"InformationReport", information_report_round_trip},
        {"InformationReport variableListName", information_report_variable_list_name_is_supported},
        {"exact report mapping", exact_report_mapping_decodes_optional_fields},
        {"C# exact report shape", csharp_exact_report_shape_maps_byte_semantics},
        {"report failure result", report_failure_result_is_preserved},
        {"RCB state mapping", rcb_read_mapping_classifies_availability},
        {"sequence continuity", sequence_tracker_detects_gap_duplicate_and_changes},
        {"segmentation continuity", segmentation_continuity_is_tracked},
        {"bounded monitor", offline_monitor_enforces_frame_and_stream_limits},
        {"malformed and bounds", malformed_and_bound_paths_are_rejected},
    };
    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << passed << '/' << tests.size() << " reporting groups.\n";
    return 0;
}
