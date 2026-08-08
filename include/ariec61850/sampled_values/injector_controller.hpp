// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ar::iec61850::sampled_values {

enum class InjectorScenarioKind : std::uint8_t {
    normal,
    protection_fault,
};

enum class InjectorControlState : std::uint8_t {
    idle,
    configured,
    armed,
    running,
    stopped,
    fault,
};

enum class InjectorControlStatus : std::uint8_t {
    ok,
    invalid_state,
    invalid_configuration,
};

struct InjectorControlConfig final {
    std::uint32_t sample_rate_hz{4'000U};
    InjectorScenarioKind scenario{InjectorScenarioKind::normal};
};

struct InjectorControlSnapshot final {
    InjectorControlState state{InjectorControlState::idle};
    InjectorControlConfig configuration{};
    std::uint32_t configuration_revision{};
    std::uint32_t armed_revision{};
    std::uint64_t run_sequence{};
};

// Transport-independent injector lifecycle state machine.
//
// JSON, USB serial, TCP, GUI, and ESP-IDF console code must stay outside this
// class. They translate commands into these operations so Windows and embedded
// targets share the same lifecycle semantics.
class InjectorController final {
public:
    [[nodiscard]] InjectorControlSnapshot snapshot() const noexcept {
        return {
            state_,
            configuration_,
            configuration_revision_,
            armed_revision_,
            run_sequence_};
    }

    [[nodiscard]] InjectorControlState state() const noexcept { return state_; }
    [[nodiscard]] bool running() const noexcept {
        return state_ == InjectorControlState::running;
    }

    [[nodiscard]] InjectorControlStatus configure(
        const InjectorControlConfig configuration) noexcept {
        if (state_ == InjectorControlState::armed ||
            state_ == InjectorControlState::running) {
            return InjectorControlStatus::invalid_state;
        }
        if (!valid(configuration)) {
            return InjectorControlStatus::invalid_configuration;
        }

        configuration_ = configuration;
        ++configuration_revision_;
        if (configuration_revision_ == 0U) {
            ++configuration_revision_;
        }
        armed_revision_ = 0U;
        state_ = InjectorControlState::configured;
        return InjectorControlStatus::ok;
    }

    [[nodiscard]] InjectorControlStatus arm() noexcept {
        if (state_ != InjectorControlState::configured &&
            state_ != InjectorControlState::stopped) {
            return InjectorControlStatus::invalid_state;
        }
        if (configuration_revision_ == 0U) {
            return InjectorControlStatus::invalid_configuration;
        }

        armed_revision_ = configuration_revision_;
        state_ = InjectorControlState::armed;
        return InjectorControlStatus::ok;
    }

    [[nodiscard]] InjectorControlStatus start() noexcept {
        if (state_ != InjectorControlState::armed ||
            armed_revision_ == 0U ||
            armed_revision_ != configuration_revision_) {
            return InjectorControlStatus::invalid_state;
        }

        ++run_sequence_;
        if (run_sequence_ == 0U) {
            ++run_sequence_;
        }
        state_ = InjectorControlState::running;
        return InjectorControlStatus::ok;
    }

    [[nodiscard]] InjectorControlStatus stop() noexcept {
        if (state_ != InjectorControlState::armed &&
            state_ != InjectorControlState::running) {
            return InjectorControlStatus::invalid_state;
        }
        state_ = InjectorControlState::stopped;
        return InjectorControlStatus::ok;
    }

    void set_fault() noexcept {
        state_ = InjectorControlState::fault;
    }

private:
    [[nodiscard]] static bool valid(
        const InjectorControlConfig configuration) noexcept {
        return configuration.sample_rate_hz > 0U &&
            configuration.sample_rate_hz <= 1'000'000U;
    }

    InjectorControlConfig configuration_{};
    std::uint32_t configuration_revision_{};
    std::uint32_t armed_revision_{};
    std::uint64_t run_sequence_{};
    InjectorControlState state_{InjectorControlState::idle};
};

} // namespace ar::iec61850::sampled_values
