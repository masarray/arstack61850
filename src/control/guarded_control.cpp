// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/guarded_control.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace ar::iec61850::control {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] bool ascii_equal(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool service_leaf(const std::string_view segment) noexcept {
    constexpr std::array<std::string_view, 15U> leaves{
        "ctlModel", "ctlVal", "ctlNum", "stSeld", "Oper", "SBO", "SBOw",
        "Cancel", "origin", "orCat", "orIdent", "T", "Test", "Check", "operTm"};
    return std::any_of(leaves.begin(), leaves.end(), [&](const auto leaf) {
        return ascii_equal(segment, leaf);
    });
}

[[nodiscard]] std::string_view trim(const std::string_view input) noexcept {
    std::size_t begin = 0U;
    while (begin < input.size() &&
           (input[begin] == ' ' || input[begin] == '\t' ||
            input[begin] == '\r' || input[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = input.size();
    while (end > begin &&
           (input[end - 1U] == ' ' || input[end - 1U] == '\t' ||
            input[end - 1U] == '\r' || input[end - 1U] == '\n')) {
        --end;
    }
    return input.substr(begin, end - begin);
}

[[nodiscard]] bool path_separator(const char value) noexcept {
    return value == '.' || value == '$';
}

[[nodiscard]] std::uint64_t hash_bytes(
    const std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

bool try_parse_control_object_reference(
    const std::string_view reference,
    ControlObjectReference& output) noexcept {
    output = {};
    const auto normalized = trim(reference);
    const auto slash = normalized.find('/');
    if (slash == std::string_view::npos || slash == 0U ||
        slash + 1U >= normalized.size() ||
        normalized.find('/', slash + 1U) != std::string_view::npos) {
        return false;
    }

    const auto domain = trim(normalized.substr(0U, slash));
    const auto path = trim(normalized.substr(slash + 1U));
    if (domain.empty() || path.empty()) {
        return false;
    }

    std::size_t separator = std::string_view::npos;
    for (std::size_t index = 0U; index < path.size(); ++index) {
        if (path_separator(path[index])) {
            separator = index;
            break;
        }
    }
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U >= path.size()) {
        return false;
    }

    const auto logical_node = path.substr(0U, separator);
    const auto data_object_path = path.substr(separator + 1U);
    if (logical_node.empty() || data_object_path.empty()) {
        return false;
    }

    std::size_t segment_begin = 0U;
    while (segment_begin < data_object_path.size()) {
        auto segment_end = segment_begin;
        while (segment_end < data_object_path.size() &&
               !path_separator(data_object_path[segment_end])) {
            if (data_object_path[segment_end] == '/') {
                return false;
            }
            ++segment_end;
        }
        if (segment_end == segment_begin ||
            service_leaf(data_object_path.substr(segment_begin, segment_end - segment_begin))) {
            return false;
        }
        if (segment_end == data_object_path.size()) {
            break;
        }
        segment_begin = segment_end + 1U;
        if (segment_begin == data_object_path.size()) {
            return false;
        }
    }

    output = {domain, logical_node, data_object_path};
    return output.valid();
}

bool build_control_item(
    const ControlObjectReference& object,
    const std::string_view functional_constraint,
    const std::string_view service_leaf_name,
    const std::span<char> destination,
    std::size_t& bytes_written) noexcept {
    bytes_written = 0U;
    if (!object.valid() || functional_constraint.empty() || service_leaf_name.empty() ||
        functional_constraint.find('$') != std::string_view::npos ||
        service_leaf_name.find('$') != std::string_view::npos ||
        service_leaf_name.find('.') != std::string_view::npos) {
        return false;
    }

    const auto required = object.logical_node.size() + 1U +
        functional_constraint.size() + 1U + object.data_object_path.size() +
        1U + service_leaf_name.size();
    if (destination.size() < required) {
        bytes_written = required;
        return false;
    }

    std::size_t offset = 0U;
    const auto append = [&](const std::string_view text) {
        for (const auto character : text) {
            destination[offset++] = path_separator(character) ? '$' : character;
        }
    };
    append(object.logical_node);
    destination[offset++] = '$';
    append(functional_constraint);
    destination[offset++] = '$';
    append(object.data_object_path);
    destination[offset++] = '$';
    append(service_leaf_name);
    bytes_written = offset;
    return true;
}

GuardedControlPlanner::GuardedControlPlanner(
    const ControlObjectReference object,
    const ControlModel model,
    const GuardedControlPolicy policy) noexcept
    : object_{object}, model_{model}, policy_{policy} {}

bool GuardedControlPlanner::valid() const noexcept {
    return object_.valid() &&
        model_ != ControlModel::unknown &&
        static_cast<std::uint8_t>(model_) <=
            static_cast<std::uint8_t>(ControlModel::select_before_operate_enhanced);
}

GuardedControlDecision GuardedControlPlanner::reject(
    const ControlAction action,
    const GuardedControlStatus status,
    const AddCause add_cause) const noexcept {
    return {status, action, active_sequence_.control_number, ControlError::no_error, add_cause};
}

bool GuardedControlPlanner::authorized(
    const ControlAction action,
    const ControlClientIdentity& client,
    const ControlSequenceView& sequence) const noexcept {
    return policy_.authorize != nullptr &&
        policy_.authorize(policy_.authorization_context, action, object_, client, sequence);
}

GuardedControlPlanner::SequenceFingerprint GuardedControlPlanner::fingerprint(
    const ControlSequenceView& sequence,
    const std::uint8_t assigned_control_number) const noexcept {
    SequenceFingerprint result;
    result.value_hash = hash_bytes(sequence.control_value);
    result.origin_hash = hash_bytes(sequence.origin_identifier);
    result.value_bytes = sequence.control_value.size();
    result.origin_bytes = sequence.origin_identifier.size();
    result.origin_category = sequence.origin_category;
    result.control_number = assigned_control_number;
    result.timestamp_token = sequence.timestamp_token;
    result.operate_at_token = sequence.operate_at_token;
    result.test = sequence.test;
    result.interlock_check = sequence.interlock_check;
    result.synchro_check = sequence.synchro_check;
    return result;
}

bool GuardedControlPlanner::sequence_matches(
    const ControlSequenceView& sequence,
    const SequenceFingerprint& selected) const noexcept {
    const auto candidate = fingerprint(
        sequence,
        sequence.control_number == 0U ? selected.control_number : sequence.control_number);
    return candidate.value_hash == selected.value_hash &&
        candidate.origin_hash == selected.origin_hash &&
        candidate.value_bytes == selected.value_bytes &&
        candidate.origin_bytes == selected.origin_bytes &&
        candidate.origin_category == selected.origin_category &&
        candidate.control_number == selected.control_number &&
        candidate.timestamp_token == selected.timestamp_token &&
        candidate.operate_at_token == selected.operate_at_token &&
        candidate.test == selected.test &&
        candidate.interlock_check == selected.interlock_check &&
        candidate.synchro_check == selected.synchro_check;
}

std::uint8_t GuardedControlPlanner::assign_control_number(
    const ControlSequenceView& sequence) noexcept {
    if (sequence.control_number != 0U) {
        return sequence.control_number;
    }
    next_control_number_ = next_control_number_ >= 255U
        ? 1U
        : static_cast<std::uint8_t>(next_control_number_ + 1U);
    return next_control_number_;
}

void GuardedControlPlanner::clear_active() noexcept {
    active_sequence_ = {};
    owner_association_id_ = 0U;
    selection_expires_at_ms_ = 0U;
    selected_ = false;
    waiting_for_termination_ = false;
}

void GuardedControlPlanner::expire_selection(const std::uint64_t now_ms) noexcept {
    if (selected_ && selection_expires_at_ms_ != 0U && now_ms >= selection_expires_at_ms_) {
        clear_active();
    }
}

GuardedControlDecision GuardedControlPlanner::select(
    const ControlClientIdentity& client,
    const ControlSequenceView& sequence,
    const std::uint64_t now_ms) noexcept {
    if (model_ != ControlModel::select_before_operate_normal) {
        return reject(ControlAction::select, GuardedControlStatus::unsupported, AddCause::not_supported);
    }
    if (!client.valid()) {
        return reject(ControlAction::select, GuardedControlStatus::invalid_identity, AddCause::no_access_authority);
    }
    if (!sequence.valid()) {
        return reject(ControlAction::select, GuardedControlStatus::invalid_sequence, AddCause::inconsistent_parameters);
    }
    expire_selection(now_ms);
    if (waiting_for_termination_) {
        return reject(ControlAction::select, GuardedControlStatus::command_already_in_execution,
                      AddCause::command_already_in_execution);
    }
    if (selected_) {
        return reject(ControlAction::select,
            owner_association_id_ == client.association_id
                ? GuardedControlStatus::object_already_selected
                : GuardedControlStatus::locked_by_other_client,
            owner_association_id_ == client.association_id
                ? AddCause::object_already_selected
                : AddCause::locked_by_other_client);
    }
    if (!authorized(ControlAction::select, client, sequence)) {
        return reject(ControlAction::select, GuardedControlStatus::access_denied,
                      AddCause::no_access_authority);
    }

    const auto control_number = assign_control_number(sequence);
    active_sequence_ = fingerprint(sequence, control_number);
    owner_association_id_ = client.association_id;
    selection_expires_at_ms_ = policy_.sbo_timeout_ms == 0U
        ? 0U
        : (now_ms > std::numeric_limits<std::uint64_t>::max() - policy_.sbo_timeout_ms
            ? std::numeric_limits<std::uint64_t>::max()
            : now_ms + policy_.sbo_timeout_ms);
    selected_ = true;
    return {GuardedControlStatus::ok, ControlAction::select, control_number,
            ControlError::no_error, AddCause::none};
}

GuardedControlDecision GuardedControlPlanner::select_with_value(
    const ControlClientIdentity& client,
    const ControlSequenceView& sequence,
    const std::uint64_t now_ms) noexcept {
    if (model_ != ControlModel::select_before_operate_enhanced) {
        return reject(ControlAction::select_with_value, GuardedControlStatus::unsupported,
                      AddCause::not_supported);
    }
    if (!client.valid()) {
        return reject(ControlAction::select_with_value, GuardedControlStatus::invalid_identity,
                      AddCause::no_access_authority);
    }
    if (!sequence.valid()) {
        return reject(ControlAction::select_with_value, GuardedControlStatus::invalid_sequence,
                      AddCause::inconsistent_parameters);
    }
    expire_selection(now_ms);
    if (waiting_for_termination_) {
        return reject(ControlAction::select_with_value,
                      GuardedControlStatus::command_already_in_execution,
                      AddCause::command_already_in_execution);
    }
    if (selected_) {
        return reject(ControlAction::select_with_value,
            owner_association_id_ == client.association_id
                ? GuardedControlStatus::object_already_selected
                : GuardedControlStatus::locked_by_other_client,
            owner_association_id_ == client.association_id
                ? AddCause::object_already_selected
                : AddCause::locked_by_other_client);
    }
    if (!authorized(ControlAction::select_with_value, client, sequence)) {
        return reject(ControlAction::select_with_value, GuardedControlStatus::access_denied,
                      AddCause::no_access_authority);
    }

    const auto control_number = assign_control_number(sequence);
    active_sequence_ = fingerprint(sequence, control_number);
    owner_association_id_ = client.association_id;
    selection_expires_at_ms_ = policy_.sbo_timeout_ms == 0U
        ? 0U
        : (now_ms > std::numeric_limits<std::uint64_t>::max() - policy_.sbo_timeout_ms
            ? std::numeric_limits<std::uint64_t>::max()
            : now_ms + policy_.sbo_timeout_ms);
    selected_ = true;
    return {GuardedControlStatus::ok, ControlAction::select_with_value, control_number,
            ControlError::no_error, AddCause::none};
}

GuardedControlDecision GuardedControlPlanner::operate(
    const ControlClientIdentity& client,
    const ControlSequenceView& sequence,
    const std::uint64_t now_ms) noexcept {
    if (!valid() || model_ == ControlModel::status_only) {
        return reject(ControlAction::operate, GuardedControlStatus::unsupported,
                      AddCause::not_supported);
    }
    if (!client.valid()) {
        return reject(ControlAction::operate, GuardedControlStatus::invalid_identity,
                      AddCause::no_access_authority);
    }
    if (!sequence.valid()) {
        return reject(ControlAction::operate, GuardedControlStatus::invalid_sequence,
                      AddCause::inconsistent_parameters);
    }
    if (waiting_for_termination_) {
        return reject(ControlAction::operate, GuardedControlStatus::command_already_in_execution,
                      AddCause::command_already_in_execution);
    }

    if (model_requires_select(model_)) {
        if (selected_ && selection_expires_at_ms_ != 0U && now_ms >= selection_expires_at_ms_) {
            const auto control_number = active_sequence_.control_number;
            clear_active();
            return {GuardedControlStatus::selection_expired, ControlAction::operate,
                    control_number, ControlError::no_error, AddCause::time_limit_over};
        }
        if (!selected_) {
            return reject(ControlAction::operate, GuardedControlStatus::object_not_selected,
                          AddCause::object_not_selected);
        }
        if (owner_association_id_ != client.association_id) {
            return reject(ControlAction::operate, GuardedControlStatus::locked_by_other_client,
                          AddCause::locked_by_other_client);
        }
        if (!sequence_matches(sequence, active_sequence_)) {
            const auto control_number = active_sequence_.control_number;
            clear_active();
            return {GuardedControlStatus::sequence_mismatch, ControlAction::operate,
                    control_number, ControlError::no_error, AddCause::inconsistent_parameters};
        }
    }

    if (!authorized(ControlAction::operate, client, sequence)) {
        const auto control_number = active_sequence_.control_number;
        if (model_requires_select(model_)) {
            clear_active();
        }
        return {GuardedControlStatus::access_denied, ControlAction::operate,
                control_number, ControlError::no_error, AddCause::no_access_authority};
    }

    std::uint8_t control_number{};
    if (model_requires_select(model_)) {
        control_number = active_sequence_.control_number;
        selected_ = false;
        selection_expires_at_ms_ = 0U;
    } else {
        control_number = assign_control_number(sequence);
        active_sequence_ = fingerprint(sequence, control_number);
        owner_association_id_ = client.association_id;
    }

    if (model_is_enhanced(model_)) {
        waiting_for_termination_ = true;
        return {GuardedControlStatus::accepted_waiting_termination, ControlAction::operate,
                control_number, ControlError::no_error, AddCause::none};
    }

    const GuardedControlDecision result{
        GuardedControlStatus::ok, ControlAction::operate, control_number,
        ControlError::no_error, AddCause::none};
    clear_active();
    return result;
}

GuardedControlDecision GuardedControlPlanner::cancel(
    const ControlClientIdentity& client,
    const std::uint64_t now_ms) noexcept {
    if (!model_requires_select(model_)) {
        return reject(ControlAction::cancel, GuardedControlStatus::unsupported,
                      AddCause::not_supported);
    }
    if (!client.valid()) {
        return reject(ControlAction::cancel, GuardedControlStatus::invalid_identity,
                      AddCause::no_access_authority);
    }
    if (selected_ && selection_expires_at_ms_ != 0U && now_ms >= selection_expires_at_ms_) {
        const auto control_number = active_sequence_.control_number;
        clear_active();
        return {GuardedControlStatus::selection_expired, ControlAction::cancel,
                control_number, ControlError::no_error, AddCause::time_limit_over};
    }
    if (!selected_) {
        return reject(ControlAction::cancel, GuardedControlStatus::no_active_selection,
                      AddCause::object_not_selected);
    }
    if (owner_association_id_ != client.association_id) {
        return reject(ControlAction::cancel, GuardedControlStatus::locked_by_other_client,
                      AddCause::locked_by_other_client);
    }

    const ControlSequenceView authorization_sequence{};
    if (!authorized(ControlAction::cancel, client, authorization_sequence)) {
        return reject(ControlAction::cancel, GuardedControlStatus::access_denied,
                      AddCause::no_access_authority);
    }

    const auto control_number = active_sequence_.control_number;
    clear_active();
    return {GuardedControlStatus::ok, ControlAction::cancel, control_number,
            ControlError::no_error, AddCause::abortion_by_cancel};
}

GuardedControlDecision GuardedControlPlanner::command_termination(
    const ControlError error,
    const AddCause add_cause) noexcept {
    if (!waiting_for_termination_) {
        return reject(ControlAction::operate, GuardedControlStatus::no_pending_termination,
                      AddCause::unknown);
    }
    const auto control_number = active_sequence_.control_number;
    const bool positive = error == ControlError::no_error &&
        (add_cause == AddCause::unknown || add_cause == AddCause::none);
    const GuardedControlDecision result{
        positive ? GuardedControlStatus::positive_termination
                 : GuardedControlStatus::negative_termination,
        ControlAction::operate,
        control_number,
        error,
        add_cause};
    clear_active();
    return result;
}

void GuardedControlPlanner::on_association_closed(
    const std::uint64_t association_id) noexcept {
    if (association_id != 0U && owner_association_id_ == association_id) {
        clear_active();
    }
}

void GuardedControlPlanner::tick(const std::uint64_t now_ms) noexcept {
    expire_selection(now_ms);
}

GuardedControlStateView GuardedControlPlanner::state(const std::uint64_t now_ms) noexcept {
    expire_selection(now_ms);
    return {model_, selected_, waiting_for_termination_, owner_association_id_,
            selection_expires_at_ms_, active_sequence_.control_number};
}

} // namespace ar::iec61850::control
