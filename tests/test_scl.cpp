// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/sampled_values/rational_schedule.hpp"
#include "ariec61850/scl/dataset_reference.hpp"
#include "ariec61850/scl/exporter.hpp"
#include "ariec61850/scl/parser.hpp"
#include "ariec61850/simulation/ied_simulator_profile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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
    CHECK(report.max_clients == 1U);
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

    const auto& configured_data_set = document.data_sets.front();
    CHECK(configured_data_set.entries.size() == 8U);
    CHECK(configured_data_set.expanded_entries.size() == 16U);
    CHECK(std::all_of(
        configured_data_set.entries.begin(),
        configured_data_set.entries.end(),
        [](const SclDataSetEntry& entry) { return entry.da_name.empty(); }));

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
    context.profile_family = SvProfileFamily::iec61850_9_2;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4800U);
    const auto compiled = SvPublisherProfileCompiler::compile(stream, context);
    CHECK(compiled.ok());
    CHECK(compiled.profile.has_value());
    const auto& profile = *compiled.profile;
    CHECK(profile.app_id == 0x4001U);
    CHECK(profile.vlan_present);
    CHECK(profile.vlan_id == 0U);
    CHECK(profile.vlan_priority == 4U);
    CHECK(profile.schema_version == 3U);
    CHECK(profile.profile_family == SvProfileFamily::iec61850_9_2);
    CHECK(profile.profile_family_resolution == SvProfileFamilyResolution::resolved);
    CHECK(profile.transport_mode == SvTransportMode::multicast);
    CHECK(profile.configuration_revision == 100U);
    CHECK(profile.timing.sampling_basis == SvSampleMode::samples_per_second);
    CHECK(profile.timing.configured_sample_rate == 4800U);
    CHECK(profile.timing.asdus_per_frame == 1U);
    CHECK(profile.timing.resolved());
    CHECK(profile.timing.samples_per_second.has_value());
    CHECK(profile.timing.samples_per_second->numerator == 4800U);
    CHECK(profile.timing.samples_per_second->denominator == 1U);
    CHECK(profile.timing.frames_per_second.has_value());
    CHECK(profile.timing.frames_per_second->numerator == 4800U);
    CHECK(profile.timing.frames_per_second->denominator == 1U);
    CHECK(profile.timing.exact_frame_rate_hz() == std::optional<std::uint32_t>{4800U});
    CHECK(profile.sample_counter_policy == SvSampleCounterPolicy::explicit_modulus);
    CHECK(profile.sample_counter_modulus == std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(4800U)});
    CHECK(profile.channels.size() == 16U);
    CHECK(profile.payload_size_bytes == 64U);
    CHECK(profile.asdu_options.element_present);

    auto maximum_vlan_stream = stream;
    maximum_vlan_stream.address.vlan_id = static_cast<std::uint16_t>(4094U);
    const auto maximum_vlan = SvPublisherProfileCompiler::compile(maximum_vlan_stream, context);
    CHECK(maximum_vlan.ok());
    CHECK(maximum_vlan.profile.has_value());
    CHECK(maximum_vlan.profile->vlan_id == 4094U);

    auto reserved_vlan_stream = stream;
    reserved_vlan_stream.address.vlan_id = static_cast<std::uint16_t>(4095U);
    const auto reserved_vlan = SvPublisherProfileCompiler::compile(reserved_vlan_stream, context);
    CHECK(!reserved_vlan.ok());
    CHECK(!reserved_vlan.profile.has_value());
    CHECK(std::any_of(
        reserved_vlan.errors.begin(), reserved_vlan.errors.end(),
        [](const std::string& error) {
            return error.find("0..4094") != std::string::npos;
        }));

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

