// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kUnsigned8Type{0x86U, 0x01U, 0x08U};
constexpr std::array<std::uint8_t, 3U> kUnsigned32Type{0x86U, 0x01U, 0x20U};
constexpr std::array<std::uint8_t, 4U> kVisible129Type{0x8AU, 0x02U, 0xFFU, 0x7FU};
constexpr std::array<std::uint8_t, 4U> kVariableOctet64Type{0x89U, 0x02U, 0xFFU, 0xC0U};
constexpr std::array<std::uint8_t, 3U> kBitString10Type{0x84U, 0x01U, 0x0AU};
constexpr std::array<std::uint8_t, 3U> kBitString6Type{0x84U, 0x01U, 0x06U};
constexpr std::array<std::uint8_t, 3U> kTrue{0x83U, 0x01U, 0xFFU};
constexpr std::array<std::string_view, 12U> kUrcbNames{
    "RptID", "RptEna", "Resv", "DatSet", "ConfRev", "OptFlds",
    "BufTm", "SqNum", "TrgOps", "IntgPd", "GI", "Owner"};
constexpr std::array<std::string_view, 15U> kBrcbNames{
    "RptID", "RptEna", "DatSet", "ConfRev", "OptFlds", "BufTm", "SqNum",
    "TrgOps", "IntgPd", "GI", "PurgeBuf", "EntryID", "TimeofEntry",
    "ResvTms", "Owner"};

enum class MockUrcbAttribute : std::uint8_t {
    report_id,
    report_enabled,
    reserved,
    data_set,
    conf_revision,
    optional_fields,
    buffer_time,
    sequence_number,
    trigger_options,
    integrity_period,
    general_interrogation,
    owner,
};

struct MockUrcbState final {
    std::string report_id{"URCB-LIVE"};
    bool enabled{true};
    bool reserved{true};
};

struct MockUrcbContext final {
    MockUrcbState* state{};
    MockUrcbAttribute attribute{MockUrcbAttribute::report_id};
};

[[nodiscard]] wire::EncodeResult emit(
    const std::span<const std::uint8_t> bytes,
    const std::span<std::uint8_t> destination) noexcept {
    if (destination.size() < bytes.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, bytes.size()};
    }
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    return {wire::EncodeStatus::ok, bytes.size(), bytes.size()};
}

[[nodiscard]] wire::EncodeResult emit_boolean(
    const bool value,
    const std::span<std::uint8_t> destination) noexcept {
    const std::array<std::uint8_t, 3U> encoded{
        0x83U, 0x01U, value ? std::uint8_t{0xFFU} : std::uint8_t{0x00U}};
    return emit(encoded, destination);
}

[[nodiscard]] wire::EncodeResult emit_unsigned(
    const std::uint8_t value,
    const std::span<std::uint8_t> destination) noexcept {
    const std::array<std::uint8_t, 3U> encoded{0x86U, 0x01U, value};
    return emit(encoded, destination);
}

[[nodiscard]] wire::EncodeResult emit_visible(
    const std::string_view value,
    const std::span<std::uint8_t> destination) noexcept {
    if (value.empty() || value.size() >= 0x80U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = value.size() + 2U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x8AU;
    destination[1] = static_cast<std::uint8_t>(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        destination[index + 2U] = static_cast<std::uint8_t>(
            static_cast<unsigned char>(value[index]));
    }
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult emit_empty_octets(
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::array<std::uint8_t, 2U> encoded{0x89U, 0x00U};
    return emit(encoded, destination);
}

[[nodiscard]] wire::EncodeResult read_true(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, kTrue.size()};
    }
    return emit(kTrue, destination);
}

