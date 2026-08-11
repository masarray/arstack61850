// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

struct MmsStaticDirectBooleanOperate final {
    bool control_value{};
    std::uint8_t origin_category{};
    std::uint8_t control_number{};
    bool test{};
    bool synchro_check{};
    bool interlock_check{};
};

struct MmsStaticDirectBooleanControlState final {
    std::uint8_t value{};
    std::uint8_t last_control_number{};
    bool last_test{};
    std::size_t accepted_operations{};
    std::size_t rejected_operations{};
};

using MmsStaticBooleanApplyCallback = bool (*)(
    void* context,
    bool value) noexcept;

struct MmsStaticDirectBooleanControlPolicy final {
    bool allow_test{true};
    bool allow_synchro_check{};
    bool allow_interlock_check{};
    std::uint32_t malformed_failure_code{7U};      // type-inconsistent
    std::uint32_t invalid_value_failure_code{11U}; // object-value-invalid
    std::uint32_t backend_failure_code{10U};       // object-non-existent/backend
};

struct MmsStaticDirectBooleanControlBinding final {
    MmsStaticDirectBooleanControlState* state{};
    MmsStaticBooleanApplyCallback apply{};
    void* apply_context{};
    MmsStaticDirectBooleanControlPolicy policy{};
};

// Decode one IEC 61850 Direct-with-normal-security Oper value with the exact
// ordered structure used by the live TypeSpecification exposed by this server:
// ctlVal, origin(orCat,orIdent), ctlNum, T, Test, Check.
// The parser is allocation-free and accepts only the bounded scalar shape.
[[nodiscard]] bool try_decode_static_direct_boolean_operate(
    std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept;

// Read callbacks suitable for MmsStaticObjectEntry.
[[nodiscard]] wire::EncodeResult mms_static_direct_boolean_read_state(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

[[nodiscard]] wire::EncodeResult mms_static_direct_normal_read_ctl_model(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

[[nodiscard]] wire::EncodeResult mms_static_control_read_unavailable(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

// Write callback suitable for the CO$...$Oper MmsStaticObjectEntry.
// Test=true is acknowledged without changing the live output state. Check bits
// are denied by default until the application explicitly enables them.
[[nodiscard]] MmsStaticWriteResult mms_static_direct_boolean_write_oper(
    void* context,
    std::span<const std::uint8_t> encoded_data) noexcept;

} // namespace ar::iec61850::mms
