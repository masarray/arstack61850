// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_runtime.hpp"

#include "ariec61850/mms/buffered_selective_report_detail.hpp"

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
constexpr std::uint8_t kAllowedTriggers = 0x70U;

constexpr std::uint8_t kReasonDataChange = 0x80U;
constexpr std::uint8_t kReasonQualityChange = 0x40U;
constexpr std::uint8_t kReasonDataUpdate = 0x20U;

constexpr std::uint8_t kOptReasonForInclusion = 0x10U;
constexpr std::uint8_t kOptBufferOverflow = 0x02U;
constexpr std::uint8_t kOptEntryId = 0x01U;
constexpr std::uint8_t kAllowedOptionalFirst = 0x7FU;
constexpr std::uint8_t kAllowedOptionalSecond = 0x80U;

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

[[nodiscard]] std::uint64_t next_entry_number(
    const std::uint64_t current) noexcept {
    return current == std::numeric_limits<std::uint64_t>::max()
        ? 1U
        : current + 1U;
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

[[nodiscard]] bool valid_reference(
    const std::string_view domain,
    const std::string_view item) noexcept {
    return visible_ascii(domain, MmsInformationReportSpanCodec::maximum_reference_bytes) &&
        visible_ascii(item, MmsInformationReportSpanCodec::maximum_reference_bytes) &&
        domain.size() + 1U + item.size() <=
            MmsInformationReportSpanCodec::maximum_reference_bytes;
}

void clear_pending(MmsStaticBrcbPendingState& pending) noexcept {
    std::fill(
        pending.member_reason_masks.begin(),
        pending.member_reason_masks.end(),
        std::uint8_t{0U});
    pending.due_ms = 0U;
    pending.pending_member_count = 0U;
    pending.pending = false;
}

[[nodiscard]] bool event_mapping(
    const MmsStaticBrcbEventReason reason,
    std::uint8_t& trigger_mask,
    std::uint8_t& report_reason) noexcept {
    switch (reason) {
    case MmsStaticBrcbEventReason::data_change:
        trigger_mask = kTriggerDataChange;
        report_reason = kReasonDataChange;
        return true;
    case MmsStaticBrcbEventReason::quality_change:
        trigger_mask = kTriggerQualityChange;
        report_reason = kReasonQualityChange;
        return true;
    case MmsStaticBrcbEventReason::data_update:
        trigger_mask = kTriggerDataUpdate;
        report_reason = kReasonDataUpdate;
        return true;
    }
    return false;
}

[[nodiscard]] std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes>
encode_entry_id(const std::uint64_t entry_number) noexcept {
    std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto byte_index = result.size() - 1U - index;
        const auto shift = static_cast<unsigned>(index * 8U);
        result[byte_index] = static_cast<std::uint8_t>(
            (entry_number >> shift) & 0xFFU);
    }
    return result;
}

[[nodiscard]] bool entry_id_equal(
    const std::span<const std::uint8_t> left,
    const std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes>& right)
    noexcept {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin());
}

} // namespace

