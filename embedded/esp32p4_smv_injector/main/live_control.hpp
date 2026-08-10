// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/live_signal_state.hpp"

#include <cstdint>

namespace ar::esp32p4::smv {

void live_control_initialize(
    std::int32_t current_rms_counts,
    std::int32_t voltage_rms_counts) noexcept;

[[nodiscard]] ar::iec61850::sampled_values::SvLiveSignalState
live_signal_snapshot() noexcept;

[[nodiscard]] bool live_tx_running() noexcept;
[[nodiscard]] bool take_start_request() noexcept;

void live_control_task(void*) noexcept;

} // namespace ar::esp32p4::smv
