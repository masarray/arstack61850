// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_urcb_event_runtime.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::uint8_t kTriggerDataChange = 0x40U;
constexpr std::uint8_t kTriggerQualityChange = 0x20U;
constexpr std::uint8_t kTriggerDataUpdate = 0x10U;

constexpr std::uint8_t kReasonDataChange = 0x80U;
constexpr std::uint8_t kReasonQualityChange = 0x40U;
constexpr std::uint8_t kReasonDataUpdate = 0x20U;
constexpr std::uint8_t kOptReasonForInclusion = 0x10U;

[[nodiscard]] std::uint64_t saturating_add(
    const std::uint64_t base,
    const std::uint64_t delta) noexcept {
    return delta > std::numeric_limits<std::uint64_t>::max() - base
        ? std::numeric_limits<std::uint64_t>::max()
        : base + delta;
}

void bump_revision(std::uint32_t& revision) noexcept {
    revision = revision == std::numeric_limits<std::uint32_t>::max()
        ? 1U
        : revision + 1U;
}

[[nodiscard]] std::uint8_t next_sequence_number(
    const std::uint8_t current) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(current) + 1U) & 0xFFU);
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

void clear_pending(MmsStaticUrcbEventState& state) noexcept {
    std::fill(state.member_reason_masks.begin(), state.member_reason_masks.end(), 0U);
    state.due_ms = 0U;
    state.source_revision = 0U;
    state.pending_member_count = 0U;
    state.pending = false;
}

