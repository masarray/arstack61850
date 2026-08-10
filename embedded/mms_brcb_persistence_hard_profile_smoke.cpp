// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_checkpoint_store.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_brcb_state_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0x00U, 0x12U, 0x34U, 0x00U, 0x01U};
constexpr std::size_t kBankBytes = 4096U;
constexpr std::size_t kStorageBytes = kBankBytes * 2U;

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
    mms::MmsStaticObjectEntry{"LD0", "X1", kBooleanType, read_boolean, &g_value, false}};
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
    17U,
    {0x7FU, 0x80U},
    20U,
    0x70U};

const mms::MmsStaticBrcbDefinition kDifferentDefinition{
    "LD0",
    "LLN0$BR$Events",
    "LD0/LLN0$BR$Different",
    "LD0",
    "LLN0$Events",
    17U,
    {0x7FU, 0x80U},
    20U,
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
    mms::MmsStaticBrcbRuntime runtime;

    explicit RuntimeFixture(const mms::MmsStaticBrcbDefinition& definition) noexcept
        : runtime{definition, pending, slots, kObjectTable, kDataSetTable} {}
};

struct SmallRuntimeFixture final {
    std::array<std::uint8_t, 1024U> slot0{};
    std::array<mms::MmsStaticBrcbSlot, 1U> slots{
        mms::MmsStaticBrcbSlot{slot0}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime runtime{
        kDefinition, pending, slots, kObjectTable, kDataSetTable};
};

struct FakeStorage final {
    std::array<std::uint8_t, kStorageBytes> bytes{};
    std::size_t write_budget{std::numeric_limits<std::size_t>::max()};
    bool fail_reads{};
    bool fail_erases{};
    bool fail_sync{};

    FakeStorage() noexcept {
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0xFFU});
    }
};

[[nodiscard]] bool storage_read(
    void* context,
    const std::size_t offset,
    const std::span<std::uint8_t> destination) noexcept {
    auto* storage = static_cast<FakeStorage*>(context);
    if (storage == nullptr || storage->fail_reads || offset > storage->bytes.size() ||
        destination.size() > storage->bytes.size() - offset) {
        return false;
    }
    std::copy_n(storage->bytes.begin() + offset, destination.size(), destination.begin());
    return true;
}

[[nodiscard]] bool storage_write(
    void* context,
    const std::size_t offset,
    const std::span<const std::uint8_t> source) noexcept {
    auto* storage = static_cast<FakeStorage*>(context);
    if (storage == nullptr || offset > storage->bytes.size() ||
        source.size() > storage->bytes.size() - offset) {
        return false;
    }
    const auto writable = std::min(source.size(), storage->write_budget);
    std::copy_n(source.begin(), writable, storage->bytes.begin() + offset);
    if (storage->write_budget != std::numeric_limits<std::size_t>::max()) {
        storage->write_budget -= writable;
    }
    return writable == source.size();
}

[[nodiscard]] bool storage_erase(
    void* context,
    const std::size_t offset,
    const std::size_t bytes) noexcept {
    auto* storage = static_cast<FakeStorage*>(context);
    if (storage == nullptr || storage->fail_erases || offset > storage->bytes.size() ||
        bytes > storage->bytes.size() - offset) {
        return false;
    }
    std::fill_n(storage->bytes.begin() + offset, bytes, std::uint8_t{0xFFU});
    return true;
}

[[nodiscard]] bool storage_sync(void* context) noexcept {
    const auto* storage = static_cast<const FakeStorage*>(context);
    return storage != nullptr && !storage->fail_sync;
}

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
    if (!runtime.next_due(now_ms + 20U, plan)) {
        return false;
    }
    return runtime.capture(plan, kReportTime, staging, workspace).success();
}

[[nodiscard]] bool entry_is(
    const mms::MmsStaticBrcbEntryView& entry,
    const std::uint8_t low_byte) noexcept {
    return entry.entry_id.size() == 8U && entry.entry_id[7U] == low_byte;
}

} // namespace

