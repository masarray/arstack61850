// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_brcb_state_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0x00U, 0x12U, 0x34U, 0x00U, 0x01U};

bool g_value = true;

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

const std::array<mms::MmsStaticObjectEntry, 1U> kObjects{
    mms::MmsStaticObjectEntry{
        "LD0", "X1", kBooleanType, read_boolean, &g_value, false}};
const mms::MmsStaticObjectTable kObjectTable{kObjects};
const std::array<mms::MmsStaticDataSetMember, 1U> kMembers{
    mms::MmsStaticDataSetMember{"LD0", "X1"}};
const std::array<mms::MmsStaticDataSetEntry, 1U> kDataSets{
    mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", kMembers, false}};
const mms::MmsStaticDataSetTable kDataSetTable{kDataSets};

const mms::MmsStaticBrcbDefinition kDefinition{
    "LD0",
    "LLN0$BR$Events",
    "LD0/LLN0$BR$Events",
    "LD0",
    "LLN0$Events",
    23U,
    {0x7FU, 0x80U},
    0U,
    0x70U};

struct RuntimeFixture final {
    std::array<std::uint8_t, 1024U> slot0{};
    std::array<std::uint8_t, 1024U> slot1{};
    std::array<std::uint8_t, 1024U> slot2{};
    std::array<mms::MmsStaticBrcbSlot, 3U> slots{
        mms::MmsStaticBrcbSlot{slot0},
        mms::MmsStaticBrcbSlot{slot1},
        mms::MmsStaticBrcbSlot{slot2}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime runtime{
        kDefinition, pending, slots, kObjectTable, kDataSetTable};
};

[[nodiscard]] bool capture_one(
    mms::MmsStaticBrcbRuntime& runtime,
    const std::uint64_t now_ms,
    const std::span<std::uint8_t> staging,
    const std::span<std::uint8_t> workspace) noexcept {
    if (runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_change, now_ms) !=
            mms::MmsStaticBrcbStatus::ok) {
        return false;
    }
    mms::MmsStaticBrcbCapturePlan plan;
    if (!runtime.next_due(now_ms, plan)) {
        return false;
    }
    return runtime.capture(plan, kReportTime, staging, workspace).success();
}

[[nodiscard]] bool entry_is(
    const mms::MmsStaticBrcbEntryView& entry,
    const std::uint8_t low_byte) noexcept {
    return entry.entry_id.size() == 8U && entry.entry_id[7U] == low_byte;
}

[[nodiscard]] bool build_legacy_v1_from_v2(
    const std::span<const std::uint8_t> v2,
    const std::span<std::uint8_t> legacy,
    std::size_t& legacy_bytes) noexcept {
    constexpr std::size_t v1_header = 48U;
    constexpr std::size_t v2_header = 56U;
    if (v2.size() < v2_header || legacy.size() < v2.size() - 8U ||
        v2[0] != 'A' || v2[1] != 'R' || v2[7] != '2') {
        legacy_bytes = 0U;
        return false;
    }

    legacy_bytes = v2.size() - (v2_header - v1_header);
    std::fill(legacy.begin(), legacy.begin() + static_cast<std::ptrdiff_t>(legacy_bytes),
              std::uint8_t{0U});
    std::copy_n(v2.begin(), 41U, legacy.begin());
    legacy[7U] = static_cast<std::uint8_t>('1');
    legacy[8U] = 0U;
    legacy[9U] = 1U;
    legacy[10U] = 0U;
    legacy[11U] = static_cast<std::uint8_t>(v1_header);
    std::copy(
        v2.begin() + static_cast<std::ptrdiff_t>(v2_header),
        v2.end(),
        legacy.begin() + static_cast<std::ptrdiff_t>(v1_header));
    return true;
}

} // namespace

