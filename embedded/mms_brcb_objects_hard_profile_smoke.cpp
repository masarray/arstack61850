// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kTrue{0x83U, 0x01U, 0xFFU};
constexpr std::array<std::uint8_t, 3U> kFalse{0x83U, 0x01U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kFiveSeconds{0x85U, 0x01U, 0x05U};
constexpr std::array<std::uint8_t, 3U> kZeroSeconds{0x85U, 0x01U, 0x00U};

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] std::uint64_t read_now(const void* context) noexcept {
    return context == nullptr ? 0U : *static_cast<const std::uint64_t*>(context);
}

[[nodiscard]] mms::MmsStaticRequestAccessContext access(
    const std::uint64_t association,
    const std::span<const std::uint8_t> owner) noexcept {
    return {association, owner};
}

[[nodiscard]] std::size_t build_write_request(
    const std::uint8_t invoke,
    const std::string_view domain,
    const std::string_view item,
    const std::span<const std::uint8_t> data,
    const std::span<std::uint8_t> destination) noexcept {
    if (domain.size() > 0x7FU || item.size() > 0x7FU || data.size() > 0x7FU) {
        return 0U;
    }
    const auto total = 24U + item.size() + data.size() + domain.size() - 3U;
    if (total > destination.size() || total > 0x7FU) {
        return 0U;
    }
    const auto confirmed_content = total - 2U;
    const auto write_content = 14U + domain.size() + item.size() + data.size();
    const auto variable_list_content = 10U + domain.size() + item.size();
    const auto sequence_content = 8U + domain.size() + item.size();
    const auto variable_content = 6U + domain.size() + item.size();
    const auto domain_specific_content = 4U + domain.size() + item.size();

    std::size_t offset = 0U;
    const auto put = [&](const std::uint8_t byte) noexcept -> bool {
        if (offset >= destination.size()) {
            return false;
        }
        destination[offset++] = byte;
        return true;
    };
    if (!put(0xA0U) || !put(static_cast<std::uint8_t>(confirmed_content)) ||
        !put(0x02U) || !put(0x01U) || !put(invoke) ||
        !put(0xA5U) || !put(static_cast<std::uint8_t>(write_content)) ||
        !put(0xA0U) || !put(static_cast<std::uint8_t>(variable_list_content)) ||
        !put(0x30U) || !put(static_cast<std::uint8_t>(sequence_content)) ||
        !put(0xA0U) || !put(static_cast<std::uint8_t>(variable_content)) ||
        !put(0xA1U) || !put(static_cast<std::uint8_t>(domain_specific_content)) ||
        !put(0x1AU) || !put(static_cast<std::uint8_t>(domain.size()))) {
        return 0U;
    }
    for (const auto ch : domain) {
        if (!put(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)))) {
            return 0U;
        }
    }
    if (!put(0x1AU) || !put(static_cast<std::uint8_t>(item.size()))) {
        return 0U;
    }
    for (const auto ch : item) {
        if (!put(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)))) {
            return 0U;
        }
    }
    if (!put(0xA0U) || !put(static_cast<std::uint8_t>(data.size()))) {
        return 0U;
    }
    for (const auto byte : data) {
        if (!put(byte)) {
            return 0U;
        }
    }
    return offset == total ? offset : 0U;
}

[[nodiscard]] bool dispatch_write(
    const mms::MmsStaticApplicationDispatcher& dispatcher,
    const std::string_view item,
    const std::span<const std::uint8_t> data,
    const mms::MmsStaticRequestAccessContext& client,
    const bool expected_success,
    const std::uint32_t expected_failure,
    std::uint8_t& invoke) noexcept {
    std::array<std::uint8_t, 128U> request{};
    std::array<std::uint8_t, 128U> response{};
    std::array<std::uint8_t, 256U> workspace{};
    const auto bytes = build_write_request(
        invoke++, "LD0", item, data, request);
    if (bytes == 0U) {
        return false;
    }
    const auto dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(bytes),
        response,
        workspace,
        client);
    if (!dispatched.success()) {
        return false;
    }
    mms::MmsWriteResponseView decoded;
    mms::MmsWriteAccessResultView result;
    if (!mms::MmsServiceSpanCodec::try_decode_write_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            decoded) ||
        decoded.result_count != 1U || !decoded.try_result(0U, result)) {
        return false;
    }
    return expected_success
        ? result.success
        : !result.success && result.failure_code == expected_failure;
}

