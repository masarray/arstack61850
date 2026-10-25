// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ar::esp32p4::smv {
void handle_multi_profile_command(char* arguments) noexcept;
void multi_profile_abort_staging() noexcept;
}
