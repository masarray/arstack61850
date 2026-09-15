// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_objects.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kTrue{0x83U, 0x01U, 0xFFU};

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

[[nodiscard]] std::uint64_t read_now(const void* context) noexcept {
    return context == nullptr ? 0U : *static_cast<const std::uint64_t*>(context);
}

[[nodiscard]] const mms::MmsStaticObjectEntry* find_item(
    const mms::MmsStaticObjectTable& table,
    const std::string_view item) noexcept {
    for (const auto& object : table.objects()) {
        if (object.domain == "LD0" && object.item == item) return &object;
    }
    return nullptr;
}

[[nodiscard]] mms::MmsStaticRequestAccessContext access(
    const std::uint64_t association,
    const std::span<const std::uint8_t> owner) noexcept {
    return {association, owner};
}

} // namespace

int main() {
    bool source = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> base_objects{
        mms::MmsStaticObjectEntry{
            "LD0", "X1", kBooleanType, read_boolean, &source, false}};
    const mms::MmsStaticObjectTable base_table{base_objects};
    if (!base_table.valid()) return 1;

    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid_against(base_table)) return 2;

    const mms::MmsStaticBrcbDefinition definition{
        "LD0",
        "B1",
        "BRCB-B1",
        "LD0",
        "LLN0$Events",
        1U,
        {0x5DU, 0x80U},
        0U,
        0x7CU,
        0U};

    std::array<std::uint8_t, 1'024U> slot_bytes{};
    std::array<mms::MmsStaticBrcbSlot, 1U> slots{
        mms::MmsStaticBrcbSlot{slot_bytes}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, base_table, data_set_table};
    if (!reports.initialize()) return 3;
    mms::MmsStaticBrcbControl control{reports};

    std::array<mms::MmsStaticObjectEntry, 16U> object_storage{};
    std::array<mms::MmsStaticBrcbObjectContext, 15U> context_storage{};
    std::array<char, 320U> name_storage{};
    std::uint64_t now = 100U;
    mms::MmsStaticBrcbObjectBank bank{
        definition,
        reports,
        control,
        base_objects,
        object_storage,
        context_storage,
        name_storage,
        read_now,
        &now};
    if (!bank.initialize() || bank.object_count() != 16U || !bank.table().valid()) return 4;

    const auto* rpt_ena = find_item(bank.table(), "B1$RptEna");
    const auto* gi = find_item(bank.table(), "B1$GI");
    const auto* time_of_entry = find_item(bank.table(), "B1$TimeofEntry");
    if (rpt_ena == nullptr || gi == nullptr || time_of_entry == nullptr ||
        rpt_ena->contextual_write == nullptr || gi->contextual_write == nullptr) {
        return 5;
    }

    const std::array<std::uint8_t, 2U> owner_a{0xAAU, 0x01U};
    const std::array<std::uint8_t, 2U> owner_b{0xBBU, 0x01U};
    const auto a = access(101U, owner_a);
    const auto b = access(202U, owner_b);

    const auto enable = rpt_ena->contextual_write(rpt_ena->write_context, kTrue, a);
    if (!enable.success || !reports.enabled()) return 6;

    const auto denied_gi = gi->contextual_write(gi->write_context, kTrue, b);
    if (denied_gi.success || denied_gi.failure_code != 3U) return 7;

    const auto accepted_gi = gi->contextual_write(gi->write_context, kTrue, a);
    if (!accepted_gi.success || !reports.general_interrogation_pending()) return 8;

    mms::MmsStaticBrcbCapturePlan plan;
    if (!reports.next_due(now, plan) ||
        plan.reason != mms::MmsStaticBrcbCaptureReason::general_interrogation) {
        return 9;
    }

    constexpr std::array<std::uint8_t, 6U> report_time{1U, 2U, 3U, 4U, 5U, 6U};
    std::array<std::uint8_t, 1'024U> encode_buffer{};
    std::array<std::uint8_t, 1'024U> workspace{};
    const auto captured = reports.capture(plan, report_time, encode_buffer, workspace);
    if (!captured.success() || reports.general_interrogation_pending() ||
        reports.latest_time_of_entry() != report_time) {
        return 10;
    }

    std::array<std::uint8_t, 16U> time_data{};
    const auto time_read = time_of_entry->read(time_of_entry->context, time_data);
    if (!time_read.success() || time_read.bytes_written != 8U ||
        time_data[0] != 0x8CU || time_data[1] != 0x06U ||
        !std::equal(report_time.begin(), report_time.end(), time_data.begin() + 2)) {
        return 11;
    }

    return 0;
}
