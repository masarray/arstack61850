// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/selective_information_report_span.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticUrcbEventStatus : std::uint8_t {
    ok,
    invalid_runtime,
    index_out_of_range,
    member_out_of_range,
    temporarily_unavailable,
    trigger_not_selected,
    data_set_not_found,
    stale_plan,
    workspace_too_small,
    object_not_found,
    backend_failure,
    response_buffer_too_small,
    report_encode_failed,
};

enum class MmsStaticUrcbEventReason : std::uint8_t {
    data_change,
    quality_change,
    data_update,
};

struct MmsStaticUrcbEventState final {
    std::array<std::uint8_t, MmsInformationReportSpanCodec::maximum_members>
        member_reason_masks{};
    std::uint64_t due_ms{};
    std::uint32_t source_revision{};
    std::uint32_t revision{};
    std::size_t pending_member_count{};
    bool pending{};
};

struct MmsStaticUrcbEventPlan final {
    static constexpr std::size_t invalid_index =
        std::numeric_limits<std::size_t>::max();

    std::size_t index{invalid_index};
    std::uint32_t source_revision{};
    std::uint32_t event_revision{};
    std::uint8_t sequence_number{};
};

struct MmsStaticUrcbEventEncodeResult final {
    MmsStaticUrcbEventStatus status{MmsStaticUrcbEventStatus::invalid_runtime};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
    std::size_t data_set_member_count{};
    std::size_t included_member_count{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticUrcbEventStatus::ok;
    }
};

// Change-trigger extension for the bounded URCB runtime. GI/integrity scheduling
// remains in MmsStaticUrcbRuntime; this class owns only dchg/qchg/dupd pending
// state and BufTm coalescing.
class MmsStaticUrcbEventRuntime final {
public:
    MmsStaticUrcbEventRuntime(
        MmsStaticUrcbRuntime& urcb,
        std::span<MmsStaticUrcbEventState> states,
        const MmsStaticObjectTable& objects,
        const MmsStaticDataSetTable& data_sets) noexcept
        : urcb_{&urcb}, states_{states}, objects_{&objects}, data_sets_{&data_sets} {}

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }

    [[nodiscard]] MmsStaticUrcbEventStatus notify(
        std::size_t control_block_index,
        std::size_t data_set_member_index,
        MmsStaticUrcbEventReason reason,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] bool next_due(
        std::uint64_t now_ms,
        MmsStaticUrcbEventPlan& plan) const noexcept;

    [[nodiscard]] MmsStaticUrcbEventEncodeResult encode(
        const MmsStaticUrcbEventPlan& plan,
        std::span<const std::uint8_t> report_time,
        std::span<std::uint8_t> destination,
        std::span<std::uint8_t> workspace) const noexcept;

    [[nodiscard]] MmsStaticUrcbEventStatus commit(
        const MmsStaticUrcbEventPlan& plan) noexcept;

    [[nodiscard]] const MmsStaticUrcbEventState* state(
        std::size_t index) const noexcept;

private:
    MmsStaticUrcbRuntime* urcb_{};
    std::span<MmsStaticUrcbEventState> states_{};
    const MmsStaticObjectTable* objects_{};
    const MmsStaticDataSetTable* data_sets_{};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
