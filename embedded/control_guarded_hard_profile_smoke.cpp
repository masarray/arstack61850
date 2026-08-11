// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/guarded_control.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850::control;

bool allow_all(
    void*,
    ControlAction,
    const ControlObjectReference&,
    const ControlClientIdentity&,
    const ControlSequenceView&) noexcept {
    return true;
}

struct AuthState final {
    bool allow{true};
    std::uint32_t calls{};
};

bool controlled_authorize(
    void* context,
    ControlAction,
    const ControlObjectReference&,
    const ControlClientIdentity&,
    const ControlSequenceView&) noexcept {
    auto* state = static_cast<AuthState*>(context);
    if (state == nullptr) {
        return false;
    }
    ++state->calls;
    return state->allow;
}

ControlSequenceView sequence(
    const std::span<const std::uint8_t> value,
    const std::span<const std::uint8_t> origin,
    const std::uint8_t control_number = 0U,
    const bool synchro = true,
    const bool interlock = true) noexcept {
    ControlSequenceView result;
    result.control_value = value;
    result.origin_category = OriginCategory::station_control;
    result.origin_identifier = origin;
    result.control_number = control_number;
    result.timestamp_token = 0x1122334455667788ULL;
    result.operate_at_token = 0U;
    result.test = false;
    result.interlock_check = interlock;
    result.synchro_check = synchro;
    return result;
}

} // namespace

