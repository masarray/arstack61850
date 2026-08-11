// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/sampled_values/rational_schedule.hpp"
#include "ariec61850/scl/dataset_reference.hpp"
#include "ariec61850/scl/parser.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{ARIEC61850_SOURCE_DIR} / "tests" / "fixtures" / "scl" / name;
}

template <typename Function>
void check_throws(Function&& function) {
    bool thrown{};
    try {
        function();
    } catch (const std::exception&) {
        thrown = true;
    }
    CHECK(thrown);
}

void parser_extracts_minimal_station_semantics() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("minimal-station.scd"));
    CHECK(document.source_name == "minimal-station.scd");
    CHECK(document.namespace_uri == "http://www.iec.ch/61850/2003/SCL");
    CHECK(document.header_id == "AR_MINIMAL_STATION");
    CHECK(document.header_version == "1");
    CHECK(document.header_revision == "A");
    CHECK(document.edition == SclEdition::edition2);
    CHECK(document.ieds.size() == 1U);
    CHECK(document.ieds[0].name == "MU01");
    CHECK(document.data_sets.size() == 2U);
    CHECK(document.sampled_values_streams.size() == 1U);
    CHECK(document.goose_streams.size() == 1U);
    CHECK(document.report_controls.size() == 1U);
    CHECK(document.conflicts.empty());

    const auto& sampled_values = document.sampled_values_streams[0];
    CHECK(sampled_values.control_block_reference == "MU01LD0/LLN0$SV$MSVCB01");
    CHECK(sampled_values.sv_id == "MU01LD0/LLN0$MSVCB01");
    CHECK(sampled_values.data_set_reference == "MU01LD0/LLN0$dsSV");
    CHECK(sampled_values.address.app_id == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(0x4001U)});
    CHECK(sampled_values.address.app_id_text == "0x4001");
    CHECK(sampled_values.address.destination_mac_text == "01:0C:CD:04:00:01");
    CHECK(sampled_values.address.destination_mac.has_value());
    CHECK(sampled_values.address.vlan_id == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(200U)});
    CHECK(sampled_values.address.vlan_priority == std::optional<std::uint8_t>{
        static_cast<std::uint8_t>(4U)});
    CHECK(sampled_values.configuration_revision == 3U);
    CHECK(sampled_values.sample_rate == 4000U);
    CHECK(sampled_values.sample_mode == "SmpPerSec");
    CHECK(sampled_values.no_asdu == 1U);
    CHECK(sampled_values.entries.size() == 2U);
    CHECK(sampled_values.entries[0].signal_reference == "MU01/LD0/TCTR1.Amp.instMag.i [MX]");
    CHECK(sampled_values.entries[0].cdc == "SAV");
    CHECK(sampled_values.entries[0].basic_type == "INT32");
    CHECK(sampled_values.entries[1].is_quality);

    const auto& goose = document.goose_streams[0];
    CHECK(goose.control_block_reference == "MU01LD0/LLN0$GO$GCB01");
    CHECK(goose.go_id == "trip-goose");
    CHECK(goose.data_set_reference == "MU01LD0/LLN0$dsGO");
    CHECK(goose.address.app_id == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(0x1001U)});
    CHECK(goose.address.destination_mac_text == "01:0C:CD:01:00:01");
    CHECK(goose.min_time_milliseconds == 4U);
    CHECK(goose.max_time_milliseconds == 1000U);
    CHECK(goose.entries.size() == 3U);
    CHECK(goose.entries[0].cdc == "DPC");
    CHECK(goose.entries[1].is_quality);
    CHECK(goose.entries[2].is_timestamp);

    const auto& report = document.report_controls[0];
    CHECK(report.control_block_reference == "MU01LD0/LLN0$RP$URCB01");
    CHECK(!report.buffered);
    CHECK(report.indexed);
    CHECK(report.data_set_reference == "MU01LD0/LLN0$dsGO");
    CHECK(report.data_set_binding_status == SclDataSetBindingStatus::resolved);
    CHECK(report.entries.size() == 3U);
}

void parser_extracts_multiple_sampled_values_streams_and_conflicts() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("compact-multi-stream.scd"));
    CHECK(document.ieds.size() == 3U);
    CHECK(document.data_sets.size() == 3U);
    CHECK(document.sampled_values_streams.size() == 3U);
    CHECK(document.goose_streams.empty());
    CHECK(document.report_controls.empty());

    for (std::size_t index = 0U; index < document.sampled_values_streams.size(); ++index) {
        const auto& stream = document.sampled_values_streams[index];
        CHECK(stream.sample_rate == 4000U);
        CHECK(stream.sample_mode == "SmpPerSec");
        CHECK(stream.no_asdu == 1U);
        CHECK(stream.entries.size() == 16U);
        CHECK(stream.entries[0].cdc == "SAV");
        CHECK(stream.entries[0].basic_type == "INT32");
        CHECK(stream.entries[1].is_quality);
        CHECK(stream.address.app_id == std::optional<std::uint16_t>{
            static_cast<std::uint16_t>(0x4000U)});
        CHECK(stream.address.vlan_id == std::optional<std::uint16_t>{
            static_cast<std::uint16_t>(0U)});
        CHECK(stream.address.vlan_priority == std::optional<std::uint8_t>{
            static_cast<std::uint8_t>(4U)});
        CHECK(stream.sv_id == "MU01_SV" + std::to_string(index + 1U));
    }

    CHECK(std::any_of(
        document.conflicts.begin(),
        document.conflicts.end(),
        [](const SclConflict& conflict) {
            return conflict.kind == "SV" && conflict.key == "APPID 0x4000";
        }));
}

