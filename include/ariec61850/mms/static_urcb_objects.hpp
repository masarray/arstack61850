// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

using MmsStaticNowMsCallback = std::uint64_t (*)(const void* context) noexcept;

enum class MmsStaticUrcbAttribute : std::uint8_t {
    report_id,
    report_enabled,
    reserved,
    data_set,
    conf_revision,
    optional_fields,
    buffer_time,
    trigger_options,
    integrity_period,
    general_interrogation,
    sequence_number,
};

struct MmsStaticUrcbObjectContext final {
    MmsStaticUrcbRuntime* runtime{};
    std::size_t index{};
    MmsStaticUrcbAttribute attribute{MmsStaticUrcbAttribute::report_id};
    MmsStaticNowMsCallback now_ms{};
    const void* now_context{};
};

class MmsStaticUrcbObjectBank final {
public:
    static constexpr std::size_t attributes_per_control_block = 11U;

    MmsStaticUrcbObjectBank(
        MmsStaticUrcbRuntime& runtime,
        std::span<const MmsStaticObjectEntry> base_objects,
        std::span<MmsStaticObjectEntry> object_storage,
        std::span<MmsStaticUrcbObjectContext> context_storage,
        std::span<char> name_storage,
        MmsStaticNowMsCallback now_ms = nullptr,
        const void* now_context = nullptr) noexcept
        : runtime_{&runtime},
          base_objects_{base_objects},
          object_storage_{object_storage},
          context_storage_{context_storage},
          name_storage_{name_storage},
          now_ms_{now_ms},
          now_context_{now_context} {}

    [[nodiscard]] bool initialize() noexcept;

    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }
    [[nodiscard]] constexpr std::size_t object_count() const noexcept {
        return initialized_ ? object_count_ : 0U;
    }
    [[nodiscard]] constexpr std::size_t name_bytes_used() const noexcept {
        return initialized_ ? name_bytes_used_ : 0U;
    }

    [[nodiscard]] std::size_t required_object_capacity() const noexcept;
    [[nodiscard]] std::size_t required_context_capacity() const noexcept;
    [[nodiscard]] std::size_t required_name_bytes() const noexcept;

    [[nodiscard]] constexpr const MmsStaticObjectTable& table() const noexcept {
        return table_;
    }

private:
    MmsStaticUrcbRuntime* runtime_{};
    std::span<const MmsStaticObjectEntry> base_objects_{};
    std::span<MmsStaticObjectEntry> object_storage_{};
    std::span<MmsStaticUrcbObjectContext> context_storage_{};
    std::span<char> name_storage_{};
    MmsStaticNowMsCallback now_ms_{};
    const void* now_context_{};
    MmsStaticObjectTable table_{std::span<const MmsStaticObjectEntry>{}};
    std::size_t object_count_{};
    std::size_t name_bytes_used_{};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