void publisher_profile_separates_sampling_from_frame_cadence() {
    using namespace ar::iec61850::sampled_values;
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("sv-4800-structured-4i4v.scd"));
    CHECK(document.sampled_values_streams.size() == 1U);
    const auto source = document.sampled_values_streams.front();

    SvPublisherProfileCompileContext context;
    context.profile_family = SvProfileFamily::iec61850_9_2;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4800U);

    auto two_asdu = source;
    two_asdu.no_asdu = 2U;
    const auto two_asdu_compiled = SvPublisherProfileCompiler::compile(two_asdu, context);
    CHECK(two_asdu_compiled.ok());
    CHECK(two_asdu_compiled.profile.has_value());
    const auto& two_asdu_timing = two_asdu_compiled.profile->timing;
    CHECK(two_asdu_timing.configured_sample_rate == 4800U);
    CHECK(two_asdu_timing.asdus_per_frame == 2U);
    CHECK(two_asdu_timing.exact_sample_rate_hz() == std::optional<std::uint32_t>{4800U});
    CHECK(two_asdu_timing.exact_frame_rate_hz() == std::optional<std::uint32_t>{2400U});

    auto six_asdu = source;
    six_asdu.sample_rate = 14400U;
    six_asdu.no_asdu = 6U;
    context.sample_counter_modulus = static_cast<std::uint16_t>(14400U);
    const auto six_asdu_compiled = SvPublisherProfileCompiler::compile(six_asdu, context);
    CHECK(six_asdu_compiled.ok());
    CHECK(six_asdu_compiled.profile.has_value());
    CHECK(six_asdu_compiled.profile->timing.exact_sample_rate_hz() ==
          std::optional<std::uint32_t>{14400U});
    CHECK(six_asdu_compiled.profile->timing.exact_frame_rate_hz() ==
          std::optional<std::uint32_t>{2400U});

    auto per_period = source;
    per_period.sample_mode = "SmpPerPeriod";
    per_period.sample_rate = 80U;
    per_period.no_asdu = 1U;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4000U);
    context.nominal_frequency_millihz.reset();
    const auto unresolved = SvPublisherProfileCompiler::compile(per_period, context);
    CHECK(unresolved.ok());
    CHECK(unresolved.profile.has_value());
    CHECK(unresolved.profile->timing.resolution ==
          SvTimingResolution::needs_nominal_frequency);
    CHECK(!unresolved.profile->timing.samples_per_second.has_value());
    CHECK(!unresolved.profile->timing.frames_per_second.has_value());
    CHECK(std::any_of(
        unresolved.warnings.begin(), unresolved.warnings.end(),
        [](const std::string& warning) {
            return warning.find("nominal-system-frequency") != std::string::npos;
        }));

    auto default_per_period = per_period;
    default_per_period.sample_mode.clear();
    const auto default_mode = SvPublisherProfileCompiler::compile(default_per_period, context);
    CHECK(default_mode.ok());
    CHECK(default_mode.profile.has_value());
    CHECK(default_mode.profile->timing.sampling_basis == SvSampleMode::samples_per_period);
    CHECK(default_mode.profile->timing.resolution ==
          SvTimingResolution::needs_nominal_frequency);

    context.nominal_frequency_millihz = 50000U;
    const auto fifty_hz = SvPublisherProfileCompiler::compile(per_period, context);
    CHECK(fifty_hz.ok());
    CHECK(fifty_hz.profile.has_value());
    CHECK(fifty_hz.profile->timing.resolved());
    CHECK(fifty_hz.profile->timing.exact_sample_rate_hz() ==
          std::optional<std::uint32_t>{4000U});
    CHECK(fifty_hz.profile->timing.exact_frame_rate_hz() ==
          std::optional<std::uint32_t>{4000U});

    context.nominal_frequency_millihz = 60000U;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4800U);
    const auto sixty_hz = SvPublisherProfileCompiler::compile(per_period, context);
    CHECK(sixty_hz.ok());
    CHECK(sixty_hz.profile.has_value());
    CHECK(sixty_hz.profile->timing.exact_sample_rate_hz() ==
          std::optional<std::uint32_t>{4800U});
    CHECK(sixty_hz.profile->timing.exact_frame_rate_hz() ==
          std::optional<std::uint32_t>{4800U});

    auto eight_asdu = per_period;
    eight_asdu.sample_rate = 256U;
    eight_asdu.no_asdu = 8U;
    context.nominal_frequency_millihz = 50000U;
    context.sample_counter_modulus = static_cast<std::uint16_t>(12800U);
    const auto legacy_measurement = SvPublisherProfileCompiler::compile(eight_asdu, context);
    CHECK(legacy_measurement.ok());
    CHECK(legacy_measurement.profile.has_value());
    CHECK(legacy_measurement.profile->timing.exact_sample_rate_hz() ==
          std::optional<std::uint32_t>{12800U});
    CHECK(legacy_measurement.profile->timing.exact_frame_rate_hz() ==
          std::optional<std::uint32_t>{1600U});

    auto seconds_per_sample = source;
    seconds_per_sample.sample_mode = "SecPerSample";
    seconds_per_sample.sample_rate = 2U;
    seconds_per_sample.no_asdu = 1U;
    context.nominal_frequency_millihz.reset();
    context.sample_counter_modulus = static_cast<std::uint16_t>(1U);
    const auto slow_stream = SvPublisherProfileCompiler::compile(seconds_per_sample, context);
    CHECK(slow_stream.ok());
    CHECK(slow_stream.profile.has_value());
    CHECK(slow_stream.profile->timing.sampling_basis == SvSampleMode::seconds_per_sample);
    CHECK(slow_stream.profile->timing.samples_per_second.has_value());
    CHECK(slow_stream.profile->timing.samples_per_second->numerator == 1U);
    CHECK(slow_stream.profile->timing.samples_per_second->denominator == 2U);
    CHECK(slow_stream.profile->timing.frames_per_second.has_value());
    CHECK(slow_stream.profile->timing.frames_per_second->numerator == 1U);
    CHECK(slow_stream.profile->timing.frames_per_second->denominator == 2U);
    CHECK(!slow_stream.profile->timing.exact_frame_rate_hz().has_value());

    auto legacy_seconds_per_sample = seconds_per_sample;
    legacy_seconds_per_sample.sample_mode = "SecPerSmp";
    const auto legacy_slow_stream =
        SvPublisherProfileCompiler::compile(legacy_seconds_per_sample, context);
    CHECK(legacy_slow_stream.ok());
    CHECK(legacy_slow_stream.profile.has_value());
    CHECK(legacy_slow_stream.profile->timing.sampling_basis ==
          SvSampleMode::seconds_per_sample);
    CHECK(legacy_slow_stream.profile->timing.samples_per_second ==
          slow_stream.profile->timing.samples_per_second);
}