int main() {
    std::array<std::uint8_t, 4096U> state{};
    std::array<std::uint8_t, 4096U> legacy{};
    std::array<std::uint8_t, 2048U> staging{};
    std::array<std::uint8_t, 256U> workspace{};

    // v2 must preserve delivered retained history separately from the delivery
    // cursor. Normal post-reboot delivery resumes at E2, while replay can still
    // seek back to the already-delivered E1.
    RuntimeFixture source;
    if (!source.runtime.initialize() ||
        source.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok) {
        return 1;
    }
    for (std::uint64_t now = 100U; now <= 300U; now += 100U) {
        g_value = !g_value;
        if (!capture_one(source.runtime, now, staging, workspace)) {
            return 2;
        }
    }
    mms::MmsStaticBrcbEntryView front;
    std::array<std::uint8_t, 8U> entry1{};
    if (source.runtime.retained_size() != 3U || source.runtime.queue_size() != 3U ||
        !source.runtime.front(front) || !entry_is(front, 1U)) {
        return 3;
    }
    std::copy(front.entry_id.begin(), front.entry_id.end(), entry1.begin());
    if (source.runtime.commit_delivery(front.entry_id) != mms::MmsStaticBrcbStatus::ok ||
        source.runtime.retained_size() != 3U || source.runtime.queue_size() != 2U ||
        !source.runtime.front(front) || !entry_is(front, 2U)) {
        return 4;
    }

    const auto encoded = mms::MmsStaticBrcbStateCodec::encode(source.runtime, state);
    if (!encoded.success() || encoded.bytes_written == 0U ||
        state[7U] != static_cast<std::uint8_t>('2') ||
        state[9U] != mms::MmsStaticBrcbStateCodec::format_version) {
        return 5;
    }

    RuntimeFixture recovered;
    if (!recovered.runtime.initialize()) {
        return 6;
    }
    const auto restored = mms::MmsStaticBrcbStateCodec::restore(
        recovered.runtime,
        std::span<const std::uint8_t>{state}.first(encoded.bytes_written));
    if (!restored.success() || recovered.runtime.enabled() ||
        recovered.runtime.retained_size() != 3U || recovered.runtime.queue_size() != 2U ||
        recovered.runtime.replay_gap() || !recovered.runtime.front(front) ||
        !entry_is(front, 2U)) {
        return 7;
    }
    if (recovered.runtime.rewind_to_oldest() != mms::MmsStaticBrcbStatus::ok ||
        recovered.runtime.queue_size() != 3U || !recovered.runtime.front(front) ||
        !entry_is(front, 1U) ||
        recovered.runtime.resume_after(entry1) != mms::MmsStaticBrcbStatus::ok ||
        recovered.runtime.queue_size() != 2U || !recovered.runtime.front(front) ||
        !entry_is(front, 2U)) {
        return 8;
    }

    // Backward restore: a v1 image has no delivered-history cursor, so it is
    // intentionally interpreted as an all-undelivered queue.
    RuntimeFixture legacy_source;
    if (!legacy_source.runtime.initialize() ||
        legacy_source.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok ||
        !capture_one(legacy_source.runtime, 100U, staging, workspace) ||
        !capture_one(legacy_source.runtime, 200U, staging, workspace)) {
        return 9;
    }
    const auto legacy_v2 = mms::MmsStaticBrcbStateCodec::encode(
        legacy_source.runtime, state);
    std::size_t legacy_bytes{};
    if (!legacy_v2.success() ||
        !build_legacy_v1_from_v2(
            std::span<const std::uint8_t>{state}.first(legacy_v2.bytes_written),
            legacy,
            legacy_bytes)) {
        return 10;
    }
    RuntimeFixture from_v1;
    if (!from_v1.runtime.initialize() ||
        !mms::MmsStaticBrcbStateCodec::restore(
            from_v1.runtime,
            std::span<const std::uint8_t>{legacy}.first(legacy_bytes)).success() ||
        from_v1.runtime.retained_size() != 2U || from_v1.runtime.queue_size() != 2U ||
        from_v1.runtime.replay_gap() || !from_v1.runtime.front(front) ||
        !entry_is(front, 1U)) {
        return 11;
    }

    // Overflow creates a replay gap. v2 must retain that fact across reboot so
    // an MMS client is not given false confidence that older history exists.
    RuntimeFixture overflow;
    if (!overflow.runtime.initialize() ||
        overflow.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok) {
        return 12;
    }
    for (std::uint64_t now = 100U; now <= 400U; now += 100U) {
        g_value = !g_value;
        if (!capture_one(overflow.runtime, now, staging, workspace)) {
            return 13;
        }
    }
    if (overflow.runtime.retained_size() != 3U || overflow.runtime.queue_size() != 3U ||
        overflow.runtime.dropped_reports() != 1U || !overflow.runtime.replay_gap() ||
        !overflow.runtime.front(front) || !entry_is(front, 2U)) {
        return 14;
    }
    const auto overflow_state = mms::MmsStaticBrcbStateCodec::encode(
        overflow.runtime, state);
    RuntimeFixture overflow_recovered;
    if (!overflow_state.success() || !overflow_recovered.runtime.initialize() ||
        !mms::MmsStaticBrcbStateCodec::restore(
            overflow_recovered.runtime,
            std::span<const std::uint8_t>{state}.first(overflow_state.bytes_written)).success() ||
        overflow_recovered.runtime.retained_size() != 3U ||
        overflow_recovered.runtime.queue_size() != 3U ||
        overflow_recovered.runtime.dropped_reports() != 1U ||
        !overflow_recovered.runtime.replay_gap() ||
        !overflow_recovered.runtime.front(front) || !entry_is(front, 2U)) {
        return 15;
    }

    // Purge is a durable recovery boundary. It clears retained history/gap but
    // does not roll EntryID backward; after another reboot the next report is E5.
    if (overflow_recovered.runtime.purge_buffer() != mms::MmsStaticBrcbStatus::ok ||
        overflow_recovered.runtime.retained_size() != 0U ||
        overflow_recovered.runtime.queue_size() != 0U ||
        overflow_recovered.runtime.replay_gap()) {
        return 16;
    }
    const auto purged_state = mms::MmsStaticBrcbStateCodec::encode(
        overflow_recovered.runtime, state);
    RuntimeFixture purged_recovered;
    if (!purged_state.success() || !purged_recovered.runtime.initialize() ||
        !mms::MmsStaticBrcbStateCodec::restore(
            purged_recovered.runtime,
            std::span<const std::uint8_t>{state}.first(purged_state.bytes_written)).success() ||
        purged_recovered.runtime.retained_size() != 0U ||
        purged_recovered.runtime.queue_size() != 0U ||
        purged_recovered.runtime.replay_gap() ||
        purged_recovered.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok ||
        !capture_one(purged_recovered.runtime, 500U, staging, workspace) ||
        !purged_recovered.runtime.front(front) || !entry_is(front, 5U)) {
        return 17;
    }

    return 0;
}