int main() {
    ControlObjectReference object;
    if (!try_parse_control_object_reference(" LD0/CSWI1.Pos ", object) ||
        object.domain != "LD0" || object.logical_node != "CSWI1" ||
        object.data_object_path != "Pos") {
        return 1;
    }
    ControlObjectReference rejected;
    if (try_parse_control_object_reference("LD0/CSWI1.Pos.Oper", rejected) ||
        try_parse_control_object_reference("LD0/CSWI1.Pos.Cancel", rejected) ||
        try_parse_control_object_reference("LD0/CSWI1", rejected)) {
        return 2;
    }

    std::array<char, 64U> item{};
    std::size_t item_bytes{};
    if (!build_control_item(object, "CF", "ctlModel", item, item_bytes) ||
        std::string_view{item.data(), item_bytes} != "CSWI1$CF$Pos$ctlModel" ||
        !build_control_item(object, "CO", "Oper", item, item_bytes) ||
        std::string_view{item.data(), item_bytes} != "CSWI1$CO$Pos$Oper") {
        return 3;
    }
    std::array<char, 4U> tiny{};
    if (build_control_item(object, "CO", "Oper", tiny, item_bytes) ||
        item_bytes <= tiny.size()) {
        return 4;
    }

    constexpr std::array<std::uint8_t, 3U> on_value{0x83U, 0x01U, 0xFFU};
    constexpr std::array<std::uint8_t, 3U> off_value{0x83U, 0x01U, 0x00U};
    constexpr std::array<std::uint8_t, 3U> origin_a{'H', 'M', 'I'};
    constexpr std::array<std::uint8_t, 3U> origin_b{'E', 'N', 'G'};
    constexpr ControlClientIdentity client_a{0xA1U};
    constexpr ControlClientIdentity client_b{0xB2U};

    // Default-deny is intentional: a usable control object still refuses an
    // Oper unless an application supplies an explicit authorization callback.
    GuardedControlPlanner denied{object, ControlModel::direct_normal};
    auto result = denied.operate(client_a, sequence(on_value, origin_a), 100U);
    if (result.status != GuardedControlStatus::access_denied ||
        result.add_cause != AddCause::no_access_authority) {
        return 5;
    }

    GuardedControlPolicy allow_policy;
    allow_policy.sbo_timeout_ms = 1'000U;
    allow_policy.authorize = allow_all;

    // Direct-normal completion boundary is MMS service acceptance. ctlNum is
    // association-local monotonic 1..255 and wraps without using zero.
    GuardedControlPlanner direct{object, ControlModel::direct_normal, allow_policy};
    result = direct.operate(client_a, sequence(on_value, origin_a), 100U);
    if (result.status != GuardedControlStatus::ok || result.control_number != 1U ||
        direct.state(100U).owner_association_id != 0U) {
        return 6;
    }
    result = direct.operate(client_a, sequence(off_value, origin_a), 110U);
    if (result.status != GuardedControlStatus::ok || result.control_number != 2U) {
        return 7;
    }
    std::uint8_t last_ctl_num = result.control_number;
    for (std::uint16_t index = 0U; index < 254U; ++index) {
        result = direct.operate(client_a, sequence(on_value, origin_a), 120U + index);
        if (!result.accepted()) {
            return 8;
        }
        last_ctl_num = result.control_number;
    }
    if (last_ctl_num != 1U) {
        return 9;
    }

    // Direct-enhanced stays busy after Write acceptance until an explicit
    // matching CommandTermination boundary is supplied by the upper layer.
    GuardedControlPlanner direct_enhanced{
        object, ControlModel::direct_enhanced, allow_policy};
    result = direct_enhanced.operate(client_a, sequence(on_value, origin_a), 1'000U);
    if (result.status != GuardedControlStatus::accepted_waiting_termination ||
        !direct_enhanced.state(1'000U).waiting_for_termination) {
        return 10;
    }
    const auto busy = direct_enhanced.operate(
        client_a, sequence(off_value, origin_a), 1'001U);
    if (busy.status != GuardedControlStatus::command_already_in_execution ||
        busy.add_cause != AddCause::command_already_in_execution) {
        return 11;
    }
    result = direct_enhanced.command_termination(ControlError::no_error, AddCause::none);
    if (result.status != GuardedControlStatus::positive_termination ||
        direct_enhanced.state(1'002U).waiting_for_termination) {
        return 12;
    }
    result = direct_enhanced.operate(client_a, sequence(on_value, origin_a), 1'003U);
    if (result.status != GuardedControlStatus::accepted_waiting_termination) {
        return 13;
    }
    result = direct_enhanced.command_termination(
        ControlError::no_error, AddCause::blocked_by_interlocking);
    if (result.status != GuardedControlStatus::negative_termination ||
        result.add_cause != AddCause::blocked_by_interlocking) {
        return 14;
    }

    // SBO normal: selection is per-object/per-association and the exact
    // ctlVal/origin/ctlNum/T/Test/Check sequence is immutable until Oper/Cancel.
    GuardedControlPlanner sbo{
        object, ControlModel::select_before_operate_normal, allow_policy};
    result = sbo.select(client_a, sequence(on_value, origin_a), 2'000U);
    if (result.status != GuardedControlStatus::ok || result.control_number != 1U ||
        !sbo.state(2'000U).selected) {
        return 15;
    }
    const auto selected_ctl_num = result.control_number;
    result = sbo.select(client_b, sequence(on_value, origin_b), 2'001U);
    if (result.status != GuardedControlStatus::locked_by_other_client ||
        result.add_cause != AddCause::locked_by_other_client) {
        return 16;
    }
    result = sbo.operate(client_b, sequence(on_value, origin_a), 2'002U);
    if (result.status != GuardedControlStatus::locked_by_other_client) {
        return 17;
    }
    result = sbo.operate(
        client_a, sequence(off_value, origin_a, selected_ctl_num), 2'003U);
    if (result.status != GuardedControlStatus::sequence_mismatch ||
        result.control_number != selected_ctl_num || sbo.state(2'003U).selected) {
        return 18;
    }

    result = sbo.select(client_a, sequence(on_value, origin_a), 2'100U);
    if (!result.accepted()) {
        return 19;
    }
    result = sbo.operate(client_a, sequence(on_value, origin_a), 2'101U);
    if (result.status != GuardedControlStatus::ok || result.control_number == 0U ||
        sbo.state(2'101U).selected) {
        return 20;
    }

    // Check bits are sequence identity. Changing synchro/interlock after select
    // is rejected instead of silently operating with a stale selection.
    result = sbo.select(client_a, sequence(on_value, origin_a), 2'200U);
    if (!result.accepted()) {
        return 21;
    }
    result = sbo.operate(client_a, sequence(on_value, origin_a, 0U, false, true), 2'201U);
    if (result.status != GuardedControlStatus::sequence_mismatch) {
        return 22;
    }

    // Selection timeout is a hard boundary and maps to the C# oracle's
    // time-limit-over semantics.
    result = sbo.select(client_a, sequence(on_value, origin_a), 3'000U);
    if (!result.accepted()) {
        return 23;
    }
    result = sbo.operate(client_a, sequence(on_value, origin_a), 4'000U);
    if (result.status != GuardedControlStatus::selection_expired ||
        result.add_cause != AddCause::time_limit_over) {
        return 24;
    }

    // Cancel is owner-only and releases the selection locally after service
    // acceptance; the transport layer will encode the retained selected values.
    result = sbo.select(client_a, sequence(on_value, origin_a), 5'000U);
    if (!result.accepted()) {
        return 25;
    }
    result = sbo.cancel(client_b, 5'001U);
    if (result.status != GuardedControlStatus::locked_by_other_client) {
        return 26;
    }
    result = sbo.cancel(client_a, 5'002U);
    if (result.status != GuardedControlStatus::ok ||
        result.add_cause != AddCause::abortion_by_cancel ||
        sbo.state(5'002U).selected) {
        return 27;
    }

    // SBO enhanced uses SelectWithValue, then Oper, then CommandTermination.
    GuardedControlPlanner sbo_enhanced{
        object, ControlModel::select_before_operate_enhanced, allow_policy};
    result = sbo_enhanced.select_with_value(
        client_a, sequence(on_value, origin_a), 6'000U);
    if (result.status != GuardedControlStatus::ok) {
        return 28;
    }
    result = sbo_enhanced.operate(client_a, sequence(on_value, origin_a), 6'001U);
    if (result.status != GuardedControlStatus::accepted_waiting_termination ||
        !sbo_enhanced.state(6'001U).waiting_for_termination) {
        return 29;
    }
    result = sbo_enhanced.select_with_value(
        client_a, sequence(off_value, origin_a), 6'002U);
    if (result.status != GuardedControlStatus::command_already_in_execution) {
        return 30;
    }
    sbo_enhanced.on_association_closed(client_a.association_id);
    if (sbo_enhanced.state(6'003U).waiting_for_termination ||
        sbo_enhanced.state(6'003U).owner_association_id != 0U) {
        return 31;
    }

    // Authorization is rechecked at Oper. Losing permission between Select and
    // Oper cancels local ownership rather than executing a previously approved command.
    AuthState auth{};
    GuardedControlPolicy changing_policy;
    changing_policy.sbo_timeout_ms = 1'000U;
    changing_policy.authorization_context = &auth;
    changing_policy.authorize = controlled_authorize;
    GuardedControlPlanner guarded{
        object, ControlModel::select_before_operate_normal, changing_policy};
    result = guarded.select(client_a, sequence(on_value, origin_a), 7'000U);
    if (!result.accepted() || auth.calls != 1U) {
        return 32;
    }
    auth.allow = false;
    result = guarded.operate(client_a, sequence(on_value, origin_a), 7'001U);
    if (result.status != GuardedControlStatus::access_denied ||
        guarded.state(7'001U).selected || auth.calls != 2U) {
        return 33;
    }

    GuardedControlPlanner status_only{object, ControlModel::status_only, allow_policy};
    result = status_only.operate(client_a, sequence(on_value, origin_a), 8'000U);
    if (result.status != GuardedControlStatus::unsupported ||
        result.add_cause != AddCause::not_supported) {
        return 34;
    }

    return 0;
}