void sampled_values_profile_family_and_transport_are_explicit() {
    using namespace ar::iec61850::sampled_values;
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("sv-4800-structured-4i4v.scd"));
    CHECK(document.sampled_values_streams.size() == 1U);
    const auto source = document.sampled_values_streams.front();
    CHECK(source.multicast);

    SvPublisherProfileCompileContext context;
    context.profile_family = SvProfileFamily::iec61850_9_2;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4800U);

    const auto generic = SvPublisherProfileCompiler::compile(source, context);
    CHECK(generic.ok());
    CHECK(generic.profile.has_value());
    CHECK(generic.profile->profile_family == SvProfileFamily::iec61850_9_2);
    CHECK(generic.profile->profile_family_resolution == SvProfileFamilyResolution::resolved);
    CHECK(generic.profile->transport_mode == SvTransportMode::multicast);
    CHECK(is_valid_sv_app_id(generic.profile->app_id));
    CHECK(is_iec_sv_multicast_mac(generic.profile->destination_mac));

    auto unicast = source;
    unicast.multicast = false;
    unicast.address.destination_mac =
        std::array<std::uint8_t, 6>{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
    unicast.address.destination_mac_text = "02:00:00:00:00:01";
    const auto unicast_compiled = SvPublisherProfileCompiler::compile(unicast, context);
    CHECK(unicast_compiled.ok());
    CHECK(unicast_compiled.profile.has_value());
    CHECK(unicast_compiled.profile->transport_mode == SvTransportMode::unicast);

    auto bad_multicast = source;
    bad_multicast.address.destination_mac =
        std::array<std::uint8_t, 6>{0x01U, 0x0CU, 0xCDU, 0x05U, 0x00U, 0x01U};
    CHECK(!SvPublisherProfileCompiler::compile(bad_multicast, context).ok());

    auto bad_unicast = source;
    bad_unicast.multicast = false;
    CHECK(!SvPublisherProfileCompiler::compile(bad_unicast, context).ok());

    auto bad_appid = source;
    bad_appid.address.app_id = static_cast<std::uint16_t>(0x3FFFU);
    CHECK(!SvPublisherProfileCompiler::compile(bad_appid, context).ok());

    context.profile_family = SvProfileFamily::legacy_9_2le;
    CHECK(!SvPublisherProfileCompiler::compile(source, context).ok());

    auto legacy = source;
    legacy.address.app_id = static_cast<std::uint16_t>(0x4000U);
    legacy.address.app_id_text = "4000";
    const auto legacy_result = SvPublisherProfileCompiler::compile(legacy, context);
    CHECK(legacy_result.ok());
    CHECK(legacy_result.profile.has_value());
    CHECK(legacy_result.profile->profile_family_resolution ==
          SvProfileFamilyResolution::resolved);
    CHECK(legacy_9_2le_4i4v_layout_matches(*legacy_result.profile));
    CHECK(legacy_9_2le_timing_matches(legacy_result.profile->timing));

    context.profile_family = SvProfileFamily::iec61869_9;
    const auto iec61869 = SvPublisherProfileCompiler::compile(source, context);
    CHECK(iec61869.ok());
    CHECK(iec61869.profile.has_value());
    CHECK(iec61869.profile->profile_family_resolution ==
          SvProfileFamilyResolution::incomplete);
    CHECK(std::any_of(
        iec61869.warnings.begin(), iec61869.warnings.end(),
        [](const std::string& warning) {
            return warning.find("deployment remains blocked") != std::string::npos;
        }));

    context.profile_family = SvProfileFamily::unspecified;
    const auto unspecified = SvPublisherProfileCompiler::compile(source, context);
    CHECK(unspecified.ok());
    CHECK(unspecified.profile.has_value());
    CHECK(unspecified.profile->profile_family_resolution ==
          SvProfileFamilyResolution::unresolved);
}

