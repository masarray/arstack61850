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

    InjectorController controller;
    if (controller.state() != InjectorControlState::idle) {
        return fail(6);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(7);
    }

    if (controller.configure({0U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::invalid_configuration) {
        return fail(8);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(9);
    }

    const auto configured = controller.snapshot();
    if (configured.state != InjectorControlState::configured ||
        configured.configuration_revision != 1U ||
        configured.armed_revision != 0U) {
        return fail(10);
    }

    if (controller.arm() != InjectorControlStatus::ok) {
        return fail(11);
    }
    const auto armed = controller.snapshot();
    if (armed.state != InjectorControlState::armed ||
        armed.armed_revision != armed.configuration_revision) {
        return fail(12);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::protection_fault}) !=
        InjectorControlStatus::invalid_state) {
        return fail(13);
    }

    if (controller.start() != InjectorControlStatus::ok || !controller.running()) {
        return fail(14);
    }
    const auto first_run = controller.snapshot();
    if (first_run.run_sequence != 1U) {
        return fail(15);
    }
    if (controller.arm() != InjectorControlStatus::invalid_state) {
        return fail(16);
    }

    if (controller.stop() != InjectorControlStatus::ok ||
        controller.state() != InjectorControlState::stopped) {
        return fail(17);
    }
    if (controller.arm() != InjectorControlStatus::ok ||
        controller.start() != InjectorControlStatus::ok) {
        return fail(18);
    }
    if (controller.snapshot().run_sequence != 2U) {
        return fail(19);
    }

    controller.set_fault();
    if (controller.state() != InjectorControlState::fault) {
        return fail(20);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(21);
    }

    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(22);
    }
    const auto recovered = controller.snapshot();
    if (recovered.state != InjectorControlState::configured ||
        recovered.configuration_revision != 2U ||
        recovered.armed_revision != 0U) {
        return fail(23);
    }

    return 0;
}
