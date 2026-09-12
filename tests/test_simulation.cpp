// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/scl/parser.hpp"
#include "ariec61850/simulation/ied_simulator_engine.hpp"
#include "ariec61850/simulation/ied_simulator_profile.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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

const ar::iec61850::simulation::IedSimulatorPoint* find_point(
    const ar::iec61850::simulation::IedSimulatorProfile& profile,
    const std::string& mms_item) {
    for (const auto& device : profile.logical_devices) {
        for (const auto& node : device.logical_nodes) {
            const auto found = std::find_if(
                node.points.begin(), node.points.end(),
                [&mms_item](const auto& point) { return point.mms_item == mms_item; });
            if (found != node.points.end()) return &*found;
        }
    }
    return nullptr;
}

void profile_builder_preserves_full_structural_model() {
    using namespace ar::iec61850;
    const auto document = scl::SclParser{}.load(fixture("minimal-station-brcb.scd"));
    const auto built = simulation::IedSimulatorProfileBuilder::build(document);

    CHECK(built.source_ied_name == "MU01");
    CHECK(built.selected_ied_name == "MU01");
    CHECK(built.profile.name == "MU01");
    CHECK(built.profile.vendor == "AR");
    CHECK(built.profile.logical_devices.size() == 1U);
    CHECK(built.profile.logical_node_count() == 4U);
    CHECK(built.profile.point_count() >= 23U);
    CHECK(built.structural_data_attribute_count == built.profile.point_count());
    CHECK(built.profile.data_sets.size() == 2U);
    CHECK(built.profile.report_control_blocks.size() == 2U);
    CHECK(built.profile.data_set_member_count() == 5U);

    const auto* structural_only = find_point(
        built.profile, "TCTR1$MX$AmpUnmapped$instMag$i");
    CHECK(structural_only != nullptr);
    CHECK(structural_only->mms_domain == "MU01LD0");
    CHECK(structural_only->basic_type == "INT32");
    CHECK(structural_only->display_type == "Number");
    CHECK(structural_only->initial_value == "0");

    const auto* position = find_point(built.profile, "XCBR1$ST$Pos$stVal");
    CHECK(position != nullptr);
    CHECK(position->display_type == "Boolean");
    CHECK(position->initial_value == "false");

    const auto* control_model = find_point(built.profile, "GGIO1$CF$SPCSO4$ctlModel");
    CHECK(control_model != nullptr);
    CHECK(control_model->display_type == "Enumeration");
    CHECK(control_model->initial_value == "4");

    const auto urcb = std::find_if(
        built.profile.report_control_blocks.begin(),
        built.profile.report_control_blocks.end(),
        [](const auto& report) { return !report.buffered; });
    const auto brcb = std::find_if(
        built.profile.report_control_blocks.begin(),
        built.profile.report_control_blocks.end(),
        [](const auto& report) { return report.buffered; });
    CHECK(urcb != built.profile.report_control_blocks.end());
    CHECK(brcb != built.profile.report_control_blocks.end());
    CHECK(urcb->mode() == "URCB");
    CHECK(brcb->mode() == "BRCB");
    CHECK(brcb->mms_item == "LLN0$BR$BRCB01");
}

void profile_builder_remaps_runtime_identity_and_filters_qt() {
    using namespace ar::iec61850;
    const auto document = scl::SclParser{}.load(fixture("minimal-station-brcb.scd"));
    simulation::IedSimulatorProfileFromSclOptions options;
    options.ied_name = "MU01";
    options.runtime_ied_name = "SIM01";
    options.include_quality_and_timestamp_points = false;
    const auto built = simulation::IedSimulatorProfileBuilder::build(document, options);

    CHECK(built.source_ied_name == "MU01");
    CHECK(built.selected_ied_name == "SIM01");
    CHECK(built.profile.name == "SIM01");
    CHECK(!built.profile.logical_devices.empty());
    CHECK(built.profile.logical_devices.front().name == "SIM01LD0");
    CHECK(built.skipped_member_count == 3U);
    CHECK(built.profile.data_set_member_count() == 2U);
    CHECK(find_point(built.profile, "XCBR1$ST$Pos$q") == nullptr);
    CHECK(find_point(built.profile, "XCBR1$ST$Pos$t") == nullptr);
    CHECK(!built.profile.report_control_blocks.empty());
    CHECK(built.profile.report_control_blocks.front().mms_domain == "SIM01LD0");
}

void engine_supports_case_insensitive_manual_state_and_deterministic_steps() {
    using namespace ar::iec61850::simulation;
    auto profile = IedSimulatorProfile::create_default_feeder_profile();
    IedSimulatorEngine engine{std::move(profile)};

    const auto* initial = engine.find_point_state("ied1ld0/xcbr1.pos.stval");
    CHECK(initial != nullptr);
    CHECK(initial->value == "closed");
    CHECK(initial->timestamp_unix_milliseconds == 0U);

    const auto mutation = engine.set_point_value(
        "IED1LD0/XCBR1$ST$Pos$stVal",
        "open",
        "questionable",
        "Manual",
        1700000000123ULL);
    CHECK(mutation.has_value());
    CHECK(mutation->previous_value == "closed");
    CHECK(mutation->new_value == "open");
    CHECK(mutation->reason == "data-change");

    const auto* updated = engine.find_point_state("XCBR1.Pos.stVal");
    CHECK(updated != nullptr);
    CHECK(updated->value == "open");
    CHECK(updated->quality == "questionable");
    CHECK(updated->origin == "Manual");
    CHECK(updated->timestamp_unix_milliseconds == 1700000000123ULL);

    engine.reset(1000U);
    engine.start();
    CHECK(engine.running());
    const auto first_events = engine.step(1100U);
    CHECK(!first_events.empty());
    const auto* phase_a = engine.find_point_state("IED1LD0/MMXU1.PhV.phsA.cVal.mag.f");
    CHECK(phase_a != nullptr);
    CHECK(phase_a->value != "230000");
    CHECK(phase_a->timestamp_unix_milliseconds == 1100U);
    CHECK(engine.step_index() == 1U);

    const auto snapshot = engine.snapshot(1200U);
    CHECK(snapshot.profile_name == "AR Demo Feeder IED");
    CHECK(snapshot.logical_device_count == 1U);
    CHECK(snapshot.logical_node_count == 2U);
    CHECK(snapshot.point_count == 4U);
    CHECK(snapshot.points.size() == 4U);

    engine.stop();
    CHECK(!engine.running());
}

} // namespace

int main() {
    try {
        profile_builder_preserves_full_structural_model();
        std::cout << "[PASS] simulator full structural profile\n";
        profile_builder_remaps_runtime_identity_and_filters_qt();
        std::cout << "[PASS] simulator runtime identity/filtering\n";
        engine_supports_case_insensitive_manual_state_and_deterministic_steps();
        std::cout << "[PASS] simulator deterministic engine\n";
        std::cout << "Passed 3/3 simulator runtime tests.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