[[nodiscard]] bool event_mapping(
    const MmsStaticUrcbEventReason reason,
    std::uint8_t& trigger_mask,
    std::uint8_t& report_reason) noexcept {
    switch (reason) {
    case MmsStaticUrcbEventReason::data_change:
        trigger_mask = kTriggerDataChange;
        report_reason = kReasonDataChange;
        return true;
    case MmsStaticUrcbEventReason::quality_change:
        trigger_mask = kTriggerQualityChange;
        report_reason = kReasonQualityChange;
        return true;
    case MmsStaticUrcbEventReason::data_update:
        trigger_mask = kTriggerDataUpdate;
        report_reason = kReasonDataUpdate;
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_mms_data(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView data;
    if (!asn1::BerSpanReader::try_read_exact(encoded, data) ||
        data.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (data.tag_number == 1 || data.tag_number == 2) {
        return data.constructed;
    }
    return data.tag_number >= 3 && data.tag_number <= 17 && !data.constructed;
}

[[nodiscard]] bool plan_matches(
    const MmsStaticUrcbEventPlan& plan,
    const MmsStaticUrcbState& control,
    const MmsStaticUrcbEventState& event) noexcept {
    return control.enabled && event.pending &&
        event.source_revision == control.revision &&
        plan.source_revision == control.revision &&
        plan.event_revision == event.revision &&
        plan.sequence_number == next_sequence_number(control.sequence_number);
}

} // namespace

bool MmsStaticUrcbEventRuntime::initialize() noexcept {
    initialized_ = false;
    if (urcb_ == nullptr || objects_ == nullptr || data_sets_ == nullptr ||
        !urcb_->valid() || !objects_->valid() || !data_sets_->valid() ||
        !data_sets_->valid_against(*objects_) || states_.size() < urcb_->size()) {
        return false;
    }
    for (std::size_t index = 0U; index < urcb_->size(); ++index) {
        states_[index] = {};
        states_[index].revision = 1U;
    }
    initialized_ = true;
    return true;
}

const MmsStaticUrcbEventState* MmsStaticUrcbEventRuntime::state(
    const std::size_t index) const noexcept {
    return initialized_ && urcb_ != nullptr && index < urcb_->size()
        ? &states_[index]
        : nullptr;
}

MmsStaticUrcbEventStatus MmsStaticUrcbEventRuntime::notify(
    const std::size_t control_block_index,
    const std::size_t data_set_member_index,
    const MmsStaticUrcbEventReason reason,
    const std::uint64_t now_ms) noexcept {
    if (!initialized_ || urcb_ == nullptr || data_sets_ == nullptr) {
        return MmsStaticUrcbEventStatus::invalid_runtime;
    }
    auto* control = urcb_->state(control_block_index);
    if (control == nullptr || control_block_index >= states_.size()) {
        return MmsStaticUrcbEventStatus::index_out_of_range;
    }
    if (!control->enabled) {
        return MmsStaticUrcbEventStatus::temporarily_unavailable;
    }

    std::uint8_t trigger_mask = 0U;
    std::uint8_t report_reason = 0U;
    if (!event_mapping(reason, trigger_mask, report_reason)) {
        return MmsStaticUrcbEventStatus::trigger_not_selected;
    }
    if ((control->trigger_options & trigger_mask) == 0U) {
        return MmsStaticUrcbEventStatus::trigger_not_selected;
    }

    const auto* data_set = data_sets_->find(object_name(
        control->data_set_domain(), control->data_set_item()));
    if (data_set == nullptr) {
        return MmsStaticUrcbEventStatus::data_set_not_found;
    }
    if (data_set_member_index >= data_set->members.size() ||
        data_set_member_index >= MmsInformationReportSpanCodec::maximum_members) {
        return MmsStaticUrcbEventStatus::member_out_of_range;
    }

    auto& event = states_[control_block_index];
    if (event.pending && event.source_revision != control->revision) {
        clear_pending(event);
        bump_revision(event.revision);
    }
    if (!event.pending) {
        event.pending = true;
        event.source_revision = control->revision;
        event.due_ms = saturating_add(now_ms, control->buffer_time_ms);
    }
    if (event.member_reason_masks[data_set_member_index] == 0U) {
        ++event.pending_member_count;
    }
    event.member_reason_masks[data_set_member_index] = static_cast<std::uint8_t>(
        event.member_reason_masks[data_set_member_index] | report_reason);
    bump_revision(event.revision);
    return MmsStaticUrcbEventStatus::ok;
}

bool MmsStaticUrcbEventRuntime::next_due(
    const std::uint64_t now_ms,
    MmsStaticUrcbEventPlan& plan) const noexcept {
    plan = {};
    if (!initialized_ || urcb_ == nullptr) {
        return false;
    }
    for (std::size_t index = 0U; index < urcb_->size(); ++index) {
        const auto* control = urcb_->state(index);
        const auto& event = states_[index];
        if (control == nullptr || !control->enabled || !event.pending ||
            event.pending_member_count == 0U ||
            event.source_revision != control->revision || now_ms < event.due_ms) {
            continue;
        }
        plan.index = index;
        plan.source_revision = control->revision;
        plan.event_revision = event.revision;
        plan.sequence_number = next_sequence_number(control->sequence_number);
        return true;
    }
    return false;
}

MmsStaticUrcbEventEncodeResult MmsStaticUrcbEventRuntime::encode(
    const MmsStaticUrcbEventPlan& plan,
    const std::span<const std::uint8_t> report_time,
    const std::span<std::uint8_t> destination,
    const std::span<std::uint8_t> workspace) const noexcept {
    if (!initialized_ || urcb_ == nullptr || objects_ == nullptr || data_sets_ == nullptr) {
        return {MmsStaticUrcbEventStatus::invalid_runtime, 0U, 0U, 0U, 0U};
    }
    if (plan.index >= urcb_->size() || plan.index >= states_.size()) {
        return {MmsStaticUrcbEventStatus::index_out_of_range, 0U, 0U, 0U, 0U};
    }
    const auto* control = urcb_->state(plan.index);
    const auto& event = states_[plan.index];
    if (control == nullptr || !plan_matches(plan, *control, event)) {
        return {MmsStaticUrcbEventStatus::stale_plan, 0U, 0U, 0U, 0U};
    }

    const auto* data_set = data_sets_->find(object_name(
        control->data_set_domain(), control->data_set_item()));
    if (data_set == nullptr) {
        return {MmsStaticUrcbEventStatus::data_set_not_found, 0U, 0U, 0U, 0U};
    }
    if (data_set->members.size() > MmsInformationReportSpanCodec::maximum_members) {
        return {
            MmsStaticUrcbEventStatus::report_encode_failed,
            0U,
            0U,
            data_set->members.size(),
            0U};
    }

    std::array<std::size_t, MmsInformationReportSpanCodec::maximum_members> indices{};
    std::array<MmsInformationReportReferenceInput,
        MmsInformationReportSpanCodec::maximum_members> references{};
    std::array<MmsReadAccessResultInput,
        MmsInformationReportSpanCodec::maximum_members> results{};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::maximum_members> reasons{};

    std::size_t included = 0U;
    std::size_t workspace_offset = 0U;
    for (std::size_t member_index = 0U;
         member_index < data_set->members.size();
         ++member_index) {
        const auto reason = event.member_reason_masks[member_index];
        if (reason == 0U) {
            continue;
        }
        const auto& member = data_set->members[member_index];
        const auto* object = objects_->find(object_name(member.domain, member.item));
        if (object == nullptr) {
            return {
                MmsStaticUrcbEventStatus::object_not_found,
                0U,
                0U,
                data_set->members.size(),
                included};
        }
        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->read(object->context, remaining);
        if (read.status == wire::EncodeStatus::buffer_too_small) {
            if (read.required_bytes >
                std::numeric_limits<std::size_t>::max() - workspace_offset) {
                return {
                    MmsStaticUrcbEventStatus::backend_failure,
                    0U,
                    0U,
                    data_set->members.size(),
                    included};
            }
            return {
                MmsStaticUrcbEventStatus::workspace_too_small,
                0U,
                workspace_offset + read.required_bytes,
                data_set->members.size(),
                included};
        }
        if (!read.success() || read.bytes_written > remaining.size() ||
            !valid_mms_data(remaining.first(read.bytes_written))) {
            return {
                MmsStaticUrcbEventStatus::backend_failure,
                0U,
                0U,
                data_set->members.size(),
                included};
        }

        indices[included] = member_index;
        references[included] = {member.domain, member.item};
        results[included] = {
            true,
            remaining.first(read.bytes_written),
            0U};
        reasons[included] = reason;
        workspace_offset += read.bytes_written;
        ++included;
    }
    if (included == 0U || included != event.pending_member_count) {
        return {
            MmsStaticUrcbEventStatus::stale_plan,
            0U,
            0U,
            data_set->members.size(),
            included};
    }

    MmsSelectiveInformationReportSnapshotInput report;
    report.report_id = control->report_id();
    report.optional_fields = control->optional_fields;
    report.sequence_number = plan.sequence_number;
    report.report_time = report_time;
    report.data_set_reference = {
        control->data_set_domain(), control->data_set_item()};
    report.conf_revision = control->conf_revision;
    report.data_set_member_count = data_set->members.size();
    report.included_member_indices =
        std::span<const std::size_t>{indices}.first(included);
    report.included_member_references =
        std::span<const MmsInformationReportReferenceInput>{references}.first(included);
    report.included_member_results =
        std::span<const MmsReadAccessResultInput>{results}.first(included);
    if (control->optional_fields.size() > 0U &&
        (control->optional_fields[0] & kOptReasonForInclusion) != 0U) {
        report.included_reason_for_inclusion =
            std::span<const std::uint8_t>{reasons}.first(included);
    }

    const auto encoded = MmsSelectiveInformationReportSpanCodec::encode_snapshot_into(
        report, destination);
    if (encoded.success()) {
        return {
            MmsStaticUrcbEventStatus::ok,
            encoded.bytes_written,
            encoded.required_bytes,
            data_set->members.size(),
            included};
    }
    if (encoded.status == wire::EncodeStatus::buffer_too_small) {
        return {
            MmsStaticUrcbEventStatus::response_buffer_too_small,
            0U,
            encoded.required_bytes,
            data_set->members.size(),
            included};
    }
    return {
        MmsStaticUrcbEventStatus::report_encode_failed,
        0U,
        encoded.required_bytes,
        data_set->members.size(),
        included};
}

MmsStaticUrcbEventStatus MmsStaticUrcbEventRuntime::commit(
    const MmsStaticUrcbEventPlan& plan) noexcept {
    if (!initialized_ || urcb_ == nullptr) {
        return MmsStaticUrcbEventStatus::invalid_runtime;
    }
    if (plan.index >= urcb_->size() || plan.index >= states_.size()) {
        return MmsStaticUrcbEventStatus::index_out_of_range;
    }
    auto* control = urcb_->state(plan.index);
    auto& event = states_[plan.index];
    if (control == nullptr || !plan_matches(plan, *control, event)) {
        return MmsStaticUrcbEventStatus::stale_plan;
    }

    control->sequence_number = plan.sequence_number;
    bump_revision(control->revision);
    clear_pending(event);
    bump_revision(event.revision);
    return MmsStaticUrcbEventStatus::ok;
}

} // namespace ar::iec61850::mms
