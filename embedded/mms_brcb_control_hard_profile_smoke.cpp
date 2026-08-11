// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_control.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] mms::MmsStaticBrcbClientIdentity client(
    const std::uint64_t association,
    const std::uint8_t first,
    const std::uint8_t second) noexcept {
    mms::MmsStaticBrcbClientIdentity identity;
    identity.association_id = association;
    identity.owner[0] = first;
    identity.owner[1] = second;
    identity.owner_size = 2U;
    return identity;
}

} // namespace

int main() {
    bool value = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{"LD0", "X1", kBooleanType, read_boolean, &value, false}};
    const mms::MmsStaticObjectTable object_table{objects};

    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};

    const mms::MmsStaticBrcbDefinition definition{
        "LD0",
        "LLN0$BR$Events",
        "LD0/LLN0$BR$Events",
        "LD0",
        "LLN0$Events",
        1U,
        {0x7FU, 0x80U},
        20U,
        0x70U};

    std::array<std::uint8_t, 512U> slot0{};
    std::array<std::uint8_t, 512U> slot1{};
    std::array<mms::MmsStaticBrcbSlot, 2U> slots{
        mms::MmsStaticBrcbSlot{slot0},
        mms::MmsStaticBrcbSlot{slot1}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, object_table, data_set_table};
    if (!reports.initialize()) {
        return 1;
    }

    mms::MmsStaticBrcbControl control{reports};
    const auto a1 = client(101U, 0xAAU, 0x01U);
    const auto a2 = client(102U, 0xAAU, 0x01U);
    const auto a3 = client(103U, 0xAAU, 0x01U);
    const auto b1 = client(201U, 0xBBU, 0x01U);
    const std::array<std::uint8_t, 8U> missing_entry{0U,0U,0U,0U,0U,0U,0U,1U};

    if (control.reserve(a1, 5U, 100U) != mms::MmsStaticBrcbControlStatus::ok) {
        return 2;
    }
    auto state = control.state(100U);
    if (!state.reserved || !state.owner_connected || state.report_enabled ||
        state.association_id != 101U || state.resv_tms_seconds != 5U ||
        state.owner.size() != 2U || state.owner[0] != 0xAAU) {
        return 3;
    }

    // A second live association, even with the same stable Owner identity,
    // cannot steal a currently connected reservation. A different Owner also
    // cannot reserve or enable the BRCB.
    if (control.reserve(a2, 5U, 200U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied ||
        control.reserve(b1, 5U, 200U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied ||
        control.set_report_enabled(b1, true, 200U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied) {
        return 4;
    }

    if (control.set_report_enabled(a1, true, 300U) !=
            mms::MmsStaticBrcbControlStatus::ok ||
        !reports.enabled() ||
        control.replay_from(a1, missing_entry, 300U) !=
            mms::MmsStaticBrcbControlStatus::temporarily_unavailable) {
        return 5;
    }

    // Association loss always disables reporting. Non-zero ResvTms retains the
    // stable Owner identity so the same Owner can reclaim before timeout.
    control.on_association_closed(a1.association_id, 1'000U);
    state = control.state(1'000U);
    if (reports.enabled() || !state.reserved || state.owner_connected ||
        state.association_id != 0U || state.reservation_expires_at_ms != 6'000U ||
        control.reserve(b1, 5U, 2'000U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied ||
        control.reserve(a2, 7U, 2'500U) != mms::MmsStaticBrcbControlStatus::ok) {
        return 6;
    }

    // The old association token is dead; only the reconnecting live association
    // may control replay/purge/release.
    if (control.set_report_enabled(a1, true, 2'600U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied ||
        control.replay_from(a2, missing_entry, 2'600U) !=
            mms::MmsStaticBrcbControlStatus::entry_not_found ||
        control.purge_buffer(a2, 2'600U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.release(a2, 2'600U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.state(2'600U).reserved) {
        return 7;
    }

    // ResvTms==0 releases immediately on association loss.
    if (control.reserve(a3, 0U, 3'000U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.set_report_enabled(a3, true, 3'000U) != mms::MmsStaticBrcbControlStatus::ok) {
        return 8;
    }
    control.on_association_closed(a3.association_id, 3'100U);
    if (reports.enabled() || control.state(3'100U).reserved) {
        return 9;
    }

    // A retained reservation blocks another Owner until the exact expiry, then
    // automatically becomes available without dynamic allocation or threads.
    if (control.reserve(a1, 2U, 10'000U) != mms::MmsStaticBrcbControlStatus::ok) {
        return 10;
    }
    control.on_association_closed(a1.association_id, 10'000U);
    if (control.reserve(b1, 1U, 11'999U) !=
            mms::MmsStaticBrcbControlStatus::object_access_denied) {
        return 11;
    }
    control.tick(12'000U);
    if (control.state(12'000U).reserved ||
        control.reserve(b1, 1U, 12'000U) != mms::MmsStaticBrcbControlStatus::ok) {
        return 12;
    }

    return 0;
}