int main() {
    FakeStorage storage;
    const mms::MmsStaticBrcbStorageBackend backend{
        &storage,
        storage.bytes.size(),
        storage_read,
        storage_write,
        storage_erase,
        storage_sync};
    const mms::MmsStaticBrcbCheckpointStore store{backend, kBankBytes};
    if (!store.valid() || store.maximum_state_bytes() == 0U) {
        return 1;
    }

    std::array<std::uint8_t, 3072U> state_buffer{};
    std::array<std::uint8_t, 2048U> staging{};
    std::array<std::uint8_t, 256U> workspace{};

    RuntimeFixture empty{kDefinition};
    if (!empty.runtime.initialize() ||
        store.restore(empty.runtime, state_buffer).status !=
            mms::MmsStaticBrcbCheckpointStatus::no_checkpoint) {
        return 2;
    }

    RuntimeFixture source{kDefinition};
    if (!source.runtime.initialize() ||
        source.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok) {
        return 3;
    }
    g_value = true;
    if (!capture_one(source.runtime, 100U, staging, workspace)) {
        return 4;
    }
    g_value = false;
    if (!capture_one(source.runtime, 200U, staging, workspace) ||
        source.runtime.queue_size() != 2U) {
        return 5;
    }

    mms::MmsStaticBrcbEntryView source_front;
    std::array<std::uint8_t, 1024U> expected_front{};
    std::array<std::uint8_t, 8U> expected_entry_id{};
    if (!source.runtime.front(source_front) || source_front.mms_pdu.size() > expected_front.size()) {
        return 6;
    }
    const auto expected_front_bytes = source_front.mms_pdu.size();
    std::copy(source_front.mms_pdu.begin(), source_front.mms_pdu.end(), expected_front.begin());
    std::copy(source_front.entry_id.begin(), source_front.entry_id.end(), expected_entry_id.begin());

    std::array<std::uint8_t, 8U> tiny_state{};
    const auto tiny_checkpoint = store.checkpoint(source.runtime, tiny_state);
    if (tiny_checkpoint.status != mms::MmsStaticBrcbCheckpointStatus::state_buffer_too_small ||
        tiny_checkpoint.required_state_bytes <= tiny_state.size()) {
        return 7;
    }

    const auto first_checkpoint = store.checkpoint(source.runtime, state_buffer);
    if (!first_checkpoint.success() || first_checkpoint.generation != 1U ||
        first_checkpoint.bank_index != 0U || first_checkpoint.state_bytes == 0U) {
        return 8;
    }

    RuntimeFixture recovered{kDefinition};
    if (!recovered.runtime.initialize()) {
        return 9;
    }
    const auto first_restore = store.restore(recovered.runtime, state_buffer);
    mms::MmsStaticBrcbEntryView recovered_front;
    if (!first_restore.success() || first_restore.generation != 1U ||
        recovered.runtime.enabled() || recovered.runtime.queue_size() != 2U ||
        !recovered.runtime.front(recovered_front) ||
        recovered_front.mms_pdu.size() != expected_front_bytes ||
        !std::equal(
            recovered_front.mms_pdu.begin(),
            recovered_front.mms_pdu.end(),
            expected_front.begin()) ||
        !std::equal(
            recovered_front.entry_id.begin(),
            recovered_front.entry_id.end(),
            expected_entry_id.begin())) {
        return 10;
    }

    RuntimeFixture wrong_definition{kDifferentDefinition};
    if (!wrong_definition.runtime.initialize()) {
        return 11;
    }
    const auto mismatch = store.restore(wrong_definition.runtime, state_buffer);
    if (mismatch.status != mms::MmsStaticBrcbCheckpointStatus::state_restore_failed ||
        mismatch.state_status != mms::MmsStaticBrcbStateStatus::definition_mismatch ||
        wrong_definition.runtime.queue_size() != 0U) {
        return 12;
    }

    SmallRuntimeFixture too_small;
    if (!too_small.runtime.initialize()) {
        return 13;
    }
    const auto capacity = store.restore(too_small.runtime, state_buffer);
    if (capacity.status != mms::MmsStaticBrcbCheckpointStatus::state_restore_failed ||
        capacity.state_status != mms::MmsStaticBrcbStateStatus::capacity_mismatch ||
        too_small.runtime.queue_size() != 0U) {
        return 14;
    }

    // Build generation 2: deliver EntryID 1, then append EntryID 3.
    if (!entry_is(recovered_front, 1U) ||
        recovered.runtime.commit_delivery(recovered_front.entry_id) !=
            mms::MmsStaticBrcbStatus::ok ||
        recovered.runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok) {
        return 15;
    }
    g_value = true;
    if (!capture_one(recovered.runtime, 300U, staging, workspace) ||
        recovered.runtime.queue_size() != 2U ||
        !recovered.runtime.front(recovered_front) || !entry_is(recovered_front, 2U)) {
        return 16;
    }
    const auto second_checkpoint = store.checkpoint(recovered.runtime, state_buffer);
    if (!second_checkpoint.success() || second_checkpoint.generation != 2U ||
        second_checkpoint.bank_index != 1U) {
        return 17;
    }

    // Queue now advances to EntryID 4, but checkpoint generation 3 is torn
    // while writing the final commit footer. Generation 2 must survive reboot.
    g_value = false;
    if (!capture_one(recovered.runtime, 400U, staging, workspace) ||
        recovered.runtime.queue_size() != 3U) {
        return 18;
    }
    const auto projected = mms::MmsStaticBrcbStateCodec::encode(
        recovered.runtime, state_buffer);
    if (!projected.success()) {
        return 19;
    }
    storage.write_budget =
        mms::MmsStaticBrcbCheckpointStore::record_header_bytes +
        projected.bytes_written + 8U;
    const auto torn = store.checkpoint(recovered.runtime, state_buffer);
    if (torn.status != mms::MmsStaticBrcbCheckpointStatus::storage_failure ||
        torn.generation != 3U || torn.bank_index != 0U) {
        return 20;
    }
    storage.write_budget = std::numeric_limits<std::size_t>::max();

    RuntimeFixture after_torn{kDefinition};
    if (!after_torn.runtime.initialize()) {
        return 21;
    }
    const auto torn_restore = store.restore(after_torn.runtime, state_buffer);
    mms::MmsStaticBrcbEntryView torn_front;
    if (!torn_restore.success() || torn_restore.generation != 2U ||
        torn_restore.bank_index != 1U || after_torn.runtime.queue_size() != 2U ||
        !after_torn.runtime.front(torn_front) || !entry_is(torn_front, 2U)) {
        return 22;
    }

    // Retry generation 3 successfully. The store alternates back to bank 0.
    const auto third_checkpoint = store.checkpoint(recovered.runtime, state_buffer);
    if (!third_checkpoint.success() || third_checkpoint.generation != 3U ||
        third_checkpoint.bank_index != 0U) {
        return 23;
    }

    // Corrupt the newest payload. Recovery must reject bank 0 by CRC and fall
    // back to still-valid generation 2 in bank 1.
    storage.bytes[mms::MmsStaticBrcbCheckpointStore::record_header_bytes + 10U] ^=
        std::uint8_t{0x01U};
    RuntimeFixture after_corruption{kDefinition};
    if (!after_corruption.runtime.initialize()) {
        return 24;
    }
    const auto corrupt_restore = store.restore(after_corruption.runtime, state_buffer);
    if (!corrupt_restore.success() || corrupt_restore.generation != 2U ||
        corrupt_restore.bank_index != 1U || after_corruption.runtime.queue_size() != 2U ||
        !after_corruption.runtime.front(torn_front) || !entry_is(torn_front, 2U)) {
        return 25;
    }

    // Erasing both banks produces a clean no-checkpoint state, not partial data.
    std::fill(storage.bytes.begin(), storage.bytes.end(), std::uint8_t{0xFFU});
    RuntimeFixture erased{kDefinition};
    if (!erased.runtime.initialize() ||
        store.restore(erased.runtime, state_buffer).status !=
            mms::MmsStaticBrcbCheckpointStatus::no_checkpoint ||
        erased.runtime.queue_size() != 0U) {
        return 26;
    }

    return 0;
}
