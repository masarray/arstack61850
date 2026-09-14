// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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
    const std::array<char, 5U>& expected) noexcept {
    if (identifier.size() != 4U) return false;
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (identifier[index] != static_cast<std::uint8_t>(expected[index])) {
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
                !identifier_equals(identifier, names[expected_index])) {
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

    return 0;
}