[[nodiscard]] wire::EncodeResult read_mock_urcb(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const MockUrcbContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    switch (context->attribute) {
    case MockUrcbAttribute::report_id:
        return emit_visible(context->state->report_id, destination);
    case MockUrcbAttribute::report_enabled:
        return emit_boolean(context->state->enabled, destination);
    case MockUrcbAttribute::reserved:
        return emit_boolean(context->state->reserved, destination);
    case MockUrcbAttribute::data_set:
        return emit_visible("LD0/LLN0$Events", destination);
    case MockUrcbAttribute::conf_revision:
        return emit_unsigned(1U, destination);
    case MockUrcbAttribute::optional_fields: {
        constexpr std::array<std::uint8_t, 5U> encoded{0x84U, 0x03U, 0x06U, 0x5CU, 0x80U};
        return emit(encoded, destination);
    }
    case MockUrcbAttribute::buffer_time:
        return emit_unsigned(0U, destination);
    case MockUrcbAttribute::sequence_number:
        return emit_unsigned(0U, destination);
    case MockUrcbAttribute::trigger_options: {
        constexpr std::array<std::uint8_t, 4U> encoded{0x84U, 0x02U, 0x02U, 0x70U};
        return emit(encoded, destination);
    }
    case MockUrcbAttribute::integrity_period:
        return emit_unsigned(0U, destination);
    case MockUrcbAttribute::general_interrogation:
        return emit_boolean(false, destination);
    case MockUrcbAttribute::owner:
        return emit_empty_octets(destination);
    }
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

[[nodiscard]] std::uint64_t read_now(const void* context) noexcept {
    return context == nullptr ? 0U : *static_cast<const std::uint64_t*>(context);
}

[[nodiscard]] bool bool_value(
    const mms::MmsDataValue& value,
    const bool expected) noexcept {
    if (value.kind() != mms::MmsDataKind::boolean) return false;
    const auto* actual = std::get_if<bool>(&value.value());
    return actual != nullptr && *actual == expected;
}

[[nodiscard]] bool string_value(
    const mms::MmsDataValue& value,
    const std::string_view expected) noexcept {
    if (value.kind() != mms::MmsDataKind::visible_string) return false;
    const auto* actual = std::get_if<std::string>(&value.value());
    return actual != nullptr && *actual == expected;
}

[[nodiscard]] bool octets_value(
    const mms::MmsDataValue& value,
    const std::span<const std::uint8_t> expected) noexcept {
    return value.kind() == mms::MmsDataKind::octet_string &&
        value.raw_value().size() == expected.size() &&
        std::equal(value.raw_value().begin(), value.raw_value().end(), expected.begin());
}

[[nodiscard]] bool type_names_match(
    const mms::MmsTypeSpecification& type,
    const std::span<const std::string_view> names) noexcept {
    if (type.kind != mms::MmsTypeKind::structure || type.children.size() != names.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (type.children[index].name != names[index]) return false;
    }
    return true;
}

[[nodiscard]] bool read_roots(
    const mms::MmsStaticApplicationDispatcher& dispatcher,
    const std::uint32_t invoke,
    mms::MmsReadResponse& decoded) {
    mms::MmsReadRequest request;
    request.invoke_id = invoke;
    request.variables.push_back(
        mms::MmsObjectName::domain_specific("LD0", "LLN0$RP$U1"));
    request.variables.push_back(
        mms::MmsObjectName::domain_specific("LD0", "LLN0$BR$B1"));
    const auto encoded = mms::MmsServiceCodec::encode_read_request_pdu(request);

    std::array<std::uint8_t, 4'096U> response{};
    std::array<std::uint8_t, 4'096U> workspace{};
    const auto dispatched = dispatcher.dispatch(encoded, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::read ||
        dispatched.invoke_id != invoke) {
        return false;
    }
    decoded = mms::MmsServiceCodec::decode_read_response(
        std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
        invoke);
    return decoded.results.size() == 2U &&
        decoded.results[0].success() && decoded.results[1].success();
}

[[nodiscard]] bool read_type(
    const mms::MmsStaticApplicationDispatcher& dispatcher,
    const std::uint32_t invoke,
    const std::string_view item,
    mms::MmsVariableAccessAttributesResponse& decoded) {
    mms::MmsVariableAccessAttributesRequest request;
    request.invoke_id = invoke;
    request.name = mms::MmsObjectName::domain_specific("LD0", std::string{item});
    const auto encoded =
        mms::MmsServiceCodec::encode_variable_access_attributes_request_pdu(request);

    std::array<std::uint8_t, 4'096U> response{};
    std::array<std::uint8_t, 4'096U> workspace{};
    const auto dispatched = dispatcher.dispatch(encoded, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_variable_access_attributes ||
        dispatched.invoke_id != invoke) {
        return false;
    }
    decoded = mms::MmsServiceCodec::decode_variable_access_attributes_response(
        std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
        invoke);
    return !decoded.mms_deletable;
}

} // namespace

