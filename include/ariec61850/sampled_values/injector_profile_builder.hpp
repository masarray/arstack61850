// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/injector_runtime_program.hpp"

#include <cstdint>

namespace ar::iec61850::sampled_values {

class InjectorProfileBuilder final {
public:
    using ChannelProfiles = InjectorRuntimeProgram::ChannelProfiles;

    void begin(
        const ChannelProfiles& base,
        const std::uint32_t source_revision,
        const std::uint32_t ramp_duration_samples = 0U) noexcept {
        target_ = base;
        source_revision_ = source_revision;
        ramp_duration_samples_ = ramp_duration_samples;
        active_ = ramp_duration_samples == 0U || ramp_duration_samples >= 2U;
    }

    void abort() noexcept {
        active_ = false;
        source_revision_ = 0U;
        ramp_duration_samples_ = 0U;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool ramp() const noexcept {
        return active_ && ramp_duration_samples_ >= 2U;
    }
    [[nodiscard]] std::uint32_t source_revision() const noexcept {
        return source_revision_;
    }
    [[nodiscard]] std::uint32_t ramp_duration_samples() const noexcept {
        return ramp_duration_samples_;
    }
    [[nodiscard]] const ChannelProfiles& target() const noexcept { return target_; }

    [[nodiscard]] bool edit_channel(const InjectorChannelEdit& edit) noexcept {
        return active_ && InjectorRuntimeProgram::apply_edit(target_, edit);
    }

private:
    ChannelProfiles target_{};
    std::uint32_t source_revision_{};
    std::uint32_t ramp_duration_samples_{};
    bool active_{};
};

} // namespace ar::iec61850::sampled_values
