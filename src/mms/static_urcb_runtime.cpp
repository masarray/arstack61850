// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_urcb_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::uint8_t kAllowedOptionalFirst = 0x7CU;
constexpr std::uint8_t kAllowedOptionalSecond = 0x80U;
constexpr std::uint8_t kAllowedTriggerOptions = 0x7CU;
constexpr std::uint8_t kTriggerIntegrity = 0x08U;

[[nodiscard]] bool visible_ascii(
    const std::string_view text,
    const std::size_t maximum_bytes) noexcept {
    if (text.empty() || text.size() > maximum_bytes) {
        return false;
    }
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_report_id(const std::string_view report_id) noexcept {
    return visible_ascii(
        report_id,
        MmsStaticUrcbState::maximum_report_id_bytes);
}

[[nodiscard]] bool valid_reference(
    const std::string_view domain,
    const std::string_view item) noexcept {
    return visible_ascii(
               domain,
               MmsStaticUrcbState::maximum_reference_component_bytes) &&
        visible_ascii(
            item,
            MmsStaticUrcbState::maximum_reference_component_bytes) &&
        domain.size() + 1U + item.size() <=
            MmsInformationReportSpanCodec::maximum_reference_bytes;
}

[[nodiscard]] bool valid_optional_fields(
    const std::span<const std::uint8_t> optional_fields) noexcept {
    return optional_fields.size() ==
               MmsInformationReportSpanCodec::optional_field_bytes &&
        (optional_fields[0] & static_cast<std::uint8_t>(~kAllowedOptionalFirst)) == 0U &&
        (optional_fields[1] & static_cast<std::uint8_t>(~kAllowedOptionalSecond)) == 0U;
}

[[nodiscard]] bool valid_trigger_options(const std::uint8_t options) noexcept {
    return (options & static_cast<std::uint8_t>(~kAllowedTriggerOptions)) == 0U;
}

template <std::size_t N>
[[nodiscard]] bool copy_text(
    const std::string_view source,
    std::array<char, N>& destination,
    std::size_t& length) noexcept {
    if (source.empty() || source.size() > destination.size()) {
        length = 0U;
        return false;
    }
    std::fill(destination.begin(), destination.end(), '\0');
    std::copy(source.begin(), source.end(), destination.begin());
    length = source.size();
    return true;
}

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
}

[[nodiscard]] MmsObjectNameView object_name(
    const std::string_view domain,
    const std::string_view item) noexcept {
    return {
        MmsObjectNameViewKind::domain_specific,
        as_bytes(domain),
        as_bytes(item)};
}

[[nodiscard]] bool same_name(
    const MmsStaticUrcbDefinition& left,
    const MmsStaticUrcbDefinition& right) noexcept {
    return left.domain == right.domain && left.item == right.item;
}

void bump_revision(MmsStaticUrcbState& state) noexcept {
    state.revision = state.revision == std::numeric_limits<std::uint32_t>::max()
        ? 1U
        : state.revision + 1U;
}

[[nodiscard]] std::uint64_t saturating_add(
    const std::uint64_t base,
    const std::uint64_t delta) noexcept {
    return delta > std::numeric_limits<std::uint64_t>::max() - base
        ? std::numeric_limits<std::uint64_t>::max()
        : base + delta;
}

[[nodiscard]] std::uint32_t effective_integrity_period(
    const MmsStaticUrcbState& state) noexcept {
    if (state.integrity_period_ms == 0U) {
        return 0U;
    }
    return state.integrity_period_ms < MmsStaticUrcbRuntime::minimum_integrity_period_ms
        ? MmsStaticUrcbRuntime::minimum_integrity_period_ms
        : state.integrity_period_ms;
}

void refresh_integrity_arm(
    MmsStaticUrcbState& state,
    const std::uint64_t now_ms) noexcept {
    const auto period = effective_integrity_period(state);
    const bool selected = (state.trigger_options & kTriggerIntegrity) != 0U;
    state.integrity_armed = state.enabled && selected && period != 0U;
    state.next_integrity_due_ms = state.integrity_armed
        ? saturating_add(now_ms, period)
        : 0U;
}

[[nodiscard]] std::uint8_t next_sequence_number(
    const std::uint8_t current) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(current) + 1U) & 0xFFU);
}