void parser_preserves_sampled_value_multicast_semantics() {
    using namespace ar::iec61850::sampled_values;
    using namespace ar::iec61850::scl;

    constexpr std::string_view xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0">
      <SampledValueControl name="UCAST" smvID="UCAST" smpRate="4000"
          smpMod="SmpPerSec" nofASDU="1" multicast="false"/>
      <SampledValueControl name="DEFAULT" smvID="DEFAULT" smpRate="4000"
          smpMod="SmpPerSec" nofASDU="1"/>
      <SampledValueControl name="MALFORMED" smvID="MALFORMED" smpRate="4000"
          smpMod="SmpPerSec" nofASDU="1" multicast="bogus"/>
    </LN0>
  </LDevice></Server></AccessPoint></IED>
</SCL>)xml";

    const auto document = SclParser{}.parse(xml, "sv-transport.scd");
    CHECK(document.sampled_values_streams.size() == 3U);
    CHECK(!document.sampled_values_streams[0].multicast);
    CHECK(document.sampled_values_streams[0].multicast_valid);
    CHECK(document.sampled_values_streams[1].multicast);
    CHECK(document.sampled_values_streams[1].multicast_valid);
    CHECK(document.sampled_values_streams[2].multicast);
    CHECK(!document.sampled_values_streams[2].multicast_valid);
    CHECK(std::any_of(document.warnings.begin(), document.warnings.end(), [](const std::string& warning) {
        return warning.find("multicast='bogus'") != std::string::npos &&
               warning.find("deployment/export is blocked") != std::string::npos;
    }));

    SvPublisherProfileCompileContext malformed_context;
    malformed_context.profile_family = SvProfileFamily::iec61850_9_2;
    const auto malformed = SvPublisherProfileCompiler::compile(
        document.sampled_values_streams[2], malformed_context);
    CHECK(!malformed.ok());
    CHECK(std::any_of(malformed.errors.begin(), malformed.errors.end(), [](const std::string& error) {
        return error.find("invalid explicit multicast attribute") != std::string::npos;
    }));

    auto canonical_document =
        SclParser{}.load(fixture("sv-4800-structured-4i4v.scd"));
    CHECK(canonical_document.sampled_values_streams.size() == 1U);
    canonical_document.sampled_values_streams.front().multicast = false;
    SclCanonicalExportOptions options;
    options.edition = canonical_document.edition;
    options.profile = SclExportProfile::scd;
    const auto exported = SclExporter::canonical(canonical_document, options);
    CHECK(exported.success);
    CHECK(exported.xml.find("multicast=\"false\"") != std::string::npos);
    const auto reparsed = SclParser{}.parse(exported.xml, "sv-transport-roundtrip.scd");
    CHECK(reparsed.sampled_values_streams.size() == 1U);
    CHECK(!reparsed.sampled_values_streams.front().multicast);
    CHECK(reparsed.sampled_values_streams.front().multicast_valid);

    canonical_document.sampled_values_streams.front().multicast_valid = false;
    const auto invalid_export = SclExporter::canonical(canonical_document, options);
    CHECK(!invalid_export.success);
    CHECK(invalid_export.error.find("multicast was explicitly malformed") != std::string::npos);
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

