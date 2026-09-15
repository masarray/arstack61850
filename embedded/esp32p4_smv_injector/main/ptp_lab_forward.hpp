// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal forward declaration injected only into ptp_lab_task.cpp.
// The bounded SOURCE start path may request teardown before the later helper
// definition is encountered by the compiler.
namespace ar::esp32p4::smv {
namespace {
void stop_ptp_lab() noexcept;
} // namespace
} // namespace ar::esp32p4::smv
