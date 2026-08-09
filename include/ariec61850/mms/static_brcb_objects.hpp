// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

using MmsStaticBrcbNowMsCallback = std::uint64_t (*)(const void* context) noexcept;

enum class MmsStaticBrcbAttribute : std::uint8_t {
    report_id,
    report_enabled,
    data_set,
    conf_revision,
    purge_buffer,
    entry_id,
    reservation_time,
    owner,
};

struct MmsStaticBrcbObjectContext final {
    const MmsStaticBrcbDefinition* definition{};
    MmsStaticBrcbRuntime* reports{};
    MmsStaticBrcbControl* control{};
    MmsStaticBrcbAttribute attribute{MmsStaticBrcbAttribute::report_id};
    MmsStaticBrcbNowMsCallback now_ms{};
    const void* now_context{};
};

// Allocation-free MMS object facade for one bounded BRCB runtime. The caller
// owns object/context/name storage. Association-sensitive writes are routed via
// MmsStaticContextualWriteCallback and therefore receive the active request's
// opaque association/Owner identity.
class MmsStaticBrcbObjectBank final {
public:
    static constexpr std::size_t attributes_per_control_block = 8U;

    MmsStaticBrcbObjectBank(
        const MmsStaticBrcbDefinition& definition,
        MmsStaticBrcbRuntime& reports,
        MmsStaticBrcbControl& control,
        std::span<const MmsStaticObjectEntry> base_objects,
        std::span<MmsStaticObjectEntry> object_storage,
        std::span<MmsStaticBrcbObjectContext> context_storage,
        std::span<char> name_storage,
        MmsStaticBrcbNowMsCallback now_ms = nullptr,
        const void* now_context = nullptr) noexcept
        : definition_{&definition},
          reports_{&reports},
          control_{&control},
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
    [[nodiscard]] constexpr std::size_t required_context_capacity() const noexcept {
        return attributes_per_control_block;
    }
    [[nodiscard]] std::size_t required_name_bytes() const noexcept;

    [[nodiscard]] constexpr const MmsStaticObjectTable& table() const noexcept {
        return table_;
    }

private:
    const MmsStaticBrcbDefinition* definition_{};
    MmsStaticBrcbRuntime* reports_{};
    MmsStaticBrcbControl* control_{};
    std::span<const MmsStaticObjectEntry> base_objects_{};
    std::span<MmsStaticObjectEntry> object_storage_{};
    std::span<MmsStaticBrcbObjectContext> context_storage_{};
    std::span<char> name_storage_{};
    MmsStaticBrcbNowMsCallback now_ms_{};
    const void* now_context_{};
    MmsStaticObjectTable table_{std::span<const MmsStaticObjectEntry>{}};
    std::size_t object_count_{};
    std::size_t name_bytes_used_{};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