void parser_preserves_configured_control_model_value() {
    using namespace ar::iec61850::scl;

    const auto document = SclParser{}.load(fixture("minimal-station-brcb.scd"));
    const std::array expected{
        std::pair<std::string_view, std::string_view>{"SPCSO1", "direct-with-normal-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO2", "sbo-with-normal-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO3", "direct-with-enhanced-security"},
        std::pair<std::string_view, std::string_view>{"SPCSO4", "sbo-with-enhanced-security"},
    };
    for (const auto& [data_object, configured_value] : expected) {
        const auto configured = std::find_if(
            document.model_entries.begin(),
            document.model_entries.end(),
            [&](const SclDataSetEntry& entry) {
                return entry.ln_class == "GGIO" && entry.ln_inst == "1" &&
                    entry.do_name == data_object && entry.da_name == "ctlModel";
            });
        CHECK(configured != document.model_entries.end());
        CHECK(configured->functional_constraint == "CF");
        CHECK(configured->cdc == "SPC");
        CHECK(configured->basic_type == "Enum");
        CHECK(configured->configured_value == configured_value);
    }
}

void simulator_compiles_semantic_defaults_and_indexed_report_instances() {
    using namespace ar::iec61850::scl;
    using namespace ar::iec61850::simulation;

    constexpr std::string_view xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="SIM_DEFAULTS" version="1" revision="A"/>
  <IED name="IED1" manufacturer="SIEMENS" type="SIM" configVersion="CFG-7">
    <AccessPoint name="P1"><Server><LDevice inst="LD0">
      <LN0 lnClass="LLN0" lnType="LLN0Type">
        <DataSet name="ds"><FCDA ldInst="LD0" lnClass="LLN0" doName="Mod" daName="stVal" fc="ST"/></DataSet>
        <ReportControl name="UR" datSet="ds" rptID="RID" buffered="false" indexed="true" confRev="3">
          <RptEnabled max="2"/>
        </ReportControl>
      </LN0>
      <LN lnClass="GGIO" inst="1" lnType="GGIOType"/>
    </LDevice></Server></AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0">
      <DO name="Mod" type="StateType"/>
      <DO name="Beh" type="StateType"/>
      <DO name="Health" type="StateType"/>
      <DO name="NamPlt" type="NamePlateType"/>
    </LNodeType>
    <LNodeType id="GGIOType" lnClass="GGIO"><DO name="Other" type="OtherType"/></LNodeType>
    <DOType id="StateType" cdc="ENS">
      <DA name="stVal" bType="Enum" type="StateEnum" fc="ST"/>
      <DA name="q" bType="Quality" fc="ST"/>
      <DA name="t" bType="Timestamp" fc="ST"/>
    </DOType>
    <DOType id="NamePlateType" cdc="LPL">
      <DA name="vendor" bType="VisString255" fc="DC"/>
      <DA name="configRev" bType="VisString255" fc="DC"/>
    </DOType>
    <DOType id="OtherType" cdc="ENS"><DA name="stVal" bType="Enum" type="StateEnum" fc="ST"/></DOType>
    <EnumType id="StateEnum"><EnumVal ord="0">reserved</EnumVal><EnumVal ord="1">normal</EnumVal></EnumType>
  </DataTypeTemplates>
</SCL>)xml";

    const auto document = SclParser{}.parse(xml, "semantic-defaults.scd");
    CHECK(document.report_controls.size() == 1U);
    CHECK(document.report_controls.front().indexed);
    CHECK(document.report_controls.front().max_clients == 2U);

    IedSimulatorProfileFromSclOptions options;
    options.ied_name = "IED1";
    options.simulation_start_unix_ms = 1'700'000'000'123ULL;
    const auto compiled = IedSimulatorProfileBuilder::build(document, options);
    CHECK(compiled.report_control_definition_count == 1U);
    CHECK(compiled.report_control_instance_count == 2U);
    CHECK(compiled.profile.report_control_blocks.size() == 2U);
    CHECK(compiled.profile.report_control_blocks[0].mms_item == "LLN0$RP$UR01");
    CHECK(compiled.profile.report_control_blocks[1].mms_item == "LLN0$RP$UR02");
    CHECK(compiled.profile.report_control_blocks[0].reference == "IED1LD0/LLN0$RP$UR01");
    CHECK(compiled.profile.report_control_blocks[1].reference == "IED1LD0/LLN0$RP$UR02");
    CHECK(compiled.profile.report_control_blocks[0].report_id == "RID");
    CHECK(compiled.profile.report_control_blocks[1].report_id == "RID");

    const auto point = [&](const std::string_view item) -> const IedSimulatorPoint& {
        for (const auto& device : compiled.profile.logical_devices) {
            for (const auto& node : device.logical_nodes) {
                const auto found = std::find_if(
                    node.points.begin(), node.points.end(),
                    [item](const IedSimulatorPoint& candidate) {
                        return candidate.mms_item == item;
                    });
                if (found != node.points.end()) return *found;
            }
        }
        throw std::runtime_error("missing simulator point: " + std::string{item});
    };

    CHECK(point("LLN0$ST$Mod$stVal").initial_value == "1");
    CHECK(point("LLN0$ST$Beh$stVal").initial_value == "1");
    CHECK(point("LLN0$ST$Health$stVal").initial_value == "1");
    CHECK(point("LLN0$ST$Mod$q").initial_value == "good");
    CHECK(point("LLN0$ST$Mod$t").initial_value == "unix-ms:1700000000123");
    CHECK(point("LLN0$DC$NamPlt$vendor").initial_value == "SIEMENS");
    CHECK(point("LLN0$DC$NamPlt$configRev").initial_value == "CFG-7");

    // Semantic defaults are object-aware: an unrelated Enum still receives the
    // conservative zero fallback rather than a global "normal=1" mutation.
    CHECK(point("GGIO1$ST$Other$stVal").initial_value == "0");
}

