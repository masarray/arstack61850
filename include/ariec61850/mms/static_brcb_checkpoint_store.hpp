// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_brcb_state_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

class MmsStaticBrcbRuntime;

struct MmsStaticBrcbStorageBackend final {
    using ReadFn = bool (*)(
        void* context,
        std::size_t offset,
        std::span<std::uint8_t> destination) noexcept;
    using WriteFn = bool (*)(
        void* context,
        std::size_t offset,
        std::span<const std::uint8_t> source) noexcept;
    using EraseFn = bool (*)(
        void* context,
        std::size_t offset,
        std::size_t bytes) noexcept;
    using SyncFn = bool (*)(void* context) noexcept;

    void* context{};
    std::size_t capacity_bytes{};
    ReadFn read{};
    WriteFn write{};
    EraseFn erase{};
    SyncFn sync{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return capacity_bytes != 0U && read != nullptr && write != nullptr &&
            erase != nullptr && sync != nullptr;
    }
};

enum class MmsStaticBrcbCheckpointStatus : std::uint8_t {
    ok,
    no_checkpoint,
    invalid_backend,
    invalid_layout,
    state_buffer_too_small,
    storage_failure,
    state_encode_failed,
    state_restore_failed,
};

struct MmsStaticBrcbCheckpointResult final {
    MmsStaticBrcbCheckpointStatus status{
        MmsStaticBrcbCheckpointStatus::invalid_backend};
    MmsStaticBrcbStateStatus state_status{MmsStaticBrcbStateStatus::ok};
    std::uint64_t generation{};
    std::size_t bank_index{};
    std::size_t state_bytes{};
    std::size_t required_state_bytes{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticBrcbCheckpointStatus::ok;
    }
};

// Two fixed banks are used as an A/B journal. The inactive bank is erased and
// rewritten first; its commit footer is written only after the complete state
// image has been written and synchronized. The previous committed bank is kept
// intact as the recovery fallback.
class MmsStaticBrcbCheckpointStore final {
public:
    static constexpr std::uint16_t format_version = 1U;
    static constexpr std::size_t bank_count = 2U;
    static constexpr std::size_t record_header_bytes = 40U;
    static constexpr std::size_t record_footer_bytes = 32U;

    MmsStaticBrcbCheckpointStore(
        MmsStaticBrcbStorageBackend backend,
        std::size_t bank_bytes) noexcept
        : backend_{backend}, bank_bytes_{bank_bytes} {}

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] constexpr std::size_t bank_bytes() const noexcept {
        return bank_bytes_;
    }
    [[nodiscard]] constexpr std::size_t maximum_state_bytes() const noexcept {
        return bank_bytes_ >= record_header_bytes + record_footer_bytes
            ? bank_bytes_ - record_header_bytes - record_footer_bytes
            : 0U;
    }

    [[nodiscard]] MmsStaticBrcbCheckpointResult checkpoint(
        const MmsStaticBrcbRuntime& runtime,
        std::span<std::uint8_t> state_buffer) const noexcept;

    [[nodiscard]] MmsStaticBrcbCheckpointResult restore(
        MmsStaticBrcbRuntime& runtime,
        std::span<std::uint8_t> state_buffer) const noexcept;

private:
    MmsStaticBrcbStorageBackend backend_{};
    std::size_t bank_bytes_{};
};

} // namespace ar::iec61850::mms
