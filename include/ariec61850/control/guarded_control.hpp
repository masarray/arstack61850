// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::control {

enum class ControlModel : std::uint8_t {
    status_only = 0U,
    direct_normal = 1U,
    select_before_operate_normal = 2U,
    direct_enhanced = 3U,
    select_before_operate_enhanced = 4U,
    unknown = 0xFFU,
};

enum class ControlAction : std::uint8_t {
    select,
    select_with_value,
    operate,
    cancel,
};

enum class OriginCategory : std::uint8_t {
    not_supported = 0U,
    bay_control = 1U,
    station_control = 2U,
    remote_control = 3U,
    automatic_bay = 4U,
    automatic_station = 5U,
    automatic_remote = 6U,
    maintenance = 7U,
    process = 8U,
};

enum class ControlError : std::uint8_t {
    no_error = 0U,
    unknown = 1U,
    timeout_test = 2U,
    operator_test = 3U,
};

enum class AddCause : std::uint8_t {
    unknown = 0U,
    not_supported = 1U,
    blocked_by_switching_hierarchy = 2U,
    select_failed = 3U,
    invalid_position = 4U,
    position_reached = 5U,
    parameter_change_in_execution = 6U,
    step_limit = 7U,
    blocked_by_mode = 8U,
    blocked_by_process = 9U,
    blocked_by_interlocking = 10U,
    blocked_by_synchrocheck = 11U,
    command_already_in_execution = 12U,
    blocked_by_health = 13U,
    one_of_n_control = 14U,
    abortion_by_cancel = 15U,
    time_limit_over = 16U,
    abortion_by_trip = 17U,
    object_not_selected = 18U,
    object_already_selected = 19U,
    no_access_authority = 20U,
    ended_with_overshoot = 21U,
    abortion_due_to_deviation = 22U,
    abortion_by_communication_loss = 23U,
    abortion_by_command = 24U,
    none = 25U,
    inconsistent_parameters = 26U,
    locked_by_other_client = 27U,
};

enum class GuardedControlStatus : std::uint8_t {
    ok,
    accepted_waiting_termination,
    positive_termination,
    negative_termination,
    unsupported,
    invalid_identity,
    invalid_sequence,
    access_denied,
    object_not_selected,
    object_already_selected,
    locked_by_other_client,
    selection_expired,
    sequence_mismatch,
    command_already_in_execution,
    no_active_selection,
    no_pending_termination,
};

struct ControlObjectReference final {
    std::string_view domain{};
    std::string_view logical_node{};
    std::string_view data_object_path{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !domain.empty() && !logical_node.empty() && !data_object_path.empty();
    }
};

// Parse the public control-object root used by the C# oracle: LD/LN.DO[.subDO].
// Service leaves (Oper/SBO/SBOw/Cancel/ctlModel/etc.) are intentionally rejected.
[[nodiscard]] bool try_parse_control_object_reference(
    std::string_view reference,
    ControlObjectReference& output) noexcept;

// Build an MMS variable item such as LN$CF$DO$ctlModel or LN$CO$DO$Oper.
// The domain is returned separately by ControlObjectReference::domain.
[[nodiscard]] bool build_control_item(
    const ControlObjectReference& object,
    std::string_view functional_constraint,
    std::string_view service_leaf,
    std::span<char> destination,
    std::size_t& bytes_written) noexcept;

struct ControlClientIdentity final {
    std::uint64_t association_id{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return association_id != 0U;
    }
};

// `control_value` is one exact encoded MMS Data value (not an MMS Write PDU).
// The planner fingerprints it at selection/operate boundaries rather than
// allocating/copying an unbounded vendor-specific value.
struct ControlSequenceView final {
    std::span<const std::uint8_t> control_value{};
    OriginCategory origin_category{OriginCategory::station_control};
    std::span<const std::uint8_t> origin_identifier{};
    std::uint8_t control_number{}; // 0 => association-local auto allocation.
    std::uint64_t timestamp_token{}; // normalized T identity supplied by caller.
    std::uint64_t operate_at_token{}; // 0 => immediate, otherwise normalized operTm.
    bool test{};
    bool interlock_check{};
    bool synchro_check{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !control_value.empty() && origin_identifier.size() <= 64U &&
            static_cast<std::uint8_t>(origin_category) <=
                static_cast<std::uint8_t>(OriginCategory::process);
    }
};

