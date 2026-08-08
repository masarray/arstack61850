// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/injector_controller.hpp"

#include <cstdint>

namespace {

using namespace ar::iec61850::sampled_values;

[[nodiscard]] int fail(const int code) noexcept { return code; }

} // namespace

int main() {
    InjectorController controller;
    if (controller.state() != InjectorControlState::idle) {
        return fail(1);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(2);
    }

    if (controller.configure({0U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::invalid_configuration) {
        return fail(3);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(4);
    }

    const auto configured = controller.snapshot();
    if (configured.state != InjectorControlState::configured ||
        configured.configuration_revision != 1U ||
        configured.armed_revision != 0U) {
        return fail(5);
    }

    if (controller.arm() != InjectorControlStatus::ok) {
        return fail(6);
    }
    const auto armed = controller.snapshot();
    if (armed.state != InjectorControlState::armed ||
        armed.armed_revision != armed.configuration_revision) {
        return fail(7);
    }
    if (controller.configure({4'000U, InjectorScenarioKind::protection_fault}) !=
        InjectorControlStatus::invalid_state) {
        return fail(8);
    }

    if (controller.start() != InjectorControlStatus::ok || !controller.running()) {
        return fail(9);
    }
    const auto first_run = controller.snapshot();
    if (first_run.run_sequence != 1U) {
        return fail(10);
    }
    if (controller.arm() != InjectorControlStatus::invalid_state) {
        return fail(11);
    }

    if (controller.stop() != InjectorControlStatus::ok ||
        controller.state() != InjectorControlState::stopped) {
        return fail(12);
    }
    if (controller.arm() != InjectorControlStatus::ok ||
        controller.start() != InjectorControlStatus::ok) {
        return fail(13);
    }
    if (controller.snapshot().run_sequence != 2U) {
        return fail(14);
    }

    controller.set_fault();
    if (controller.state() != InjectorControlState::fault) {
        return fail(15);
    }
    if (controller.start() != InjectorControlStatus::invalid_state) {
        return fail(16);
    }

    if (controller.configure({4'000U, InjectorScenarioKind::normal}) !=
        InjectorControlStatus::ok) {
        return fail(17);
    }
    const auto recovered = controller.snapshot();
    if (recovered.state != InjectorControlState::configured ||
        recovered.configuration_revision != 2U ||
        recovered.armed_revision != 0U) {
        return fail(18);
    }

    return 0;
}