void parser_compiles_structured_4800_sv_profile_without_drift() {
    using namespace ar::iec61850::sampled_values;
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("sv-4800-structured-4i4v.scd"));
    CHECK(document.sampled_values_streams.size() == 1U);
    CHECK(document.data_sets.size() == 1U);
    CHECK(document.warnings.empty());

    const auto& stream = document.sampled_values_streams.front();
    CHECK(stream.address.destination_mac_text == "01:0C:CD:04:00:00");
    CHECK(stream.address.app_id == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(0x4001U)});
    CHECK(stream.address.vlan_id == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(0U)});
    CHECK(stream.address.vlan_priority == std::optional<std::uint8_t>{
        static_cast<std::uint8_t>(4U)});
    CHECK(stream.configuration_revision == 100U);
    CHECK(stream.sample_rate == 4800U);
    CHECK(stream.sample_mode == "SmpPerSec");
    CHECK(stream.no_asdu == 1U);
    CHECK(stream.smv_options.element_present);
    CHECK(!stream.smv_options.refresh_time);
    CHECK(!stream.smv_options.sample_synchronized);
    CHECK(!stream.smv_options.sample_rate);
    CHECK(!stream.smv_options.data_set);
    CHECK(!stream.smv_options.security);
    CHECK(!stream.smv_options.synch_source_id);
    CHECK(stream.entries.size() == 16U);

    for (std::size_t channel = 0U; channel < 8U; ++channel) {
        const auto& value = stream.entries[channel * 2U];
        const auto& quality = stream.entries[channel * 2U + 1U];
        CHECK(value.da_name == "instMag.i");
        CHECK(value.cdc == "SAV");
        CHECK(value.basic_type == "INT32");
        CHECK(!value.is_quality);
        CHECK(quality.da_name == "q");
        CHECK(quality.cdc == "SAV");
        CHECK(quality.basic_type == "Quality");
        CHECK(quality.is_quality);
    }

    SvPublisherProfileCompileContext context;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4800U);
    const auto compiled = SvPublisherProfileCompiler::compile(stream, context);
    CHECK(compiled.ok());
    CHECK(compiled.profile.has_value());
    const auto& profile = *compiled.profile;
    CHECK(profile.app_id == 0x4001U);
    CHECK(profile.vlan_present);
    CHECK(profile.vlan_id == 0U);
    CHECK(profile.vlan_priority == 4U);
    CHECK(profile.configuration_revision == 100U);
    CHECK(profile.sample_mode == SvSampleMode::samples_per_second);
    CHECK(profile.publisher_rate_hz == std::optional<std::uint32_t>{4800U});
    CHECK(profile.sample_counter_policy == SvSampleCounterPolicy::explicit_modulus);
    CHECK(profile.sample_counter_modulus == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(4800U)});
    CHECK(profile.channels.size() == 16U);
    CHECK(profile.payload_size_bytes == 64U);
    CHECK(profile.asdu_options.element_present);

    RationalTickSchedule schedule_4800{1'000'000U, 4800U};
    std::uint64_t total_ticks{};
    std::uint32_t intervals_208{};
    std::uint32_t intervals_209{};
    for (std::uint32_t sample = 0U; sample < 4800U; ++sample) {
        const auto interval = schedule_4800.next_interval_ticks();
        total_ticks += interval;
        if (interval == 208U) {
            ++intervals_208;
        } else if (interval == 209U) {
            ++intervals_209;
        } else {
            CHECK(false);
        }
    }
    CHECK(total_ticks == 1'000'000U);
    CHECK(intervals_208 == 3200U);
    CHECK(intervals_209 == 1600U);

    RationalTickSchedule schedule_4000{1'000'000U, 4000U};
    total_ticks = 0U;
    for (std::uint32_t sample = 0U; sample < 4000U; ++sample) {
        const auto interval = schedule_4000.next_interval_ticks();
        CHECK(interval == 250U);
        total_ticks += interval;
    }
    CHECK(total_ticks == 1'000'000U);
}