[[nodiscard]] std::array<std::uint8_t, 10U> octet_data(
    const std::span<const std::uint8_t> value) noexcept {
    std::array<std::uint8_t, 10U> encoded{};
    if (value.size() != 8U) {
        return encoded;
    }
    encoded[0] = 0x89U;
    encoded[1] = 0x08U;
    std::copy(value.begin(), value.end(), encoded.begin() + 2);
    return encoded;
}

} // namespace

int main() {
    bool value = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> base_objects{
        mms::MmsStaticObjectEntry{
            "LD0", "X1", kBooleanType, read_boolean, &value, false}};
    const mms::MmsStaticObjectTable base_table{base_objects};

    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};

    const mms::MmsStaticBrcbDefinition definition{
        "LD0",
        "B1",
        "LD0/LLN0$BR$B1",
        "LD0",
        "LLN0$Events",
        1U,
        {0x7FU, 0x80U},
        0U,
        0x70U};

    std::array<std::uint8_t, 512U> slot0{};
    std::array<std::uint8_t, 512U> slot1{};
    std::array<mms::MmsStaticBrcbSlot, 2U> slots{
        mms::MmsStaticBrcbSlot{slot0},
        mms::MmsStaticBrcbSlot{slot1}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, base_table, data_set_table};
    if (!reports.initialize()) {
        return 1;
    }
    mms::MmsStaticBrcbControl control{reports};

    std::array<mms::MmsStaticObjectEntry, 9U> object_storage{};
    std::array<mms::MmsStaticBrcbObjectContext, 8U> context_storage{};
    std::array<char, 128U> name_storage{};
    std::uint64_t now = 100U;
    mms::MmsStaticBrcbObjectBank bank{
        definition,
        reports,
        control,
        base_objects,
        object_storage,
        context_storage,
        name_storage,
        read_now,
        &now};
    if (!bank.initialize() || bank.object_count() != 9U || !bank.table().valid()) {
        return 2;
    }

    const mms::MmsStaticApplicationDispatcher dispatcher{bank.table(), data_set_table};
    const std::array<std::uint8_t, 2U> owner_a{0xAAU, 0x01U};
    const std::array<std::uint8_t, 2U> owner_b{0xBBU, 0x01U};
    const auto a = access(101U, owner_a);
    const auto b = access(201U, owner_b);
    std::uint8_t invoke = 1U;

    if (!dispatch_write(dispatcher, "B1$RptEna", kTrue, a, true, 0U, invoke) ||
        !reports.enabled()) {
        return 3;
    }
    auto state = control.state(now);
    if (!state.reserved || !state.owner_connected ||
        state.association_id != 101U || state.owner.size() != owner_a.size() ||
        !std::equal(state.owner.begin(), state.owner.end(), owner_a.begin())) {
        return 4;
    }

    if (!dispatch_write(dispatcher, "B1$RptEna", kTrue, b, false, 3U, invoke) ||
        !dispatch_write(dispatcher, "B1$PurgeBuf", kTrue, b, false, 3U, invoke)) {
        return 5;
    }

    std::array<std::uint8_t, 1024U> encode_buffer{};
    std::array<std::uint8_t, 1024U> capture_workspace{};
    const std::array<std::uint8_t, 6U> report_time{0U,0U,0x12U,0x34U,0U,0x01U};
    if (reports.notify(0U, mms::MmsStaticBrcbEventReason::data_change, now) !=
            mms::MmsStaticBrcbStatus::ok) {
        return 6;
    }
    mms::MmsStaticBrcbCapturePlan plan;
    if (!reports.next_due(now, plan)) {
        return 7;
    }
    const auto first = reports.capture(plan, report_time, encode_buffer, capture_workspace);
    if (!first.success()) {
        return 8;
    }

    value = false;
    now = 200U;
    if (reports.notify(0U, mms::MmsStaticBrcbEventReason::data_change, now) !=
            mms::MmsStaticBrcbStatus::ok ||
        !reports.next_due(now, plan)) {
        return 9;
    }
    const auto second = reports.capture(plan, report_time, encode_buffer, capture_workspace);
    if (!second.success() || reports.retained_size() != 2U ||
        reports.latest_entry_id() != second.entry_id) {
        return 10;
    }

    if (!dispatch_write(dispatcher, "B1$RptEna", kFalse, a, true, 0U, invoke) ||
        reports.enabled()) {
        return 11;
    }

    if (!dispatch_write(dispatcher, "B1$ResvTms", kFiveSeconds, a, true, 0U, invoke)) {
        return 12;
    }
    state = control.state(now);
    if (!state.reserved || state.resv_tms_seconds != 5U ||
        state.owner.size() != owner_a.size()) {
        return 13;
    }

    const auto first_entry_data = octet_data(first.entry_id);
    if (!dispatch_write(
            dispatcher, "B1$EntryID", first_entry_data, a, true, 0U, invoke) ||
        reports.queue_size() != 1U) {
        return 14;
    }
    mms::MmsStaticBrcbEntryView front;
    if (!reports.front(front) || front.entry_id.size() != second.entry_id.size() ||
        !std::equal(front.entry_id.begin(), front.entry_id.end(), second.entry_id.begin())) {
        return 15;
    }

    const std::array<std::uint8_t, 8U> zero_entry{};
    const auto zero_entry_data = octet_data(zero_entry);
    if (!dispatch_write(
            dispatcher, "B1$EntryID", zero_entry_data, a, true, 0U, invoke) ||
        reports.queue_size() != 2U) {
        return 16;
    }
    const std::array<std::uint8_t, 8U> missing_entry{0U,0U,0U,0U,0U,0U,0U,0x7FU};
    const auto missing_entry_data = octet_data(missing_entry);
    if (!dispatch_write(
            dispatcher, "B1$EntryID", missing_entry_data, a, false, 11U, invoke)) {
        return 17;
    }

    if (!dispatch_write(dispatcher, "B1$PurgeBuf", kTrue, a, true, 0U, invoke) ||
        reports.retained_size() != 0U || reports.queue_size() != 0U ||
        reports.latest_entry_id() != zero_entry) {
        return 18;
    }

    if (!dispatch_write(dispatcher, "B1$ResvTms", kZeroSeconds, a, true, 0U, invoke)) {
        return 19;
    }
    state = control.state(now);
    if (state.reserved || !state.owner.empty()) {
        return 20;
    }

    std::array<std::uint8_t, 32U> read_buffer{};
    const auto& entry_object = object_storage[1U + 5U];
    const auto entry_read = entry_object.read(entry_object.context, read_buffer);
    if (!entry_read.success() || entry_read.bytes_written != 10U ||
        read_buffer[0] != 0x89U || read_buffer[1] != 0x08U ||
        !std::all_of(read_buffer.begin() + 2, read_buffer.begin() + 10,
            [](const std::uint8_t byte) { return byte == 0U; })) {
        return 21;
    }
    const auto& purge_object = object_storage[1U + 4U];
    const auto purge_read = purge_object.read(purge_object.context, read_buffer);
    if (!purge_read.success() || purge_read.bytes_written != 3U ||
        read_buffer[0] != 0x83U || read_buffer[2] != 0x00U) {
        return 22;
    }

    return 0;
}
