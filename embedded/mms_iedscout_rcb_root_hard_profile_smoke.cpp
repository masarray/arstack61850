// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/mms/static_urcb_objects.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

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
constexpr std::array<std::uint8_t, 3U> kTrue{0x83U, 0x01U, 0xFFU};
constexpr std::array<std::string_view, 11U> kUrcbNames{
    "RptID", "RptEna", "Resv", "DatSet", "ConfRev", "OptFlds",
    "BufTm", "TrgOps", "IntgPd", "GI", "SqNum"};
constexpr std::array<std::string_view, 8U> kBrcbNames{
    "RptID", "RptEna", "DatSet", "ConfRev",
    "PurgeBuf", "EntryID", "ResvTms", "Owner"};

[[nodiscard]] wire::EncodeResult read_true(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, kTrue.size()};
    }
    if (destination.size() < kTrue.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, kTrue.size()};
    }
    std::copy(kTrue.begin(), kTrue.end(), destination.begin());
    return {wire::EncodeStatus::ok, kTrue.size(), kTrue.size()};
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
        const std::array<mms::MmsStaticObjectEntry, 1U> base_objects{
            mms::MmsStaticObjectEntry{
                "LD0", "X1", kBooleanType, read_true, &source_value, false}};
        const mms::MmsStaticObjectTable base_table{base_objects};
        if (!base_table.valid()) return 1;

        const std::array<mms::MmsStaticDataSetMember, 1U> members{
            mms::MmsStaticDataSetMember{"LD0", "X1"}};
        const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
            mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
        const mms::MmsStaticDataSetTable data_set_table{data_sets};
        if (!data_set_table.valid_against(base_table)) return 2;

        constexpr std::array<std::uint8_t, 2U> opt_fields{0x5CU, 0x80U};
        const std::array<mms::MmsStaticUrcbDefinition, 1U> urcb_definitions{
            mms::MmsStaticUrcbDefinition{
                "LD0", "LLN0$RP$U1", "URCB-U1",
                "LD0", "LLN0$Events", 1U, opt_fields, 0U, 0U, 0U}};
        std::array<mms::MmsStaticUrcbState, 1U> urcb_states{};
        mms::MmsStaticUrcbRuntime urcb_runtime{
            urcb_definitions, urcb_states, base_table, data_set_table};
        if (!urcb_runtime.initialize()) return 3;

        std::array<mms::MmsStaticObjectEntry, 12U> urcb_objects{};
        std::array<mms::MmsStaticUrcbObjectContext, 11U> urcb_contexts{};
        std::array<char, 512U> urcb_names{};
        std::uint64_t now = 100U;
        mms::MmsStaticUrcbObjectBank urcb_bank{
            urcb_runtime,
            base_objects,
            urcb_objects,
            urcb_contexts,
            urcb_names,
            read_now,
            &now};
        if (!urcb_bank.initialize() || !urcb_bank.table().valid()) return 4;

        const mms::MmsStaticBrcbDefinition brcb_definition{
            "LD0",
            "LLN0$BR$B1",
            "BRCB-B1",
            "LD0",
            "LLN0$Events",
            7U,
            {0x5CU, 0x80U},
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
            urcb_bank.table(),
            data_set_table};
        if (!brcb_runtime.initialize()) return 5;
        mms::MmsStaticBrcbControl brcb_control{brcb_runtime};

        std::array<mms::MmsStaticObjectEntry, 20U> final_objects{};
        std::array<mms::MmsStaticBrcbObjectContext, 8U> brcb_contexts{};
        std::array<char, 512U> brcb_names{};
        mms::MmsStaticBrcbObjectBank brcb_bank{
            brcb_definition,
            brcb_runtime,
            brcb_control,
            urcb_bank.table().objects(),
            final_objects,
            brcb_contexts,
            brcb_names,
            read_now,
            &now};
        if (!brcb_bank.initialize() || !brcb_bank.table().valid()) return 6;

        mms::MmsStaticDispatchPolicy policy;
        policy.maximum_write_variables = 1U;
        policy.advertise_flattened_child_aliases = true;
        const mms::MmsStaticApplicationDispatcher dispatcher{
            brcb_bank.table(), data_set_table, policy};

        if (urcb_runtime.set_report_id(0U, "URCB-LIVE") != mms::MmsStaticUrcbStatus::ok ||
            urcb_runtime.set_reserved(0U, true) != mms::MmsStaticUrcbStatus::ok ||
            urcb_runtime.set_enabled(0U, true, now) != mms::MmsStaticUrcbStatus::ok) {
            return 7;
        }

        mms::MmsStaticBrcbClientIdentity client;
        client.association_id = 77U;
        client.owner[0] = 0xAAU;
        client.owner[1] = 0x55U;
        client.owner_size = 2U;
        if (brcb_control.reserve(client, 5U, now) != mms::MmsStaticBrcbControlStatus::ok ||
            brcb_control.set_report_enabled(client, true, now) !=
                mms::MmsStaticBrcbControlStatus::ok) {
            return 8;
        }

        mms::MmsReadResponse first_read;
        if (!read_roots(dispatcher, 10U, first_read)) return 9;
        const auto& urcb_value = *first_read.results[0].value;
        const auto& brcb_value = *first_read.results[1].value;
        if (urcb_value.kind() != mms::MmsDataKind::structure ||
            brcb_value.kind() != mms::MmsDataKind::structure ||
            urcb_value.children().size() != kUrcbNames.size() ||
            brcb_value.children().size() != kBrcbNames.size()) {
            return 10;
        }
        if (!string_value(urcb_value.children()[0], "URCB-LIVE") ||
            !bool_value(urcb_value.children()[1], true) ||
            !bool_value(urcb_value.children()[2], true)) {
            return 11;
        }
        if (!string_value(brcb_value.children()[0], "BRCB-B1") ||
            !bool_value(brcb_value.children()[1], true) ||
            !octets_value(brcb_value.children()[7], client.owner_view())) {
            return 12;
        }

        mms::MmsVariableAccessAttributesResponse urcb_type;
        mms::MmsVariableAccessAttributesResponse brcb_type;
        if (!read_type(dispatcher, 11U, "LLN0$RP$U1", urcb_type) ||
            !read_type(dispatcher, 12U, "LLN0$BR$B1", brcb_type) ||
            !type_names_match(urcb_type.type, kUrcbNames) ||
            !type_names_match(brcb_type.type, kBrcbNames)) {
            return 13;
        }

        if (urcb_runtime.set_enabled(0U, false, now) != mms::MmsStaticUrcbStatus::ok ||
            urcb_runtime.set_reserved(0U, false) != mms::MmsStaticUrcbStatus::ok ||
            brcb_control.set_report_enabled(client, false, now) !=
                mms::MmsStaticBrcbControlStatus::ok ||
            brcb_control.release(client, now) != mms::MmsStaticBrcbControlStatus::ok) {
            return 14;
        }

        now = 200U;
        mms::MmsReadResponse second_read;
        if (!read_roots(dispatcher, 13U, second_read)) return 15;
        const auto& urcb_after = *second_read.results[0].value;
        const auto& brcb_after = *second_read.results[1].value;
        if (!bool_value(urcb_after.children()[1], false) ||
            !bool_value(urcb_after.children()[2], false) ||
            !bool_value(brcb_after.children()[1], false) ||
            !octets_value(brcb_after.children()[7], {})) {
            return 16;
        }

        return 0;
    } catch (...) {
        return 100;
    }
}
