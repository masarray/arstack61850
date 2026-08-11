// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace ar::esp32p4::smv {

// Handles the machine-facing PROFILE subcommands used by the local GUI.
// The operator-facing surface remains the GUI; this text transport is a
// bounded development control protocol, not a user workflow.
void handle_profile_command(char* arguments) noexcept;

} // namespace ar::esp32p4::smv
