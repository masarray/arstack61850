// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_object_table.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

using MmsStaticSettingGroupUtcCallback = bool (*)(
    const void* context,
    std::span<std::uint8_t, 8U> destination) noexcept;

struct MmsStaticSettingGroupSharedState final {
    explicit MmsStaticSettingGroupSharedState(
        const std::uint32_t number_of_groups,
        const std::uint32_t active_group) noexcept
        : number_of_setting_groups{number_of_groups},
          active_setting_group{active_group} {}

    [[nodiscard]] bool valid() const noexcept {
        const auto active = active_setting_group.load(std::memory_order_acquire);
        return number_of_setting_groups != 0U &&
            active != 0U && active <= number_of_setting_groups;
    }

    std::uint32_t number_of_setting_groups{};
    std::atomic<std::uint32_t> active_setting_group{};
    // Eight encoded IEC 61850 UTC-time octets packed big-endian. Zero means
    // the simulator has no source evidence for a previous activation time.
    std::atomic<std::uint64_t> last_activation_time_be{};
    // ActSG changes are rare configuration operations. Contention is rejected
    // immediately instead of blocking a protocol worker.
    std::atomic_flag activation_in_progress = ATOMIC_FLAG_INIT;
};

struct MmsStaticSettingGroupDefinition final {
    std::string_view domain;
    std::string_view item; // canonical SGCB root, e.g. LLN0$SP$SGCB
};

enum class MmsStaticSettingGroupAttribute : std::uint8_t {
    active_setting_group,
    confirm_edit,
    edit_setting_group,
    last_activation_time,
    number_of_setting_groups,
};

struct MmsStaticSettingGroupObjectContext final {
    MmsStaticSettingGroupSharedState* state{};
    MmsStaticSettingGroupAttribute attribute{
        MmsStaticSettingGroupAttribute::active_setting_group};
    MmsStaticSettingGroupUtcCallback now_utc{};
    const void* now_context{};
};

// Exact five-attribute SGCB facade used by engineering clients:
// ActSG,CnfEdit,EditSG,LActTm,NumOfSG. Full SE/EditSG/CnfEdit transactions are
// intentionally outside this runtime; only guarded ActSG activation is writable.
class MmsStaticSettingGroupObjectBank final {
public:
    static constexpr std::size_t attributes_per_control_block = 5U;

    MmsStaticSettingGroupObjectBank(
        const MmsStaticSettingGroupDefinition& definition,
        MmsStaticSettingGroupSharedState& state,
        std::span<const MmsStaticObjectEntry> base_objects,
        std::span<MmsStaticObjectEntry> object_storage,
        std::span<MmsStaticSettingGroupObjectContext> context_storage,
        std::span<char> name_storage,
        MmsStaticSettingGroupUtcCallback now_utc = nullptr,
        const void* now_context = nullptr) noexcept
        : definition_{&definition},
          state_{&state},
          base_objects_{base_objects},
          object_storage_{object_storage},
          context_storage_{context_storage},
          name_storage_{name_storage},
          now_utc_{now_utc},
          now_context_{now_context} {}

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] std::size_t required_object_capacity() const noexcept;
    [[nodiscard]] constexpr std::size_t required_context_capacity() const noexcept {
        return attributes_per_control_block;
    }
    [[nodiscard]] std::size_t required_name_bytes() const noexcept;

    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }
    [[nodiscard]] constexpr const MmsStaticObjectTable& table() const noexcept {
        return table_;
    }

private:
    const MmsStaticSettingGroupDefinition* definition_{};
    MmsStaticSettingGroupSharedState* state_{};
    std::span<const MmsStaticObjectEntry> base_objects_{};
    std::span<MmsStaticObjectEntry> object_storage_{};
    std::span<MmsStaticSettingGroupObjectContext> context_storage_{};
    std::span<char> name_storage_{};
    MmsStaticSettingGroupUtcCallback now_utc_{};
    const void* now_context_{};
    MmsStaticObjectTable table_{std::span<const MmsStaticObjectEntry>{}};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