struct GuardedControlDecision final {
    GuardedControlStatus status{GuardedControlStatus::unsupported};
    ControlAction action{ControlAction::operate};
    std::uint8_t control_number{};
    ControlError control_error{ControlError::no_error};
    AddCause add_cause{AddCause::none};

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return status == GuardedControlStatus::ok ||
            status == GuardedControlStatus::accepted_waiting_termination ||
            status == GuardedControlStatus::positive_termination;
    }
};

using ControlAuthorizationCallback = bool (*)(
    void* context,
    ControlAction action,
    const ControlObjectReference& object,
    const ControlClientIdentity& client,
    const ControlSequenceView& sequence) noexcept;

struct GuardedControlPolicy final {
    std::uint64_t sbo_timeout_ms{10'000U};
    void* authorization_context{};
    ControlAuthorizationCallback authorize{}; // nullptr => fail closed.
};

struct GuardedControlStateView final {
    ControlModel model{ControlModel::unknown};
    bool selected{};
    bool waiting_for_termination{};
    std::uint64_t owner_association_id{};
    std::uint64_t selection_expires_at_ms{};
    std::uint8_t active_control_number{};
};

// Allocation-free safety planner for one controllable Data Object. It models
// the immutable client sequence used by ARIEC61850 and deliberately does not
// perform network I/O. A transport layer executes the returned service action.
class GuardedControlPlanner final {
public:
    GuardedControlPlanner(
        ControlObjectReference object,
        ControlModel model,
        GuardedControlPolicy policy = {}) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] GuardedControlDecision select(
        const ControlClientIdentity& client,
        const ControlSequenceView& sequence,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] GuardedControlDecision select_with_value(
        const ControlClientIdentity& client,
        const ControlSequenceView& sequence,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] GuardedControlDecision operate(
        const ControlClientIdentity& client,
        const ControlSequenceView& sequence,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] GuardedControlDecision cancel(
        const ControlClientIdentity& client,
        std::uint64_t now_ms) noexcept;

    // Enhanced-security completion boundary. MMS Write acceptance alone leaves
    // the planner waiting; only matching CommandTermination completes it.
    [[nodiscard]] GuardedControlDecision command_termination(
        ControlError error,
        AddCause add_cause) noexcept;

    void on_association_closed(std::uint64_t association_id) noexcept;
    void tick(std::uint64_t now_ms) noexcept;
    [[nodiscard]] GuardedControlStateView state(std::uint64_t now_ms) noexcept;

private:
    struct SequenceFingerprint final {
        std::uint64_t value_hash{};
        std::uint64_t origin_hash{};
        std::size_t value_bytes{};
        std::size_t origin_bytes{};
        OriginCategory origin_category{OriginCategory::station_control};
        std::uint8_t control_number{};
        std::uint64_t timestamp_token{};
        std::uint64_t operate_at_token{};
        bool test{};
        bool interlock_check{};
        bool synchro_check{};
    };

    [[nodiscard]] GuardedControlDecision reject(
        ControlAction action,
        GuardedControlStatus status,
        AddCause add_cause) const noexcept;
    [[nodiscard]] bool authorized(
        ControlAction action,
        const ControlClientIdentity& client,
        const ControlSequenceView& sequence) const noexcept;
    [[nodiscard]] SequenceFingerprint fingerprint(
        const ControlSequenceView& sequence,
        std::uint8_t assigned_control_number) const noexcept;
    [[nodiscard]] bool sequence_matches(
        const ControlSequenceView& sequence,
        const SequenceFingerprint& selected) const noexcept;
    [[nodiscard]] std::uint8_t assign_control_number(
        const ControlSequenceView& sequence) noexcept;
    void expire_selection(std::uint64_t now_ms) noexcept;
    void clear_active() noexcept;

    ControlObjectReference object_{};
    ControlModel model_{ControlModel::unknown};
    GuardedControlPolicy policy_{};
    SequenceFingerprint active_sequence_{};
    std::uint64_t owner_association_id_{};
    std::uint64_t selection_expires_at_ms_{};
    std::uint8_t next_control_number_{};
    bool selected_{};
    bool waiting_for_termination_{};
};

[[nodiscard]] constexpr bool model_requires_select(const ControlModel model) noexcept {
    return model == ControlModel::select_before_operate_normal ||
        model == ControlModel::select_before_operate_enhanced;
}

[[nodiscard]] constexpr bool model_is_enhanced(const ControlModel model) noexcept {
    return model == ControlModel::direct_enhanced ||
        model == ControlModel::select_before_operate_enhanced;
}

} // namespace ar::iec61850::control