bool MmsStaticBrcbRuntime::initialize() noexcept {
    initialized_ = false;
    if (definition_ == nullptr || pending_ == nullptr || objects_ == nullptr ||
        data_sets_ == nullptr || !objects_->valid() || !data_sets_->valid() ||
        !data_sets_->valid_against(*objects_) || slots_.empty()) {
        return false;
    }
    if (!valid_reference(definition_->domain, definition_->item) ||
        !visible_ascii(
            definition_->report_id,
            MmsInformationReportSpanCodec::maximum_report_id_bytes) ||
        !valid_reference(definition_->data_set_domain, definition_->data_set_item) ||
        (definition_->optional_fields[0] &
            static_cast<std::uint8_t>(~kAllowedOptionalFirst)) != 0U ||
        (definition_->optional_fields[1] &
            static_cast<std::uint8_t>(~kAllowedOptionalSecond)) != 0U ||
        (definition_->optional_fields[0] &
            static_cast<std::uint8_t>(kOptBufferOverflow | kOptEntryId)) !=
            static_cast<std::uint8_t>(kOptBufferOverflow | kOptEntryId) ||
        definition_->trigger_options == 0U ||
        (definition_->trigger_options & static_cast<std::uint8_t>(~kAllowedTriggers)) != 0U) {
        return false;
    }

    const auto* data_set = data_sets_->find(object_name(
        definition_->data_set_domain, definition_->data_set_item));
    if (data_set == nullptr || data_set->members.empty() ||
        data_set->members.size() > MmsInformationReportSpanCodec::maximum_members) {
        return false;
    }
    for (const auto& slot : slots_) {
        if (slot.storage.empty()) {
            return false;
        }
    }

    *pending_ = {};
    pending_->revision = 1U;
    clear_retained_slots();
    head_ = 0U;
    count_ = 0U;
    delivery_offset_ = 0U;
    next_entry_number_ = 1U;
    dropped_reports_ = 0U;
    queue_revision_ = 1U;
    sequence_number_ = 0U;
    replay_gap_ = false;
    enabled_ = false;
    initialized_ = true;
    return true;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::set_enabled(
    const bool enabled) noexcept {
    if (!initialized_ || pending_ == nullptr) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    if (enabled_ == enabled) {
        return MmsStaticBrcbStatus::ok;
    }
    enabled_ = enabled;
    clear_pending(*pending_);
    bump_revision(pending_->revision);
    return MmsStaticBrcbStatus::ok;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::notify(
    const std::size_t data_set_member_index,
    const MmsStaticBrcbEventReason reason,
    const std::uint64_t now_ms) noexcept {
    if (!initialized_ || definition_ == nullptr || pending_ == nullptr ||
        data_sets_ == nullptr) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    if (!enabled_) {
        return MmsStaticBrcbStatus::temporarily_unavailable;
    }

    std::uint8_t trigger_mask = 0U;
    std::uint8_t report_reason = 0U;
    if (!event_mapping(reason, trigger_mask, report_reason) ||
        (definition_->trigger_options & trigger_mask) == 0U) {
        return MmsStaticBrcbStatus::trigger_not_selected;
    }

    const auto* data_set = data_sets_->find(object_name(
        definition_->data_set_domain, definition_->data_set_item));
    if (data_set == nullptr) {
        return MmsStaticBrcbStatus::data_set_not_found;
    }
    if (data_set_member_index >= data_set->members.size() ||
        data_set_member_index >= MmsInformationReportSpanCodec::maximum_members) {
        return MmsStaticBrcbStatus::member_out_of_range;
    }

    if (!pending_->pending) {
        pending_->pending = true;
        pending_->due_ms = saturating_add(now_ms, definition_->buffer_time_ms);
    }
    if (pending_->member_reason_masks[data_set_member_index] == 0U) {
        ++pending_->pending_member_count;
    }
    pending_->member_reason_masks[data_set_member_index] = static_cast<std::uint8_t>(
        pending_->member_reason_masks[data_set_member_index] | report_reason);
    bump_revision(pending_->revision);
    return MmsStaticBrcbStatus::ok;
}

bool MmsStaticBrcbRuntime::next_due(
    const std::uint64_t now_ms,
    MmsStaticBrcbCapturePlan& plan) const noexcept {
    plan = {};
    if (!initialized_ || pending_ == nullptr || !enabled_ ||
        !pending_->pending || pending_->pending_member_count == 0U ||
        now_ms < pending_->due_ms || slots_.empty()) {
        return false;
    }
    plan.pending_revision = pending_->revision;
    plan.queue_revision = queue_revision_;
    plan.entry_number = next_entry_number_;
    plan.sequence_number = next_sequence_number(sequence_number_);
    plan.buffer_overflow = count_ == slots_.size() && delivery_offset_ == 0U;
    return true;
}

MmsStaticBrcbCaptureResult MmsStaticBrcbRuntime::capture(
    const MmsStaticBrcbCapturePlan& plan,
    const std::span<const std::uint8_t> report_time,
    const std::span<std::uint8_t> encode_buffer,
    const std::span<std::uint8_t> workspace) noexcept {
    MmsStaticBrcbCaptureResult result;
    if (!initialized_ || definition_ == nullptr || pending_ == nullptr ||
        objects_ == nullptr || data_sets_ == nullptr || slots_.empty() ||
        delivery_offset_ > count_) {
        result.status = MmsStaticBrcbStatus::invalid_runtime;
        return result;
    }
    const bool full = count_ == slots_.size();
    const bool overflow = full && delivery_offset_ == 0U;
    if (!enabled_ || !pending_->pending || pending_->pending_member_count == 0U ||
        plan.pending_revision != pending_->revision ||
        plan.queue_revision != queue_revision_ ||
        plan.entry_number != next_entry_number_ ||
        plan.sequence_number != next_sequence_number(sequence_number_) ||
        plan.buffer_overflow != overflow) {
        result.status = MmsStaticBrcbStatus::stale_plan;
        return result;
    }

    const auto* data_set = data_sets_->find(object_name(
        definition_->data_set_domain, definition_->data_set_item));
    if (data_set == nullptr) {
        result.status = MmsStaticBrcbStatus::data_set_not_found;
        return result;
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
        const auto reason_mask = pending_->member_reason_masks[member_index];
        if (reason_mask == 0U) {
            continue;
        }
        const auto& member = data_set->members[member_index];
        const auto* object = objects_->find(object_name(member.domain, member.item));
        if (object == nullptr) {
            result.status = MmsStaticBrcbStatus::object_not_found;
            result.included_member_count = included;
            return result;
        }
        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->read(object->context, remaining);
        if (read.status == wire::EncodeStatus::buffer_too_small) {
            result.status = MmsStaticBrcbStatus::workspace_too_small;
            if (read.required_bytes <=
                std::numeric_limits<std::size_t>::max() - workspace_offset) {
                result.required_bytes = workspace_offset + read.required_bytes;
            }
            result.included_member_count = included;
            return result;
        }
        if (!read.success() || read.bytes_written > remaining.size() ||
            !detail::valid_mms_data(remaining.first(read.bytes_written))) {
            result.status = MmsStaticBrcbStatus::backend_failure;
            result.included_member_count = included;
            return result;
        }

        indices[included] = member_index;
        references[included] = {member.domain, member.item};
        results[included] = {true, remaining.first(read.bytes_written), 0U};
        reasons[included] = reason_mask;
        workspace_offset += read.bytes_written;
        ++included;
    }
    if (included == 0U || included != pending_->pending_member_count) {
        result.status = MmsStaticBrcbStatus::stale_plan;
        result.included_member_count = included;
        return result;
    }

    const auto entry_id = encode_entry_id(plan.entry_number);
    MmsBufferedSelectiveInformationReportSnapshotInput report;
    report.report_id = definition_->report_id;
    report.optional_fields = definition_->optional_fields;
    report.sequence_number = plan.sequence_number;
    report.report_time = report_time;
    report.data_set_reference = {
        definition_->data_set_domain, definition_->data_set_item};
    report.buffer_overflow = plan.buffer_overflow;
    report.entry_id = entry_id;
    report.conf_revision = definition_->conf_revision;
    report.data_set_member_count = data_set->members.size();
    report.included_member_indices =
        std::span<const std::size_t>{indices}.first(included);
    report.included_member_references =
        std::span<const MmsInformationReportReferenceInput>{references}.first(included);
    report.included_member_results =
        std::span<const MmsReadAccessResultInput>{results}.first(included);
    if ((definition_->optional_fields[0] & kOptReasonForInclusion) != 0U) {
        report.included_reason_for_inclusion =
            std::span<const std::uint8_t>{reasons}.first(included);
    }

    const auto encoded =
        MmsBufferedSelectiveInformationReportSpanCodec::encode_snapshot_into(
            report, encode_buffer);
    if (!encoded.success()) {
        result.status = encoded.status == wire::EncodeStatus::buffer_too_small
            ? MmsStaticBrcbStatus::response_buffer_too_small
            : MmsStaticBrcbStatus::report_encode_failed;
        result.required_bytes = encoded.required_bytes;
        result.included_member_count = included;
        result.entry_id = entry_id;
        return result;
    }

    const auto target_index = full
        ? head_
        : (head_ + count_) % slots_.size();
    auto& slot = slots_[target_index];
    if (slot.storage.size() < encoded.bytes_written) {
        result.status = MmsStaticBrcbStatus::slot_too_small;
        result.required_bytes = encoded.bytes_written;
        result.included_member_count = included;
        result.entry_id = entry_id;
        return result;
    }

    std::copy_n(encode_buffer.begin(), encoded.bytes_written, slot.storage.begin());
    slot.entry_id = entry_id;
    slot.bytes = encoded.bytes_written;
    slot.sequence_number = plan.sequence_number;
    slot.buffer_overflow = plan.buffer_overflow;
    slot.occupied = true;

    if (full) {
        // The physical oldest slot is being recycled. If the delivery cursor is
        // already past it, retain the same logical next-to-deliver entry by
        // shifting the cursor with the head. Otherwise an undelivered report is
        // irrecoverably lost and the recovery gap must remain visible.
        if (delivery_offset_ == 0U) {
            ++dropped_reports_;
            replay_gap_ = true;
        } else {
            --delivery_offset_;
        }
        head_ = (head_ + 1U) % slots_.size();
    } else {
        ++count_;
    }
    sequence_number_ = plan.sequence_number;
    next_entry_number_ = next_entry_number(plan.entry_number);
    clear_pending(*pending_);
    bump_revision(pending_->revision);
    bump_revision(queue_revision_);

    result.status = MmsStaticBrcbStatus::ok;
    result.bytes_written = encoded.bytes_written;
    result.required_bytes = encoded.required_bytes;
    result.included_member_count = included;
    result.entry_id = entry_id;
    return result;
}

bool MmsStaticBrcbRuntime::front(MmsStaticBrcbEntryView& entry) const noexcept {
    entry = {};
    if (!initialized_ || count_ == 0U || delivery_offset_ >= count_ ||
        head_ >= slots_.size()) {
        return false;
    }
    const auto physical = (head_ + delivery_offset_) % slots_.size();
    const auto& slot = slots_[physical];
    if (!slot.occupied || slot.bytes == 0U || slot.bytes > slot.storage.size()) {
        return false;
    }
    entry.mms_pdu = std::span<const std::uint8_t>{slot.storage}.first(slot.bytes);
    entry.entry_id = slot.entry_id;
    entry.sequence_number = slot.sequence_number;
    entry.buffer_overflow = slot.buffer_overflow;
    return true;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::commit_delivery(
    const std::span<const std::uint8_t> expected_entry_id) noexcept {
    if (!initialized_ || slots_.empty() || delivery_offset_ > count_) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    if (delivery_offset_ >= count_ || head_ >= slots_.size()) {
        return MmsStaticBrcbStatus::entry_not_found;
    }
    const auto physical = (head_ + delivery_offset_) % slots_.size();
    const auto& slot = slots_[physical];
    if (!slot.occupied || !entry_id_equal(expected_entry_id, slot.entry_id)) {
        return MmsStaticBrcbStatus::entry_not_found;
    }

    ++delivery_offset_;
    bump_revision(queue_revision_);
    return MmsStaticBrcbStatus::ok;
}

bool MmsStaticBrcbRuntime::find_entry(
    const std::span<const std::uint8_t> entry_id,
    std::size_t& logical_index) const noexcept {
    logical_index = 0U;
    if (!initialized_ || entry_id.size() != MmsInformationReportSpanCodec::entry_id_bytes ||
        count_ > slots_.size() || head_ >= slots_.size()) {
        return false;
    }
    for (std::size_t logical = 0U; logical < count_; ++logical) {
        const auto physical = (head_ + logical) % slots_.size();
        const auto& slot = slots_[physical];
        if (slot.occupied && entry_id_equal(entry_id, slot.entry_id)) {
            logical_index = logical;
            return true;
        }
    }
    return false;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::replay_from(
    const std::span<const std::uint8_t> entry_id) noexcept {
    if (!initialized_) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    std::size_t logical{};
    if (!find_entry(entry_id, logical)) {
        return MmsStaticBrcbStatus::entry_not_found;
    }
    delivery_offset_ = logical;
    bump_revision(queue_revision_);
    return MmsStaticBrcbStatus::ok;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::resume_after(
    const std::span<const std::uint8_t> entry_id) noexcept {
    if (!initialized_) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    std::size_t logical{};
    if (!find_entry(entry_id, logical)) {
        return MmsStaticBrcbStatus::entry_not_found;
    }
    delivery_offset_ = logical + 1U;
    bump_revision(queue_revision_);
    return MmsStaticBrcbStatus::ok;
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::rewind_to_oldest() noexcept {
    if (!initialized_ || count_ > slots_.size()) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    delivery_offset_ = 0U;
    bump_revision(queue_revision_);
    return MmsStaticBrcbStatus::ok;
}

void MmsStaticBrcbRuntime::clear_retained_slots() noexcept {
    for (auto& slot : slots_) {
        const auto storage = slot.storage;
        slot = {};
        slot.storage = storage;
    }
}

MmsStaticBrcbStatus MmsStaticBrcbRuntime::purge_buffer() noexcept {
    if (!initialized_ || slots_.empty()) {
        return MmsStaticBrcbStatus::invalid_runtime;
    }
    clear_retained_slots();
    head_ = 0U;
    count_ = 0U;
    delivery_offset_ = 0U;
    replay_gap_ = false;
    bump_revision(queue_revision_);
    return MmsStaticBrcbStatus::ok;
}

} // namespace ar::iec61850::mms