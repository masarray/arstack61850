// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_information_report.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

enum class MmsStaticUrcbStatus : std::uint8_t {
    ok,
    invalid_runtime,
    index_out_of_range,
    invalid_value,
    object_access_denied,
    temporarily_unavailable,
    data_set_not_found,
    no_report_due,
    stale_plan,
    workspace_too_small,
    backend_failure,
    response_buffer_too_small,
    report_encode_failed,
};

enum class MmsStaticUrcbReportReason : std::uint8_t {
    none = 0x00U,
    general_interrogation = 0x08U,
    integrity = 0x10U,
};

struct MmsStaticUrcbDefinition final {
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
    std::uint32_t integrity_period_ms{};
};

struct MmsStaticUrcbState final {
    static constexpr std::size_t maximum_report_id_bytes =
        MmsInformationReportSpanCodec::maximum_report_id_bytes;
    static constexpr std::size_t maximum_reference_component_bytes =
        MmsInformationReportSpanCodec::maximum_reference_bytes;

    std::array<char, maximum_report_id_bytes> report_id_storage{};
    std::size_t report_id_size{};
    std::array<char, maximum_reference_component_bytes> data_set_domain_storage{};
    std::size_t data_set_domain_size{};
    std::array<char, maximum_reference_component_bytes> data_set_item_storage{};
    std::size_t data_set_item_size{};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::optional_field_bytes>
        optional_fields{};
    std::uint32_t conf_revision{1U};
    std::uint32_t buffer_time_ms{};
    std::uint32_t integrity_period_ms{};
    std::uint64_t next_integrity_due_ms{};
    std::uint32_t revision{};
    std::uint8_t trigger_options{};
    std::uint8_t sequence_number{};
    bool enabled{};
    bool reserved{};
    bool general_interrogation_pending{};
    bool integrity_armed{};

    [[nodiscard]] std::string_view report_id() const noexcept {
        return {report_id_storage.data(), report_id_size};
    }

    [[nodiscard]] std::string_view data_set_domain() const noexcept {
        return {data_set_domain_storage.data(), data_set_domain_size};
    }

    [[nodiscard]] std::string_view data_set_item() const noexcept {
        return {data_set_item_storage.data(), data_set_item_size};
    }
};

struct MmsStaticUrcbEmissionPlan final {
    static constexpr std::size_t invalid_index =
        std::numeric_limits<std::size_t>::max();

    std::size_t index{invalid_index};
    std::uint32_t revision{};
    std::uint8_t sequence_number{};
    MmsStaticUrcbReportReason reason{MmsStaticUrcbReportReason::none};
};

struct MmsStaticUrcbEncodeResult final {
    MmsStaticUrcbStatus status{MmsStaticUrcbStatus::invalid_runtime};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
    std::size_t member_count{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticUrcbStatus::ok;
    }
};

class MmsStaticUrcbRuntime final {
public:
    static constexpr std::size_t maximum_control_blocks = 16U;
    static constexpr std::uint32_t minimum_integrity_period_ms = 100U;

    MmsStaticUrcbRuntime(
        std::span<const MmsStaticUrcbDefinition> definitions,
        std::span<MmsStaticUrcbState> states,
        const MmsStaticObjectTable& objects,
        const MmsStaticDataSetTable& data_sets) noexcept
        : definitions_{definitions},
          states_{states},
          objects_{&objects},
          data_sets_{&data_sets} {}

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return initialized_ ? definitions_.size() : 0U;
    }

    [[nodiscard]] const MmsStaticUrcbDefinition* definition(
        std::size_t index) const noexcept;
    [[nodiscard]] const MmsStaticUrcbState* state(std::size_t index) const noexcept;
    [[nodiscard]] MmsStaticUrcbState* state(std::size_t index) noexcept;

    [[nodiscard]] bool find_index(
        const MmsObjectNameView& name,
        std::size_t& index) const noexcept;

    [[nodiscard]] MmsStaticUrcbStatus set_enabled(
        std::size_t index,
        bool enabled,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_reserved(
        std::size_t index,
        bool reserved) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_report_id(
        std::size_t index,
        std::string_view report_id) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_data_set(
        std::size_t index,
        std::string_view domain,
        std::string_view item) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_optional_fields(
        std::size_t index,
        std::span<const std::uint8_t> optional_fields) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_buffer_time_ms(
        std::size_t index,
        std::uint32_t buffer_time_ms) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_trigger_options(
        std::size_t index,
        std::uint8_t trigger_options) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus set_integrity_period_ms(
        std::size_t index,
        std::uint32_t integrity_period_ms) noexcept;
    [[nodiscard]] MmsStaticUrcbStatus request_general_interrogation(
        std::size_t index) noexcept;

    [[nodiscard]] bool next_due(
        std::uint64_t now_ms,
        MmsStaticUrcbEmissionPlan& plan) const noexcept;

    [[nodiscard]] MmsStaticUrcbEncodeResult encode(
        const MmsStaticUrcbEmissionPlan& plan,
        std::span<const std::uint8_t> report_time,
        std::span<std::uint8_t> destination,
        std::span<std::uint8_t> workspace) const noexcept;

    [[nodiscard]] MmsStaticUrcbStatus commit(
        const MmsStaticUrcbEmissionPlan& plan,
        std::uint64_t now_ms) noexcept;

private:
    std::span<const MmsStaticUrcbDefinition> definitions_{};
    std::span<MmsStaticUrcbState> states_{};
    const MmsStaticObjectTable* objects_{};
    const MmsStaticDataSetTable* data_sets_{};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