void dataset_reference_resolver_accepts_canonical_and_local_forms() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("minimal-station.scd"));
    const auto resolve = [&document](const std::string& reference) {
        return SclDataSetReferenceResolver::resolve(
            document.data_sets,
            "MU01",
            "LD0",
            "LLN0",
            reference);
    };

    for (const auto& reference : std::vector<std::string>{
             "dsGO",
             "LLN0$dsGO",
             "LLN0$DS$dsGO",
             "LLN0.dsGO",
             "MU01LD0/LLN0$dsGO"}) {
        const auto resolution = resolve(reference);
        CHECK(resolution.status == SclDataSetBindingStatus::resolved);
        CHECK(resolution.data_set != nullptr);
        CHECK(resolution.data_set->name == "dsGO");
        CHECK(resolution.canonical_reference == "MU01LD0/LLN0$dsGO");
    }

    const auto unresolved = resolve("missing");
    CHECK(unresolved.status == SclDataSetBindingStatus::unresolved);
    CHECK(unresolved.data_set == nullptr);
    CHECK(unresolved.canonical_reference == "MU01LD0/LLN0$missing");

    const auto not_specified = resolve("  ");
    CHECK(not_specified.status == SclDataSetBindingStatus::not_specified);
}

void parser_detects_duplicate_ieds_and_missing_dataset_references() {
    using namespace ar::iec61850::scl;

    constexpr std::string_view xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0"><LN0 lnClass="LLN0">
    <GSEControl name="GCB1" datSet="missing" appID="g1" confRev="1"/>
  </LN0></LDevice></Server></AccessPoint></IED>
  <IED name="IED1"/>
</SCL>)xml";

    const auto document = SclParser{}.parse(xml, "conflict.scd");
    CHECK(std::any_of(
        document.conflicts.begin(),
        document.conflicts.end(),
        [](const SclConflict& conflict) {
            return conflict.kind == "IED" && conflict.key == "IED1";
        }));
    CHECK(std::any_of(
        document.warnings.begin(),
        document.warnings.end(),
        [](const std::string& warning) {
            return warning.find("references missing DataSet 'missing'") != std::string::npos;
        }));
}

void parser_detects_editions_from_root_metadata() {
    using namespace ar::iec61850::scl;

    const auto edition2 = SclParser{}.parse(
        R"xml(<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B"><Header id="ED2"/></SCL>)xml");
    CHECK(edition2.edition == SclEdition::edition2);

    const auto edition21 = SclParser{}.parse(
        R"xml(<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4"><Header id="ED21"/></SCL>)xml");
    CHECK(edition21.edition == SclEdition::edition21);

    const auto edition1 = SclParser{}.parse(
        R"xml(<SCL xmlns="http://www.iec.ch/61850/2003/SCL"><Header id="ED1"/></SCL>)xml");
    CHECK(edition1.edition == SclEdition::edition1);
}

void parser_supports_prefixed_namespaces_and_predefined_entities() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.parse(
        R"xml(<scl:SCL xmlns:scl="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B"><scl:Header id="A &amp; B"/></scl:SCL>)xml");
    CHECK(document.namespace_uri == "http://www.iec.ch/61850/2003/SCL");
    CHECK(document.header_id == "A & B");
    CHECK(document.edition == SclEdition::edition2);
}

void secure_xml_reader_rejects_dtd_entities_and_malformed_documents() {
    using namespace ar::iec61850::scl;

    check_throws([] {
        static_cast<void>(SclParser{}.parse(
            R"xml(<!DOCTYPE SCL [<!ENTITY x "boom">]><SCL xmlns="http://www.iec.ch/61850/2003/SCL"><Header id="&x;"/></SCL>)xml"));
    });
    check_throws([] {
        static_cast<void>(SclParser{}.parse(
            R"xml(<SCL xmlns="http://www.iec.ch/61850/2003/SCL"><Header id="&vendor;"/></SCL>)xml"));
    });
    check_throws([] {
        static_cast<void>(SclParser{}.parse("<SCL><Header></SCL>"));
    });
    check_throws([] {
        static_cast<void>(SclParser{}.parse("<NotScl/>"));
    });
    check_throws([] {
        static_cast<void>(SclParser{}.parse("   "));
    });
    check_throws([] {
        std::string deeply_nested = "<SCL>";
        for (std::size_t index = 0U; index < 130U; ++index) {
            deeply_nested += "<Private>";
        }
        for (std::size_t index = 0U; index < 130U; ++index) {
            deeply_nested += "</Private>";
        }
        deeply_nested += "</SCL>";
        static_cast<void>(SclParser{}.parse(deeply_nested));
    });
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"SCL minimal station", parser_extracts_minimal_station_semantics},
        {"SCL multi-stream", parser_extracts_multiple_sampled_values_streams_and_conflicts},
        {"SCL structured 4800 SV profile", parser_compiles_structured_4800_sv_profile_without_drift},
        {"SCL dataset references", dataset_reference_resolver_accepts_canonical_and_local_forms},
        {"SCL conflicts and warnings", parser_detects_duplicate_ieds_and_missing_dataset_references},
        {"SCL edition detection", parser_detects_editions_from_root_metadata},
        {"SCL prefixed namespace", parser_supports_prefixed_namespaces_and_predefined_entities},
        {"SCL secure XML", secure_xml_reader_rejects_dtd_entities_and_malformed_documents},
    };

    std::size_t passed{};
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

    std::cout << "Passed " << passed << '/' << tests.size() << " SCL tests.\n";
    return 0;
}
