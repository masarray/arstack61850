// SPDX-License-Identifier: GPL-3.0-or-later

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

// ConfirmedRequestPDU invoke=33, GetVariableAccessAttributes(LDX/LLN0).
// This is the shape used by external IEC 61850 client while deciding whether a Logical Node
// exposes service branches such as RP/BR.
constexpr std::array<std::uint8_t, 22U> kLln0AttributesRequest{
    0xA0U, 0x14U, 0x02U, 0x01U, 0x21U,
    0xA6U, 0x0FU,
    0xA0U, 0x0DU,
    0xA1U, 0x0BU,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x58U,
    0x1AU, 0x04U, 0x4CU, 0x4CU, 0x4EU, 0x30U};

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

[[nodiscard]] bool contains_ascii(
    const std::span<const std::uint8_t> bytes,
    const std::string_view text) noexcept {
    if (text.empty() || text.size() > bytes.size()) return false;
    const auto first = std::search(
        bytes.begin(), bytes.end(),
        text.begin(), text.end(),
        [](const std::uint8_t left, const char right) noexcept {
            return left == static_cast<std::uint8_t>(static_cast<unsigned char>(right));
        });
    return first != bytes.end();
}

} // namespace

int main() {
    const bool root_value = true;
    const bool report_enabled = false;

    // The exact LLN0 entry deliberately carries a stale scalar type. Before the
    // interoperability fix, exact lookup short-circuited here and hid the RP
    // descendants even though GetNameList exposed them. The final composed
    // object graph must win for GVAA hierarchy projection.
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LDX", "LLN0", kBooleanType,
            read_boolean, &root_value, false},
        mms::MmsStaticObjectEntry{
            "LDX", "LLN0$RP$Unbuffer01$RptEna", kBooleanType,
            read_boolean, &report_enabled, false}};

    const mms::MmsStaticObjectTable table{objects};
    if (!table.valid()) return 1;

    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    std::array<std::uint8_t, 512U> response{};
    std::array<std::uint8_t, 512U> workspace{};

    const auto dispatched = dispatcher.dispatch(
        kLln0AttributesRequest,
        response,
        workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::get_variable_access_attributes ||
        dispatched.invoke_id != 33U ||
        dispatched.bytes_written <= 14U) {
        return 2;
    }

    const auto encoded = std::span<const std::uint8_t>{response}.first(dispatched.bytes_written);
    if (!contains_ascii(encoded, "RP") ||
        !contains_ascii(encoded, "Unbuffer01") ||
        !contains_ascii(encoded, "RptEna")) {
        return 3;
    }

    return 0;
}
