// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ariec61850/sampled_values/compiled_device_profile.hpp"
#include "ariec61850/sampled_values/live_signal_state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

// The console/control task is the ONLY writer to the committed roster.
// A TX START serially follows SELECT, and COMMIT is forbidden while live SV
// runs. The publisher only reads this immutable table on START; no hot-path
// XML parsing or shared mutable signal packet buffers.
namespace ar::esp32p4::smv {
void handle_multi_profile_command(char* arguments) noexcept;
void multi_profile_abort_staging() noexcept;
void handle_svstream_command(char* arguments) noexcept;
[[nodiscard]] ar::iec61850::sampled_values::SvLiveSignalState
multi_profile_signal_snapshot(std::size_t slot) noexcept;
// One acquire snapshot at the start of each 250us batch. Bits 0/1/2 gate
// canonical MU1/MU2/MU3 Ethernet submissions without resetting the grid.
[[nodiscard]] std::uint8_t multi_profile_enabled_mask() noexcept;

// Control-task readback: the selected immutable roster is the sole profile authority.
[[nodiscard]] std::uint64_t multi_profile_roster_hash_if_selected() noexcept;
[[nodiscard]] std::uint64_t multi_profile_roster_generation_if_selected() noexcept;
// Requires three current bank generations + canonical hashes and all three TX bits.
// No new publisher owner, no heap, no wire-time work.
[[nodiscard]] bool multi_profile_group_start_matches(
    std::uint64_t roster_hash,
    const std::array<std::uint64_t, 3>& generations,
    const std::array<std::uint64_t, 3>& hashes) noexcept;

// Null means legacy V1 START. Non-null is the selected, validated, committed
// 3-profile V2 roster. Never retain across STOP and a future control commit.
// Capacity is guaranteed by M4ThreeSvBinaryCodec (exactly three records).
[[nodiscard]] const ar::iec61850::sampled_values::CompiledSvDeviceProfile*
multi_profile_tx_roster_if_selected() noexcept;
}