[[nodiscard]] bool plan_matches_state(
    const MmsStaticUrcbEmissionPlan& plan,
    const MmsStaticUrcbState& state) noexcept {
    if (!state.enabled || plan.revision != state.revision ||
        plan.reason == MmsStaticUrcbReportReason::none ||
        plan.sequence_number != next_sequence_number(state.sequence_number)) {
        return false;
    }
    if (plan.reason == MmsStaticUrcbReportReason::general_interrogation) {
        return state.general_interrogation_pending;
    }
    return plan.reason == MmsStaticUrcbReportReason::integrity && state.integrity_armed;
}

[[nodiscard]] MmsStaticUrcbEncodeResult map_report_result(
    const MmsStaticInformationReportEncodeResult& result) noexcept {
    MmsStaticUrcbStatus status = MmsStaticUrcbStatus::report_encode_failed;
    switch (result.status) {
    case MmsStaticInformationReportStatus::response_ready:
        status = MmsStaticUrcbStatus::ok;
        break;
    case MmsStaticInformationReportStatus::data_set_not_found:
        status = MmsStaticUrcbStatus::data_set_not_found;
        break;
    case MmsStaticInformationReportStatus::workspace_too_small:
        status = MmsStaticUrcbStatus::workspace_too_small;
        break;
    case MmsStaticInformationReportStatus::backend_failure:
    case MmsStaticInformationReportStatus::object_not_found:
        status = MmsStaticUrcbStatus::backend_failure;
        break;
    case MmsStaticInformationReportStatus::response_buffer_too_small:
        status = MmsStaticUrcbStatus::response_buffer_too_small;
        break;
    case MmsStaticInformationReportStatus::invalid_object_table:
    case MmsStaticInformationReportStatus::invalid_data_set_table:
    case MmsStaticInformationReportStatus::invalid_report:
        status = MmsStaticUrcbStatus::report_encode_failed;
        break;
    }
    return {status, result.bytes_written, result.required_bytes, result.member_count};
}

} // namespace

bool MmsStaticUrcbRuntime::initialize() noexcept {
    initialized_ = false;
    if (objects_ == nullptr || data_sets_ == nullptr ||
        !objects_->valid() || !data_sets_->valid() ||
        !data_sets_->valid_against(*objects_) ||
        definitions_.size() > maximum_control_blocks ||
        states_.size() < definitions_.size()) {
        return false;
    }

    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        if (!valid_reference(definition.domain, definition.item) ||
            !valid_report_id(definition.report_id) ||
            !valid_reference(definition.data_set_domain, definition.data_set_item) ||
            !valid_optional_fields(definition.optional_fields) ||
            !valid_trigger_options(definition.trigger_options) ||
            data_sets_->find(object_name(
                definition.data_set_domain,
                definition.data_set_item)) == nullptr) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (same_name(definitions_[earlier], definition)) {
                return false;
            }
        }
    }

    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        auto& state_ref = states_[index];
        state_ref = {};
        if (!copy_text(
                definition.report_id,
                state_ref.report_id_storage,
                state_ref.report_id_size) ||
            !copy_text(
                definition.data_set_domain,
                state_ref.data_set_domain_storage,
                state_ref.data_set_domain_size) ||
            !copy_text(
                definition.data_set_item,
                state_ref.data_set_item_storage,
                state_ref.data_set_item_size)) {
            return false;
        }
        state_ref.optional_fields = definition.optional_fields;
        state_ref.conf_revision = definition.conf_revision;
        state_ref.buffer_time_ms = definition.buffer_time_ms;
        state_ref.trigger_options = definition.trigger_options;
        state_ref.integrity_period_ms = definition.integrity_period_ms;
        state_ref.revision = 1U;
    }

    initialized_ = true;
    return true;
}

const MmsStaticUrcbDefinition* MmsStaticUrcbRuntime::definition(
    const std::size_t index) const noexcept {
    return initialized_ && index < definitions_.size() ? &definitions_[index] : nullptr;
}

const MmsStaticUrcbState* MmsStaticUrcbRuntime::state(
    const std::size_t index) const noexcept {
    return initialized_ && index < definitions_.size() ? &states_[index] : nullptr;
}

