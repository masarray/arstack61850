// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/buffered_selective_information_report_span.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

class MmsStaticBrcbStateCodec;

enum class MmsStaticBrcbStatus : std::uint8_t {
    ok,
    invalid_runtime,
    invalid_definition,
    member_out_of_range,
    trigger_not_selected,
    temporarily_unavailable,
    no_report_due,
    stale_plan,
    data_set_not_found,
    object_not_found,
    workspace_too_small,
    response_buffer_too_small,
    slot_too_small,
    backend_failure,
    report_encode_failed,
    entry_not_found,
};

enum class MmsStaticBrcbEventReason : std::uint8_t {
    data_change,
    quality_change,
    data_update,
};

struct MmsStaticBrcbDefinition final {
    std::string_view domain;
    std::string_view item;
    std::string_view report_id;
    std::string_view data_set_domain;
    std::string_view data_set_item;
    std::uint32_t conf_revision{1U};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::optional_field_bytes>
        optional_fields{};
    std::uint32_t buffer_time_ms{};
    std::uint8_t trigger_options{};
};

// Payload storage is caller-owned so applications choose the queue RAM budget.
// `storage` must remain valid for the lifetime of the runtime.
struct MmsStaticBrcbSlot final {
    std::span<std::uint8_t> storage{};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes> entry_id{};
    std::size_t bytes{};
    std::uint8_t sequence_number{};
    bool buffer_overflow{};
    bool occupied{};
};

struct MmsStaticBrcbPendingState final {
    std::array<std::uint8_t, MmsInformationReportSpanCodec::maximum_members>
        member_reason_masks{};
    std::uint64_t due_ms{};
    std::uint32_t revision{};
    std::size_t pending_member_count{};
    bool pending{};
};

struct MmsStaticBrcbCapturePlan final {
    std::uint32_t pending_revision{};
    std::uint32_t queue_revision{};
    std::uint64_t entry_number{};
    std::uint8_t sequence_number{};
    bool buffer_overflow{};
};

struct MmsStaticBrcbCaptureResult final {
    MmsStaticBrcbStatus status{MmsStaticBrcbStatus::invalid_runtime};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
    std::size_t included_member_count{};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes> entry_id{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticBrcbStatus::ok;
    }
};

struct MmsStaticBrcbEntryView final {
    std::span<const std::uint8_t> mms_pdu{};
    std::span<const std::uint8_t> entry_id{};
    std::uint8_t sequence_number{};
    bool buffer_overflow{};
};

// Bounded BRCB profile: one runtime instance represents one BRCB. Captured
// snapshots remain retained in caller-owned RAM after successful delivery so a
// client can reposition the delivery cursor by EntryID. The active delivery
// queue and the retained-history window are therefore intentionally distinct.
// Persistence is storage-agnostic and mediated by MmsStaticBrcbStateCodec.
class MmsStaticBrcbRuntime final {
public:
    MmsStaticBrcbRuntime(
        const MmsStaticBrcbDefinition& definition,
        MmsStaticBrcbPendingState& pending,
        std::span<MmsStaticBrcbSlot> slots,
        const MmsStaticObjectTable& objects,
        const MmsStaticDataSetTable& data_sets) noexcept
        : definition_{&definition}, pending_{&pending}, slots_{slots},
          objects_{&objects}, data_sets_{&data_sets} {}

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }

    [[nodiscard]] MmsStaticBrcbStatus set_enabled(bool enabled) noexcept;
    [[nodiscard]] constexpr bool enabled() const noexcept { return enabled_; }

    [[nodiscard]] MmsStaticBrcbStatus notify(
        std::size_t data_set_member_index,
        MmsStaticBrcbEventReason reason,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] bool next_due(
        std::uint64_t now_ms,
        MmsStaticBrcbCapturePlan& plan) const noexcept;

    [[nodiscard]] MmsStaticBrcbCaptureResult capture(
        const MmsStaticBrcbCapturePlan& plan,
        std::span<const std::uint8_t> report_time,
        std::span<std::uint8_t> encode_buffer,
        std::span<std::uint8_t> workspace) noexcept;

    // Returns the next entry selected by the current delivery cursor.
    [[nodiscard]] bool front(MmsStaticBrcbEntryView& entry) const noexcept;

    // Marks the currently selected entry as delivered without deleting the
    // retained snapshot. This is what makes later replay/resume possible.
    [[nodiscard]] MmsStaticBrcbStatus commit_delivery(
        std::span<const std::uint8_t> expected_entry_id) noexcept;

    // Reposition delivery to the exact retained EntryID.
    [[nodiscard]] MmsStaticBrcbStatus replay_from(
        std::span<const std::uint8_t> entry_id) noexcept;

    // Reposition delivery to the retained entry immediately after EntryID.
    // If EntryID is the newest retained entry, the active queue becomes empty.
    [[nodiscard]] MmsStaticBrcbStatus resume_after(
        std::span<const std::uint8_t> entry_id) noexcept;

    // Replays the complete retained window from its oldest available entry.
    [[nodiscard]] MmsStaticBrcbStatus rewind_to_oldest() noexcept;

    // Implements the runtime PurgeBuf primitive. It clears retained reports and
    // delivery/recovery state while preserving the report definition, EntryID
    // monotonic progression, and any independent pending trigger accumulation.
    [[nodiscard]] MmsStaticBrcbStatus purge_buffer() noexcept;

    // Number of reports still selected for delivery.
    [[nodiscard]] constexpr std::size_t queue_size() const noexcept {
        return delivery_offset_ <= count_ ? count_ - delivery_offset_ : 0U;
    }

    // Number of snapshots retained for possible replay, delivered or not.
    [[nodiscard]] constexpr std::size_t retained_size() const noexcept { return count_; }

    [[nodiscard]] constexpr std::size_t queue_capacity() const noexcept {
        return slots_.size();
    }
    [[nodiscard]] constexpr std::uint64_t dropped_reports() const noexcept {
        return dropped_reports_;
    }

    // True after an undelivered retained entry was evicted because the bounded
    // buffer filled. PurgeBuf clears this condition; replay cannot reconstruct
    // data that is no longer retained.
    [[nodiscard]] constexpr bool replay_gap() const noexcept { return replay_gap_; }

private:
    friend class MmsStaticBrcbStateCodec;

    [[nodiscard]] bool find_entry(
        std::span<const std::uint8_t> entry_id,
        std::size_t& logical_index) const noexcept;
    void clear_retained_slots() noexcept;

    const MmsStaticBrcbDefinition* definition_{};
    MmsStaticBrcbPendingState* pending_{};
    std::span<MmsStaticBrcbSlot> slots_{};
    const MmsStaticObjectTable* objects_{};
    const MmsStaticDataSetTable* data_sets_{};
    std::size_t head_{};
    std::size_t count_{};
    std::size_t delivery_offset_{};
    std::uint64_t next_entry_number_{1U};
    std::uint64_t dropped_reports_{};
    std::uint32_t queue_revision_{1U};
    std::uint8_t sequence_number_{};
    bool replay_gap_{};
    bool enabled_{};
    bool initialized_{};
};

} // namespace ar::iec61850::mms