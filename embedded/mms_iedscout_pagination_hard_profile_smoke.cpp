// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::size_t kObjectCount = 160U;
constexpr std::size_t kPageSize = 32U;

[[nodiscard]] wire::EncodeResult read_false(
    const void*,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool identifier_equals(
    const std::span<const std::uint8_t> identifier,
    const std::string_view expected) noexcept {
    if (identifier.size() != expected.size()) return false;
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (identifier[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    static_assert(kObjectCount > mms::MmsServiceSpanCodec::maximum_identifiers);

    std::array<std::array<char, 5U>, kObjectCount> names{};
    std::array<mms::MmsStaticObjectEntry, kObjectCount> objects{};
    for (std::size_t index = 0U; index < kObjectCount; ++index) {
        names[index][0] = 'N';
        names[index][1] = static_cast<char>('0' + ((index / 100U) % 10U));
        names[index][2] = static_cast<char>('0' + ((index / 10U) % 10U));
        names[index][3] = static_cast<char>('0' + (index % 10U));
        names[index][4] = '\0';
        objects[index] = mms::MmsStaticObjectEntry{
            "LD0",
            std::string_view{names[index].data(), 4U},
            kBooleanType,
            read_false,
            nullptr};
    }

    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid()) return 1;

    mms::MmsStaticDispatchPolicy policy;
    policy.maximum_names_per_response = kPageSize;
    policy.advertise_flattened_child_aliases = true;
    const mms::MmsStaticApplicationDispatcher dispatcher{table, policy};

    std::array<std::uint8_t, 4096U> response_bytes{};
    std::array<std::uint8_t, 512U> workspace{};
    std::string continue_after;
    std::size_t expected_index = 0U;

    for (std::uint32_t invoke_id = 1U; expected_index < kObjectCount; ++invoke_id) {
        mms::MmsGetNameListRequest request;
        request.invoke_id = invoke_id;
        request.object_class = mms::MmsGetNameListObjectClass::named_variable;
        request.scope = mms::MmsObjectScopeKind::domain_specific;
        request.domain_id = "LD0";
        request.continue_after = continue_after;

        const auto encoded_request = mms::MmsServiceCodec::encode_get_name_list_request_pdu(request);
        const auto dispatched = dispatcher.dispatch(
            std::span<const std::uint8_t>{encoded_request},
            response_bytes,
            workspace);
        if (!dispatched.success() ||
            dispatched.service != mms::MmsWireConfirmedService::get_name_list ||
            dispatched.invoke_id != invoke_id) {
            return 2;
        }

        mms::MmsGetNameListResponseView response;
        if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
                std::span<const std::uint8_t>{response_bytes}.first(dispatched.bytes_written),
                response) ||
            response.invoke_id != invoke_id ||
            response.identifier_count == 0U ||
            response.identifier_count > kPageSize) {
            return 3;
        }

        for (std::size_t page_index = 0U;
             page_index < response.identifier_count;
             ++page_index) {
            if (expected_index >= kObjectCount) return 4;
            std::span<const std::uint8_t> identifier;
            if (!response.try_identifier(page_index, identifier) ||
                !identifier_equals(
                    identifier,
                    std::string_view{names[expected_index].data(), 4U})) {
                return 5;
            }
            ++expected_index;
        }

        const auto should_have_more = expected_index < kObjectCount;
        if (response.more_follows != should_have_more) return 6;
        if (should_have_more) {
            continue_after.assign(
                names[expected_index - 1U].data(),
                4U);
        }
    }

    if (expected_index != kObjectCount) return 7;

    // The continuation is the exact last identifier returned on the previous
    // page, matching the measured IEDScout discovery profile. A missing token
    // must fail deterministically instead of restarting the directory walk.
    mms::MmsGetNameListRequest invalid;
    invalid.invoke_id = 6U;
    invalid.object_class = mms::MmsGetNameListObjectClass::named_variable;
    invalid.scope = mms::MmsObjectScopeKind::domain_specific;
    invalid.domain_id = "LD0";
    invalid.continue_after = "N999";
    const auto invalid_request = mms::MmsServiceCodec::encode_get_name_list_request_pdu(invalid);
    const auto invalid_result = dispatcher.dispatch(
        std::span<const std::uint8_t>{invalid_request},
        response_bytes,
        workspace);
    if (invalid_result.status != mms::MmsStaticDispatchStatus::object_not_found) return 8;

    // Regression for the real IEDScout Report discovery failure: the concrete
    // table only contains RCB leaves, but GetNameList must advertise every
    // virtual hierarchy prefix that synthetic GVAA/Read already understands.
    constexpr std::array<std::string_view, 4U> hierarchy_items{
        "LLN0$RP$U1$RptID",
        "LLN0$RP$U1$RptEna",
        "LLN0$BR$B1$RptID",
        "LLN0$BR$B1$RptEna"};
    std::array<mms::MmsStaticObjectEntry, hierarchy_items.size()> hierarchy_objects{};
    for (std::size_t index = 0U; index < hierarchy_items.size(); ++index) {
        hierarchy_objects[index] = mms::MmsStaticObjectEntry{
            "LD0",
            hierarchy_items[index],
            kBooleanType,
            read_false,
            nullptr};
    }
    const mms::MmsStaticObjectTable hierarchy_table{hierarchy_objects};
    if (!hierarchy_table.valid()) return 9;

    mms::MmsStaticDispatchPolicy hierarchy_policy;
    hierarchy_policy.maximum_names_per_response = 3U;
    hierarchy_policy.advertise_flattened_child_aliases = true;
    const mms::MmsStaticApplicationDispatcher hierarchy_dispatcher{
        hierarchy_table, hierarchy_policy};

    constexpr std::array<std::string_view, 9U> expected_hierarchy{
        "LLN0",
        "LLN0$BR",
        "LLN0$BR$B1",
        "LLN0$BR$B1$RptEna",
        "LLN0$BR$B1$RptID",
        "LLN0$RP",
        "LLN0$RP$U1",
        "LLN0$RP$U1$RptEna",
        "LLN0$RP$U1$RptID"};

    continue_after.clear();
    expected_index = 0U;
    for (std::uint32_t invoke_id = 20U;
         expected_index < expected_hierarchy.size();
         ++invoke_id) {
        mms::MmsGetNameListRequest request;
        request.invoke_id = invoke_id;
        request.object_class = mms::MmsGetNameListObjectClass::named_variable;
        request.scope = mms::MmsObjectScopeKind::domain_specific;
        request.domain_id = "LD0";
        request.continue_after = continue_after;

        const auto encoded_request = mms::MmsServiceCodec::encode_get_name_list_request_pdu(request);
        const auto dispatched = hierarchy_dispatcher.dispatch(
            std::span<const std::uint8_t>{encoded_request},
            response_bytes,
            workspace);
        if (!dispatched.success()) return 10;

        mms::MmsGetNameListResponseView response;
        if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_response(
                std::span<const std::uint8_t>{response_bytes}.first(dispatched.bytes_written),
                response) ||
            response.identifier_count == 0U ||
            response.identifier_count > hierarchy_policy.maximum_names_per_response) {
            return 11;
        }

        for (std::size_t page_index = 0U;
             page_index < response.identifier_count;
             ++page_index) {
            if (expected_index >= expected_hierarchy.size()) return 12;
            std::span<const std::uint8_t> identifier;
            if (!response.try_identifier(page_index, identifier) ||
                !identifier_equals(identifier, expected_hierarchy[expected_index])) {
                return 13;
            }
            ++expected_index;
        }

        const auto should_have_more = expected_index < expected_hierarchy.size();
        if (response.more_follows != should_have_more) return 14;
        if (should_have_more) {
            continue_after.assign(expected_hierarchy[expected_index - 1U]);
        }
    }

    if (expected_index != expected_hierarchy.size()) return 15;

    mms::MmsGetNameListRequest invalid_virtual;
    invalid_virtual.invoke_id = 30U;
    invalid_virtual.object_class = mms::MmsGetNameListObjectClass::named_variable;
    invalid_virtual.scope = mms::MmsObjectScopeKind::domain_specific;
    invalid_virtual.domain_id = "LD0";
    invalid_virtual.continue_after = "LLN0$RP$Missing";
    const auto invalid_virtual_request =
        mms::MmsServiceCodec::encode_get_name_list_request_pdu(invalid_virtual);
    const auto invalid_virtual_result = hierarchy_dispatcher.dispatch(
        std::span<const std::uint8_t>{invalid_virtual_request},
        response_bytes,
        workspace);
    if (invalid_virtual_result.status != mms::MmsStaticDispatchStatus::object_not_found) {
        return 16;
    }

    return 0;
}
