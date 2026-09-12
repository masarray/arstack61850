// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_span_codec.hpp"
#include "ariec61850/mms/static_direct_control.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ar::iec61850::mms;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

[[nodiscard]] std::vector<std::uint8_t> make_oper(
    const std::int64_t origin_category,
    const bool signed_origin) {
    constexpr std::array<std::uint8_t, 3U> origin_id{'P', '4', 'T'};
    constexpr std::array<std::uint8_t, 1U> no_check{0U};

    const auto origin_value = signed_origin
        ? MmsDataValue::integer(origin_category)
        : MmsDataValue::unsigned_integer(static_cast<std::uint64_t>(origin_category));

    const auto oper = MmsDataValue::structure({
        MmsDataValue::boolean(true),
        MmsDataValue::structure({
            origin_value,
            MmsDataValue::octet_string(origin_id),
        }),
        MmsDataValue::unsigned_integer(1U),
        MmsDataValue::utc_time(Iec61850UtcTime{}),
        MmsDataValue::boolean(false),
        MmsDataValue::bit_string(6U, no_check),
    });

    const auto required = MmsDataSpanCodec::encoded_size(oper);
    CHECK(required.has_value());
    std::vector<std::uint8_t> bytes(*required);
    const auto encoded = MmsDataSpanCodec::encode_into(oper, bytes);
    CHECK(encoded.success());
    CHECK(encoded.bytes_written == bytes.size());
    return bytes;
}

} // namespace

int main() {
    try {
        MmsStaticDirectBooleanOperate decoded{};

        const auto unsigned_oper = make_oper(2, false);
        CHECK(try_decode_static_direct_boolean_operate(unsigned_oper, decoded));
        CHECK(decoded.origin_category == 2U);

        const auto integer_oper = make_oper(2, true);
        CHECK(try_decode_static_direct_boolean_operate(integer_oper, decoded));
        CHECK(decoded.origin_category == 2U);

        const auto negative_integer_oper = make_oper(-1, true);
        CHECK(!try_decode_static_direct_boolean_operate(negative_integer_oper, decoded));

        const auto out_of_range_oper = make_oper(9, true);
        CHECK(try_decode_static_direct_boolean_operate(out_of_range_oper, decoded));
        MmsStaticDirectBooleanControlState state{};
        MmsStaticDirectBooleanControlBinding binding{&state};
        const auto rejected = mms_static_direct_boolean_write_oper(&binding, out_of_range_oper);
        CHECK(!rejected.success);
        CHECK(rejected.failure_code == binding.policy.invalid_value_failure_code);
        CHECK(state.accepted_operations == 0U);
        CHECK(state.rejected_operations == 1U);

        std::cout
            << "IEDSIM_P4_LIBIEC61850_CONTROL_ORCAT_PASS "
            << "unsigned=true integer=true negative_rejected=true range_0_8_guarded=true\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
