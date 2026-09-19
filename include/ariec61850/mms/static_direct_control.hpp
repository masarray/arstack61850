// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

enum class MmsStaticControlModel : std::uint8_t {
    status_only = 0U,
    direct_normal = 1U,
    sbo_normal = 2U,
    direct_enhanced = 3U,
    sbo_enhanced = 4U,
};

struct MmsStaticDirectBooleanOperate final {
    bool control_value{};
    std::uint8_t origin_category{};
    std::array<std::uint8_t, 64U> origin_identifier{};
    std::size_t origin_identifier_size{};
    std::uint8_t control_number{};
    std::array<std::uint8_t, 8U> timestamp{};
    bool test{};
    bool synchro_check{};
    bool interlock_check{};
};

// Process/selection state shared by every association serving the same command
// Data Object. The atomics are intentionally limited to cross-association facts;
// command correlation and diagnostics remain association-local below.
struct MmsStaticDirectBooleanSharedState final {
    std::atomic<std::uint8_t> value{};
    std::atomic<std::uint64_t> selected_association_id{};
    std::atomic<std::uint64_t> selection_deadline_ms{};
};

// Per-association control state. One instance belongs to exactly one MMS
// association and must not be shared between workers.
struct MmsStaticDirectBooleanControlState final {
    std::uint8_t value{};
    std::uint8_t last_control_number{};
    bool last_test{};
    std::size_t accepted_operations{};
    std::size_t rejected_operations{};

    bool selected{};
    bool selected_with_value{};
    MmsStaticDirectBooleanOperate selected_command{};

    bool pending_termination{};
    MmsStaticDirectBooleanOperate termination_command{};
};

using MmsStaticBooleanApplyCallback = bool (*)(
    void* context,
    bool value) noexcept;

using MmsStaticControlNowCallback = std::uint64_t (*)(
    const void* context) noexcept;

struct MmsStaticDirectBooleanControlPolicy final {
    bool allow_test{true};
    bool allow_synchro_check{};
    bool allow_interlock_check{};
    std::uint32_t malformed_failure_code{7U};       // type-inconsistent
    std::uint32_t invalid_value_failure_code{11U};  // object-value-invalid
    std::uint32_t backend_failure_code{10U};        // object-non-existent/backend
    std::uint32_t temporarily_unavailable_code{2U}; // temporarily-unavailable
};

struct MmsStaticDirectBooleanControlBinding final {
    // Legacy fields stay first so existing aggregate initializers remain source
    // compatible with the original Direct-Normal vertical slice.
    MmsStaticDirectBooleanControlState* state{};
    MmsStaticBooleanApplyCallback apply{};
    void* apply_context{};
    MmsStaticDirectBooleanControlPolicy policy{};

    // Extended control semantics.
    MmsStaticDirectBooleanSharedState* shared_state{};
    MmsStaticControlModel model{MmsStaticControlModel::direct_normal};
    std::uint64_t association_id{};
    std::string_view selection_reference{};
    std::uint64_t sbo_timeout_ms{10'000U};
    MmsStaticControlNowCallback now_ms{};
    const void* now_context{};
};

// Decode one IEC 61850 boolean control structure. Oper/SBOw use the exact
// ordered shape ctlVal, origin(orCat,orIdent), ctlNum, T, Test, Check. Cancel
// uses the same correlation fields without Check.
[[nodiscard]] bool try_decode_static_direct_boolean_operate(
    std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept;

[[nodiscard]] bool try_decode_static_boolean_cancel(
    std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept;

// Read callbacks suitable for MmsStaticObjectEntry.
[[nodiscard]] wire::EncodeResult mms_static_direct_boolean_read_state(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

// Backward-compatible Direct-Normal ctlModel reader.
[[nodiscard]] wire::EncodeResult mms_static_direct_normal_read_ctl_model(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

// Configured ctlModel reader. context must point to a binding.
[[nodiscard]] wire::EncodeResult mms_static_control_read_ctl_model(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

// SBO-normal selection is a Read service. A successful selection returns the
// selected-object reference as MMS VisibleString; contention returns an empty
// VisibleString without mutating the current owner.
[[nodiscard]] wire::EncodeResult mms_static_sbo_normal_read(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

[[nodiscard]] wire::EncodeResult mms_static_control_read_unavailable(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

// Backward-compatible regular Write callback for Direct-Normal Oper.
[[nodiscard]] MmsStaticWriteResult mms_static_direct_boolean_write_oper(
    void* context,
    std::span<const std::uint8_t> encoded_data) noexcept;

// Association-aware service callbacks used by the simulator/server. They
// validate that the callback is executed for the binding's owning association.
[[nodiscard]] MmsStaticWriteResult mms_static_boolean_write_oper_contextual(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;

[[nodiscard]] MmsStaticWriteResult mms_static_boolean_write_sbow_contextual(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;

[[nodiscard]] MmsStaticWriteResult mms_static_boolean_write_cancel_contextual(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;

// Release an SBO reservation when an association closes. Safe to call for all
// control models; Direct models are a no-op apart from local state cleanup.
void mms_static_control_on_association_closed(
    MmsStaticDirectBooleanControlBinding& binding) noexcept;

} // namespace ar::iec61850::mms
