// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_checkpoint_store.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::size_t kBankBytes = 4096U;
constexpr std::size_t kStorageBytes = kBankBytes * 2U;

bool gValue = true;

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
    mms::MmsStaticObjectEntry{"LD0", "X1", kBooleanType, read_boolean, &gValue, false}};
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

struct RuntimeFixture final {
    std::array<std::uint8_t, 1024U> slot0{};
    std::array<std::uint8_t, 1024U> slot1{};
    std::array<mms::MmsStaticBrcbSlot, 2U> slots{
        mms::MmsStaticBrcbSlot{slot0},
        mms::MmsStaticBrcbSlot{slot1}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime runtime{
        kDefinition, pending, slots, kObjectTable, kDataSetTable};
};

struct FakeStorage final {
    std::array<std::uint8_t, kStorageBytes> bytes{};
    bool fail_bank0_reads{};
    bool fail_bank1_reads{};

    FakeStorage() noexcept {
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0xFFU});
    }
};

[[nodiscard]] bool storage_read(
    void* context,
    const std::size_t offset,
    const std::span<std::uint8_t> destination) noexcept {
    auto* storage = static_cast<FakeStorage*>(context);
    if (storage == nullptr || offset > storage->bytes.size() ||
        destination.size() > storage->bytes.size() - offset) {
        return false;
    }
    if ((storage->fail_bank0_reads && offset < kBankBytes) ||
        (storage->fail_bank1_reads && offset >= kBankBytes)) {
        return false;
    }
    std::copy_n(
        storage->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        destination.size(),
        destination.begin());
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
    std::copy(
        source.begin(),
        source.end(),
        storage->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

[[nodiscard]] bool storage_erase(
    void* context,
    const std::size_t offset,
    const std::size_t bytes) noexcept {
    auto* storage = static_cast<FakeStorage*>(context);
    if (storage == nullptr || offset > storage->bytes.size() ||
        bytes > storage->bytes.size() - offset) {
        return false;
    }
    std::fill_n(
        storage->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes,
        std::uint8_t{0xFFU});
    return true;
}

[[nodiscard]] bool storage_sync(void* context) noexcept {
    return context != nullptr;
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
    if (!store.valid()) {
        return 1;
    }

    RuntimeFixture source;
    if (!source.runtime.initialize()) {
        return 2;
    }
    std::array<std::uint8_t, 3072U> state_buffer{};
    const auto first = store.checkpoint(source.runtime, state_buffer);
    const auto second = store.checkpoint(source.runtime, state_buffer);
    if (!first.success() || first.generation != 1U || first.bank_index != 0U ||
        !second.success() || second.generation != 2U || second.bank_index != 1U) {
        return 3;
    }

    // Bank 0 is unreadable, but the newer committed bank 1 remains valid.
    storage.fail_bank0_reads = true;
    RuntimeFixture from_bank1;
    if (!from_bank1.runtime.initialize()) {
        return 4;
    }
    const auto bank1_restore = store.restore(from_bank1.runtime, state_buffer);
    if (!bank1_restore.success() || bank1_restore.generation != 2U ||
        bank1_restore.bank_index != 1U) {
        return 5;
    }

    // Reverse the fault. The older bank 0 must remain a usable recovery point.
    storage.fail_bank0_reads = false;
    storage.fail_bank1_reads = true;
    RuntimeFixture from_bank0;
    if (!from_bank0.runtime.initialize()) {
        return 6;
    }
    const auto bank0_restore = store.restore(from_bank0.runtime, state_buffer);
    if (!bank0_restore.success() || bank0_restore.generation != 1U ||
        bank0_restore.bank_index != 0U) {
        return 7;
    }

    return 0;
}