MmsStaticUrcbState* MmsStaticUrcbRuntime::state(
    const std::size_t index) noexcept {
    return initialized_ && index < definitions_.size() ? &states_[index] : nullptr;
}

bool MmsStaticUrcbRuntime::find_index(
    const MmsObjectNameView& name,
    std::size_t& index) const noexcept {
    index = 0U;
    if (!initialized_ || name.kind != MmsObjectNameViewKind::domain_specific) {
        return false;
    }
    for (std::size_t current = 0U; current < definitions_.size(); ++current) {
        const auto& definition_ref = definitions_[current];
        if (name.domain.size() != definition_ref.domain.size() ||
            name.item.size() != definition_ref.item.size()) {
            continue;
        }
        if (std::equal(
                name.domain.begin(),
                name.domain.end(),
                reinterpret_cast<const std::uint8_t*>(definition_ref.domain.data())) &&
            std::equal(
                name.item.begin(),
                name.item.end(),
                reinterpret_cast<const std::uint8_t*>(definition_ref.item.data()))) {
            index = current;
            return true;
        }
    }
    return false;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_enabled(
    const std::size_t index,
    const bool enabled,
    const std::uint64_t now_ms) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled == enabled) {
        return MmsStaticUrcbStatus::ok;
    }

    state_ref->enabled = enabled;
    state_ref->general_interrogation_pending = false;
    if (enabled) {
        state_ref->sequence_number = 0U;
        refresh_integrity_arm(*state_ref, now_ms);
    } else {
        state_ref->integrity_armed = false;
        state_ref->next_integrity_due_ms = 0U;
    }
    bump_revision(*state_ref);
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_reserved(
    const std::size_t index,
    const bool reserved) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->reserved != reserved) {
        state_ref->reserved = reserved;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_report_id(
    const std::size_t index,
    const std::string_view report_id) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (!valid_report_id(report_id)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    if (state_ref->report_id() == report_id) {
        return MmsStaticUrcbStatus::ok;
    }
    if (!copy_text(
            report_id,
            state_ref->report_id_storage,
            state_ref->report_id_size)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    bump_revision(*state_ref);
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_data_set(
    const std::size_t index,
    const std::string_view domain,
    const std::string_view item) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (!valid_reference(domain, item)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    if (data_sets_->find(object_name(domain, item)) == nullptr) {
        return MmsStaticUrcbStatus::data_set_not_found;
    }
    if (state_ref->data_set_domain() == domain && state_ref->data_set_item() == item) {
        return MmsStaticUrcbStatus::ok;
    }
    if (!copy_text(
            domain,
            state_ref->data_set_domain_storage,
            state_ref->data_set_domain_size) ||
        !copy_text(
            item,
            state_ref->data_set_item_storage,
            state_ref->data_set_item_size)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    bump_revision(*state_ref);
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_optional_fields(
    const std::size_t index,
    const std::span<const std::uint8_t> optional_fields) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (!valid_optional_fields(optional_fields)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    const std::array<std::uint8_t, MmsInformationReportSpanCodec::optional_field_bytes>
        next{optional_fields[0], optional_fields[1]};
    if (state_ref->optional_fields != next) {
        state_ref->optional_fields = next;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_buffer_time_ms(
    const std::size_t index,
    const std::uint32_t buffer_time_ms) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (state_ref->buffer_time_ms != buffer_time_ms) {
        state_ref->buffer_time_ms = buffer_time_ms;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_trigger_options(
    const std::size_t index,
    const std::uint8_t trigger_options) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (!valid_trigger_options(trigger_options)) {
        return MmsStaticUrcbStatus::invalid_value;
    }
    if (state_ref->trigger_options != trigger_options) {
        state_ref->trigger_options = trigger_options;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::set_integrity_period_ms(
    const std::size_t index,
    const std::uint32_t integrity_period_ms) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (state_ref->enabled) {
        return MmsStaticUrcbStatus::object_access_denied;
    }
    if (state_ref->integrity_period_ms != integrity_period_ms) {
        state_ref->integrity_period_ms = integrity_period_ms;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::request_general_interrogation(
    const std::size_t index) noexcept {
    auto* state_ref = state(index);
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (state_ref == nullptr) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    if (!state_ref->enabled) {
        return MmsStaticUrcbStatus::temporarily_unavailable;
    }
    if (!state_ref->general_interrogation_pending) {
        state_ref->general_interrogation_pending = true;
        bump_revision(*state_ref);
    }
    return MmsStaticUrcbStatus::ok;
}

bool MmsStaticUrcbRuntime::next_due(
    const std::uint64_t now_ms,
    MmsStaticUrcbEmissionPlan& plan) const noexcept {
    plan = {};
    if (!initialized_) {
        return false;
    }

    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const auto& state_ref = states_[index];
        if (state_ref.enabled && state_ref.general_interrogation_pending) {
            plan.index = index;
            plan.revision = state_ref.revision;
            plan.sequence_number = next_sequence_number(state_ref.sequence_number);
            plan.reason = MmsStaticUrcbReportReason::general_interrogation;
            return true;
        }
    }
    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const auto& state_ref = states_[index];
        if (state_ref.enabled && state_ref.integrity_armed &&
            now_ms >= state_ref.next_integrity_due_ms) {
            plan.index = index;
            plan.revision = state_ref.revision;
            plan.sequence_number = next_sequence_number(state_ref.sequence_number);
            plan.reason = MmsStaticUrcbReportReason::integrity;
            return true;
        }
    }
    return false;
}

MmsStaticUrcbEncodeResult MmsStaticUrcbRuntime::encode(
    const MmsStaticUrcbEmissionPlan& plan,
    const std::span<const std::uint8_t> report_time,
    const std::span<std::uint8_t> destination,
    const std::span<std::uint8_t> workspace) const noexcept {
    if (!initialized_ || objects_ == nullptr || data_sets_ == nullptr) {
        return {MmsStaticUrcbStatus::invalid_runtime, 0U, 0U, 0U};
    }
    if (plan.index >= definitions_.size()) {
        return {MmsStaticUrcbStatus::index_out_of_range, 0U, 0U, 0U};
    }
    const auto& state_ref = states_[plan.index];
    if (!plan_matches_state(plan, state_ref)) {
        return {MmsStaticUrcbStatus::stale_plan, 0U, 0U, 0U};
    }

    const auto data_set_name = object_name(
        state_ref.data_set_domain(),
        state_ref.data_set_item());
    MmsStaticInformationReportInput report;
    report.report_id = state_ref.report_id();
    report.optional_fields = state_ref.optional_fields;
    report.sequence_number = plan.sequence_number;
    report.report_time = report_time;
    report.buffered = false;
    report.buffer_overflow = false;
    report.entry_id = {};
    report.conf_revision = state_ref.conf_revision;
    report.reason_for_inclusion = static_cast<std::uint8_t>(plan.reason);

    return map_report_result(
        MmsStaticInformationReportEncoder::encode_data_set_snapshot_into(
            *objects_,
            *data_sets_,
            data_set_name,
            report,
            destination,
            workspace));
}

MmsStaticUrcbStatus MmsStaticUrcbRuntime::commit(
    const MmsStaticUrcbEmissionPlan& plan,
    const std::uint64_t now_ms) noexcept {
    if (!initialized_) {
        return MmsStaticUrcbStatus::invalid_runtime;
    }
    if (plan.index >= definitions_.size()) {
        return MmsStaticUrcbStatus::index_out_of_range;
    }
    auto& state_ref = states_[plan.index];
    if (!plan_matches_state(plan, state_ref)) {
        return MmsStaticUrcbStatus::stale_plan;
    }

    state_ref.sequence_number = plan.sequence_number;
    if (plan.reason == MmsStaticUrcbReportReason::general_interrogation) {
        state_ref.general_interrogation_pending = false;
    } else if (plan.reason == MmsStaticUrcbReportReason::integrity) {
        const auto period = effective_integrity_period(state_ref);
        state_ref.next_integrity_due_ms = period == 0U
            ? 0U
            : saturating_add(now_ms, period);
        state_ref.integrity_armed = period != 0U &&
            (state_ref.trigger_options & kTriggerIntegrity) != 0U && state_ref.enabled;
    } else {
        return MmsStaticUrcbStatus::stale_plan;
    }
    bump_revision(state_ref);
    return MmsStaticUrcbStatus::ok;
}

} // namespace ar::iec61850::mms