void parser_preserves_setting_control_and_invalid_state() {
    using namespace ar::iec61850::scl;

    constexpr std::string_view valid_xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0"><SettingControl numOfSGs="3" actSG="2"/></LN0>
  </LDevice></Server></AccessPoint></IED>
</SCL>)xml";
    const auto valid = SclParser{}.parse(valid_xml, "setting-valid.scd");
    CHECK(valid.setting_controls.size() == 1U);
    const auto& control = valid.setting_controls.front();
    CHECK(control.ied_name == "IED1");
    CHECK(control.ld_inst == "LD0");
    CHECK(control.logical_node_path == "LLN0");
    CHECK(control.control_block_reference == "IED1LD0/LLN0$SP$SGCB");
    CHECK(control.number_of_setting_groups == std::optional<std::uint32_t>{3U});
    CHECK(control.active_setting_group == std::optional<std::uint32_t>{2U});
    CHECK(control.valid());
    CHECK(std::none_of(valid.warnings.begin(), valid.warnings.end(), [](const std::string& warning) {
        return warning.find("SettingControl") != std::string::npos;
    }));

    constexpr std::string_view invalid_xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0"><SettingControl numOfSGs="0" actSG="bogus"/></LN0>
  </LDevice></Server></AccessPoint></IED>
</SCL>)xml";
    const auto invalid = SclParser{}.parse(invalid_xml, "setting-invalid.scd");
    CHECK(invalid.setting_controls.size() == 1U);
    CHECK(!invalid.setting_controls.front().valid());
    CHECK(invalid.setting_controls.front().number_of_setting_groups ==
          std::optional<std::uint32_t>{0U});
    CHECK(!invalid.setting_controls.front().active_setting_group.has_value());
    CHECK(std::any_of(invalid.warnings.begin(), invalid.warnings.end(), [](const std::string& warning) {
        return warning.find("SettingControl 'IED1LD0/LLN0$SP$SGCB'") != std::string::npos &&
               warning.find("invalid or missing") != std::string::npos;
    }));
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
        {"SV sampling and frame cadence semantics", publisher_profile_separates_sampling_from_frame_cadence},
        {"SV profile family and transport semantics", sampled_values_profile_family_and_transport_are_explicit},
        {"SCL SV multicast semantics", parser_preserves_sampled_value_multicast_semantics},
        {"SCL dataset references", dataset_reference_resolver_accepts_canonical_and_local_forms},
        {"SCL configured control model", parser_preserves_configured_control_model_value},
        {"Simulator semantic defaults and indexed RCBs", simulator_compiles_semantic_defaults_and_indexed_report_instances},
        {"SCL SettingControl", parser_preserves_setting_control_and_invalid_state},
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
