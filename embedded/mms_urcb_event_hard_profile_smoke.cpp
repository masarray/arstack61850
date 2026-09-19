// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_urcb_runtime.hpp"

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

} // namespace

int main() {
    using namespace ar::iec61850;

    static_assert(mms::MmsStaticUrcbRuntime::maximum_control_blocks >= 34U);

    bool value_a = false;
    bool value_b = true;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{{
        {"LD0", "X1", kBooleanType, read_boolean, &value_a, false},
        {"LD0", "X2", kBooleanType, read_boolean, &value_b, false},
    }};
    const mms::MmsStaticObjectTable object_table{objects};
    if (!object_table.valid()) return 1;

    const std::array<mms::MmsStaticDataSetMember, 2U> members{{
        {"LD0", "X1"},
        {"LD0", "X2"},
    }};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{{
        {"LD0", "LLN0$Events", members, false},
    }};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid_against(object_table)) return 2;

    const std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{{
        {"LD0",
         "LLN0$RP$U1",
         "URCB-U1",
         "LD0",
         "LLN0$Events",
         1U,
         {0x5CU, 0x80U},
         25U,
         0x70U,
         0U},
    }};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    mms::MmsStaticUrcbRuntime runtime{
        definitions, states, object_table, data_set_table};
    if (!runtime.initialize()) return 3;
    constexpr std::array<std::uint8_t, 2U> iedscout_generic_optflds{0x7BU, 0x80U};
    constexpr std::array<std::uint8_t, 2U> effective_urcb_optflds{0x78U, 0x80U};
    constexpr std::array<std::uint8_t, 2U> unsupported_segmentation{0x78U, 0xC0U};
    constexpr std::array<std::uint8_t, 2U> original_optflds{0x5CU, 0x80U};
    if (runtime.set_optional_fields(0U, iedscout_generic_optflds) !=
            mms::MmsStaticUrcbStatus::ok) return 15;
    const auto* compatibility_state = runtime.state(0U);
    if (compatibility_state == nullptr ||
        compatibility_state->optional_fields != effective_urcb_optflds) return 16;
    if (runtime.set_optional_fields(0U, unsupported_segmentation) !=
            mms::MmsStaticUrcbStatus::invalid_value) return 17;
    if (runtime.set_optional_fields(0U, original_optflds) !=
            mms::MmsStaticUrcbStatus::ok) return 18;
    if (runtime.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok) return 4;

    // First update opens one bounded BufTm window. Later updates must coalesce
    // into that same window instead of pushing the deadline forward.
    if (runtime.notify(
            0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 100U) !=
            mms::MmsStaticUrcbStatus::ok ||
        runtime.notify(
            0U, 0U, mms::MmsStaticUrcbEventReason::quality_change, 110U) !=
            mms::MmsStaticUrcbStatus::ok ||
        runtime.notify(
            0U, 1U, mms::MmsStaticUrcbEventReason::data_change, 110U) !=
            mms::MmsStaticUrcbStatus::ok) {
        return 5;
    }

    const auto* pending = runtime.state(0U);
    if (pending == nullptr || !pending->event_pending ||
        pending->event_due_ms != 125U || pending->pending_member_count != 2U ||
        pending->member_reason_masks[0] != 0x60U ||
        pending->member_reason_masks[1] != 0x40U) {
        return 6;
    }

    mms::MmsStaticUrcbEmissionPlan plan;
    if (runtime.next_due(124U, plan)) return 7;
    if (!runtime.next_due(125U, plan) ||
        plan.index != 0U ||
        plan.reason != mms::MmsStaticUrcbReportReason::data_change ||
        plan.sequence_number != 1U) {
        return 8;
    }

    constexpr std::array<std::uint8_t, 6U> report_time{1U, 2U, 3U, 4U, 5U, 6U};
    std::array<std::uint8_t, 4'096U> destination{};
    std::array<std::uint8_t, 4'096U> workspace{};
    const auto encoded = runtime.encode(plan, report_time, destination, workspace);
    if (!encoded.success() || encoded.bytes_written == 0U || encoded.member_count != 2U) {
        return 9;
    }
    if (runtime.commit(plan, 125U) != mms::MmsStaticUrcbStatus::ok) return 10;

    const auto* committed = runtime.state(0U);
    if (committed == nullptr || committed->event_pending ||
        committed->pending_member_count != 0U ||
        committed->sequence_number != 1U ||
        committed->event_due_ms != 0U) {
        return 11;
    }
    if (runtime.next_due(125U, plan)) return 12;

    // Disabled reports must reject event scheduling and re-enable from a clean
    // pending-event state (no stale event resurrection across RptEna cycles).
    if (runtime.set_enabled(0U, false, 130U) != mms::MmsStaticUrcbStatus::ok ||
        runtime.notify(
            0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 131U) !=
            mms::MmsStaticUrcbStatus::temporarily_unavailable ||
        runtime.set_enabled(0U, true, 140U) != mms::MmsStaticUrcbStatus::ok) {
        return 13;
    }
    const auto* reenabled = runtime.state(0U);
    if (reenabled == nullptr || reenabled->event_pending ||
        reenabled->pending_member_count != 0U || reenabled->sequence_number != 0U) {
        return 14;
    }

    return 0;
}