int main() {
    try {
        const bool source_value = true;
        MockUrcbState urcb_state;
        std::array<MockUrcbContext, 12U> urcb_contexts{};
        for (std::size_t index = 0U; index < urcb_contexts.size(); ++index) {
            urcb_contexts[index] = MockUrcbContext{
                &urcb_state,
                static_cast<MockUrcbAttribute>(index)};
        }

        std::array<mms::MmsStaticObjectEntry, 13U> pre_brcb_objects{};
        pre_brcb_objects[0] = mms::MmsStaticObjectEntry{
            "LD0", "X1", kBooleanType, read_true, &source_value, false};
        constexpr std::array<std::string_view, 12U> urcb_items{
            "LLN0$RP$U1$RptID", "LLN0$RP$U1$RptEna", "LLN0$RP$U1$Resv",
            "LLN0$RP$U1$DatSet", "LLN0$RP$U1$ConfRev", "LLN0$RP$U1$OptFlds",
            "LLN0$RP$U1$BufTm", "LLN0$RP$U1$SqNum", "LLN0$RP$U1$TrgOps",
            "LLN0$RP$U1$IntgPd", "LLN0$RP$U1$GI", "LLN0$RP$U1$Owner"};
        const std::array<std::span<const std::uint8_t>, 12U> urcb_types{
            kVisible129Type, kBooleanType, kBooleanType, kVisible129Type,
            kUnsigned32Type, kBitString10Type, kUnsigned32Type, kUnsigned8Type,
            kBitString6Type, kUnsigned32Type, kBooleanType, kVariableOctet64Type};
        for (std::size_t index = 0U; index < urcb_items.size(); ++index) {
            pre_brcb_objects[index + 1U] = mms::MmsStaticObjectEntry{
                "LD0",
                urcb_items[index],
                urcb_types[index],
                read_mock_urcb,
                &urcb_contexts[index],
                false};
        }
        const mms::MmsStaticObjectTable pre_brcb_table{pre_brcb_objects};
        if (!pre_brcb_table.valid()) return 1;

        const std::array<mms::MmsStaticDataSetMember, 1U> members{
            mms::MmsStaticDataSetMember{"LD0", "X1"}};
        const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
            mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
        const mms::MmsStaticDataSetTable data_set_table{data_sets};
        if (!data_set_table.valid_against(pre_brcb_table)) return 2;

        const mms::MmsStaticBrcbDefinition brcb_definition{
            "LD0",
            "LLN0$BR$B1",
            "BRCB-B1",
            "LD0",
            "LLN0$Events",
            7U,
            {0x5DU, 0x80U},
            0U,
            0x70U};
        std::array<std::uint8_t, 1'024U> slot0{};
        std::array<std::uint8_t, 1'024U> slot1{};
        std::array<mms::MmsStaticBrcbSlot, 2U> slots{
            mms::MmsStaticBrcbSlot{slot0},
            mms::MmsStaticBrcbSlot{slot1}};
        mms::MmsStaticBrcbPendingState pending{};
        mms::MmsStaticBrcbRuntime brcb_runtime{
            brcb_definition,
            pending,
            slots,
            pre_brcb_table,
            data_set_table};
        if (!brcb_runtime.initialize()) return 3;
        mms::MmsStaticBrcbControl brcb_control{brcb_runtime};

        std::array<mms::MmsStaticObjectEntry, 28U> final_objects{};
        std::array<mms::MmsStaticBrcbObjectContext, 15U> brcb_contexts{};
        std::array<char, 1'024U> brcb_names{};
        std::uint64_t now = 100U;
        mms::MmsStaticBrcbObjectBank brcb_bank{
            brcb_definition,
            brcb_runtime,
            brcb_control,
            pre_brcb_objects,
            final_objects,
            brcb_contexts,
            brcb_names,
            read_now,
            &now};
        if (!brcb_bank.initialize() || !brcb_bank.table().valid()) return 4;

        mms::MmsStaticDispatchPolicy policy;
        policy.maximum_write_variables = 1U;
        policy.advertise_flattened_child_aliases = true;
        const mms::MmsStaticApplicationDispatcher dispatcher{
            brcb_bank.table(), data_set_table, policy};

        mms::MmsStaticBrcbClientIdentity client;
        client.association_id = 77U;
        client.owner[0] = 0xAAU;
        client.owner[1] = 0x55U;
        client.owner_size = 2U;
        if (brcb_control.reserve(client, 5U, now) != mms::MmsStaticBrcbControlStatus::ok ||
            brcb_control.set_report_enabled(client, true, now) !=
                mms::MmsStaticBrcbControlStatus::ok) {
            return 5;
        }

        mms::MmsReadResponse first_read;
        if (!read_roots(dispatcher, 10U, first_read)) return 6;
        const auto& urcb_value = *first_read.results[0].value;
        const auto& brcb_value = *first_read.results[1].value;
        if (urcb_value.kind() != mms::MmsDataKind::structure ||
            brcb_value.kind() != mms::MmsDataKind::structure ||
            urcb_value.children().size() != kUrcbNames.size() ||
            brcb_value.children().size() != kBrcbNames.size()) {
            return 7;
        }
        const std::span<const std::uint8_t> no_owner;
        if (!string_value(urcb_value.children()[0], "URCB-LIVE") ||
            !bool_value(urcb_value.children()[1], true) ||
            !bool_value(urcb_value.children()[2], true) ||
            !octets_value(urcb_value.children()[11], no_owner)) {
            return 8;
        }
        if (!string_value(brcb_value.children()[0], "BRCB-B1") ||
            !bool_value(brcb_value.children()[1], true) ||
            !octets_value(brcb_value.children()[14], client.owner_view())) {
            return 9;
        }

        mms::MmsVariableAccessAttributesResponse urcb_type;
        mms::MmsVariableAccessAttributesResponse brcb_type;
        if (!read_type(dispatcher, 11U, "LLN0$RP$U1", urcb_type) ||
            !read_type(dispatcher, 12U, "LLN0$BR$B1", brcb_type) ||
            !type_names_match(urcb_type.type, kUrcbNames) ||
            !type_names_match(brcb_type.type, kBrcbNames)) {
            return 10;
        }

        urcb_state.enabled = false;
        urcb_state.reserved = false;
        if (brcb_control.set_report_enabled(client, false, now) !=
                mms::MmsStaticBrcbControlStatus::ok ||
            brcb_control.release(client, now) != mms::MmsStaticBrcbControlStatus::ok) {
            return 11;
        }

        now = 200U;
        mms::MmsReadResponse second_read;
        if (!read_roots(dispatcher, 13U, second_read)) return 12;
        const auto& urcb_after = *second_read.results[0].value;
        const auto& brcb_after = *second_read.results[1].value;
        if (!bool_value(urcb_after.children()[1], false) ||
            !bool_value(urcb_after.children()[2], false) ||
            !bool_value(brcb_after.children()[1], false) ||
            !octets_value(brcb_after.children()[14], no_owner)) {
            return 13;
        }

        return 0;
    } catch (...) {
        return 100;
    }
}
