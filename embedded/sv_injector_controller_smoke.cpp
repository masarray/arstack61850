// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/injector_control_protocol.hpp"
#include "ariec61850/sampled_values/injector_controller.hpp"

#include <cstdint>

namespace {

using namespace ar::iec61850::sampled_values;

[[nodiscard]] int fail(const int code) noexcept { return code; }

} // namespace

int main() {
    const auto status_command = parse_injector_control_command(
        R"({"command":"status"})");
    if (!status_command.success() ||
        status_command.command.kind != InjectorControlCommandKind::status) {
        return fail(1);
    }

    const auto configure_command = parse_injector_control_command(
        R"( { "command" : "configure", "scenario" : "protection-fault" } )");
    if (!configure_command.success() ||
        configure_command.command.kind != InjectorControlCommandKind::configure ||
        configure_command.command.scenario != InjectorScenarioKind::protection_fault) {
        return fail(2);
    }

    if (parse_injector_control_command(R"({"command":"configure"})").status !=
        InjectorControlParseStatus::missing_scenario) {
        return fail(3);
    }
    if (parse_injector_control_command(R"({"command":"launch"})").status !=
        InjectorControlParseStatus::unsupported_command) {
        return fail(4);
    }
    if (parse_injector_control_command("status").status !=
        InjectorControlParseStatus::malformed) {
        return fail(5);
    }

    const auto live_edit = parse_injector_control_command(
        R"({"command":"set-channel","channel":"Ia","rms":2500,"phaseMilliDeg":15000,"frequencyMilliHz":50000})");
    if (!live_edit.success() ||
        live_edit.command.kind != InjectorControlCommandKind::set_channel ||
        live_edit.command.channel_edit.channel_index != 0U ||
        live_edit.command.channel_edit.rms_counts != 2'500 ||
        live_edit.command.channel_edit.phase_millidegrees != 15'000 ||
        live_edit.command.channel_edit.frequency_millihz != 50'000U ||
        (live_edit.command.channel_edit.fields & injector_field_rms) == 0U ||
        (live_edit.command.channel_edit.fields & injector_field_phase) == 0U ||
        (live_edit.command.channel_edit.fields & injector_field_frequency) == 0U) {
        return fail(6);
    }

    const auto ramp_edit = parse_injector_control_command(
        R"({"command":"ramp-channel","channel":"Va","rms":9000,"durationSamples":4000})");
    if (!ramp_edit.success() ||
        ramp_edit.command.kind != InjectorControlCommandKind::ramp_channel ||
        ramp_edit.command.channel_edit.channel_index != 4U ||
        ramp_edit.command.channel_edit.duration_samples != 4'000U ||
        ramp_edit.command.channel_edit.rms_counts != 9'000) {
        return fail(7);
    }

    if (parse_injector_control_command(
            R"({"command":"set-channel","channel":"Ix","rms":1000})")
            .status != InjectorControlParseStatus::unsupported_channel) {
        return fail(8);
    }
    if (parse_injector_control_command(
            R"({"command":"set-channel","channel":"Ia"})")
            .status != InjectorControlParseStatus::missing_value) {
        return fail(9);
    }
    if (parse_injector_control_command(
            R"({"command":"ramp-channel","channel":"Ia","rms":2000,"durationSamples":1})")
            .status != InjectorControlParseStatus::invalid_value) {
        return fail(10);
    }

    InjectorController controller;
    if (controller.state() != InjectorControlState::idle) {
        return fail(11);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(12);
    }

    if (controller.configure({0U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::invalid_configuration) {
        return fail(13);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(14);
    }

    const auto configured = controller.snapshot();
    if (configured.state != InjectorControlState::configured ||
        configured.configuration_revision != 1U ||
        configured.armed_revision != 0U) {
        return fail(15);
    }

    if (controller.arm() != InjectorControlStatus::ok) {
        return fail(16);
    }
    const auto armed = controller.snapshot();
    if (armed.state != InjectorControlState::armed ||
        armed.armed_revision != armed.configuration_revision) {
        return fail(17);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::protection_fault}) !=
        InjectorControlStatus::invalid_state) {
        return fail(18);
    }

    if (controller.start() != InjectorControlStatus::ok || !controller.running()) {
        return fail(19);
    }
    const auto first_run = controller.snapshot();
    if (first_run.run_sequence != 1U) {
        return fail(20);
    }
    if (controller.arm() != InjectorControlStatus::invalid_state) {
        return fail(21);
    }

    if (controller.stop() != InjectorControlStatus::ok ||
        controller.state() != InjectorControlState::stopped) {
        return fail(22);
    }
    if (controller.arm() != InjectorControlStatus::ok ||
        controller.start() != InjectorControlStatus::ok) {
        return fail(23);
    }
    if (controller.snapshot().run_sequence != 2U) {
        return fail(24);
    }

    controller.set_fault();
    if (controller.state() != InjectorControlState::fault) {
        return fail(25);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(26);
    }

    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(27);
    }
    const auto recovered = controller.snapshot();
    if (recovered.state != InjectorControlState::configured ||
        recovered.configuration_revision != 2U ||
        recovered.armed_revision != 0U) {
        return fail(28);
    }

    return 0;
}
