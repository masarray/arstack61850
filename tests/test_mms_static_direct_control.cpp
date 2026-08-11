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

using namespace ar::iec61850;
using namespace ar::iec61850::mms;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

[[nodiscard]] std::vector<std::uint8_t> make_oper(
    const bool value,
    const std::uint8_t ctl_num,
    const bool test,
    const std::uint8_t check_bits = 0U,
    const std::uint8_t origin_category = 2U) {
    constexpr std::array<std::uint8_t, 3U> origin_id{'H', 'M', 'I'};
    constexpr std::array<std::uint8_t, 1U> no_check{0U};
    const std::array<std::uint8_t, 1U> check_byte{check_bits};

    auto oper = MmsDataValue::structure({
        MmsDataValue::boolean(value),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(origin_category),
            MmsDataValue::octet_string(origin_id),
        }),
        MmsDataValue::unsigned_integer(ctl_num),
        MmsDataValue::utc_time(Iec61850UtcTime{}),
        MmsDataValue::boolean(test),
        MmsDataValue::bit_string(
            6U,
            check_bits == 0U
                ? std::span<const std::uint8_t>{no_check}
                : std::span<const std::uint8_t>{check_byte}),
    });

    const auto required = MmsDataSpanCodec::encoded_size(oper);
    CHECK(required.has_value());
    std::vector<std::uint8_t> bytes(*required);
    const auto encoded = MmsDataSpanCodec::encode_into(oper, bytes);
    CHECK(encoded.success());
    CHECK(encoded.bytes_written == bytes.size());
    return bytes;
}

void valid_oper_updates_live_state() {
    MmsStaticDirectBooleanControlState state{};
    MmsStaticDirectBooleanControlBinding binding{&state};
    const auto bytes = make_oper(true, 7U, false);

    MmsStaticDirectBooleanOperate decoded;
    CHECK(try_decode_static_direct_boolean_operate(bytes, decoded));
    CHECK(decoded.control_value);
    CHECK(decoded.origin_category == 2U);
    CHECK(decoded.control_number == 7U);
    CHECK(!decoded.test);
    CHECK(!decoded.synchro_check);
    CHECK(!decoded.interlock_check);

    const auto result = mms_static_direct_boolean_write_oper(&binding, bytes);
    CHECK(result.success);
    CHECK(state.value == 1U);
    CHECK(state.last_control_number == 7U);
    CHECK(!state.last_test);
    CHECK(state.accepted_operations == 1U);
    CHECK(state.rejected_operations == 0U);
}

void test_oper_is_non_mutating() {
    MmsStaticDirectBooleanControlState state{};
    state.value = 1U;
    MmsStaticDirectBooleanControlBinding binding{&state};
    const auto bytes = make_oper(false, 8U, true);

    const auto result = mms_static_direct_boolean_write_oper(&binding, bytes);
    CHECK(result.success);
    CHECK(state.value == 1U);
    CHECK(state.last_control_number == 8U);
    CHECK(state.last_test);
    CHECK(state.accepted_operations == 1U);
}

void unsupported_check_bits_fail_closed() {
    MmsStaticDirectBooleanControlState state{};
    MmsStaticDirectBooleanControlBinding binding{&state};
    const auto bytes = make_oper(true, 9U, false, 0x80U);

    const auto denied = mms_static_direct_boolean_write_oper(&binding, bytes);
    CHECK(!denied.success);
    CHECK(denied.failure_code == 11U);
    CHECK(state.value == 0U);
    CHECK(state.rejected_operations == 1U);

    binding.policy.allow_synchro_check = true;
    const auto accepted = mms_static_direct_boolean_write_oper(&binding, bytes);
    CHECK(accepted.success);
    CHECK(state.value == 1U);
}

void invalid_shape_and_values_are_rejected() {
    MmsStaticDirectBooleanControlState state{};
    MmsStaticDirectBooleanControlBinding binding{&state};

    auto malformed = make_oper(true, 10U, false);
    malformed.pop_back();
    auto result = mms_static_direct_boolean_write_oper(&binding, malformed);
    CHECK(!result.success);
    CHECK(result.failure_code == 7U);

    result = mms_static_direct_boolean_write_oper(&binding, make_oper(true, 0U, false));
    CHECK(!result.success);
    CHECK(result.failure_code == 11U);

    result = mms_static_direct_boolean_write_oper(&binding, make_oper(true, 11U, false, 0U, 9U));
    CHECK(!result.success);
    CHECK(result.failure_code == 11U);
    CHECK(state.value == 0U);
}

[[nodiscard]] bool reject_backend(void*, bool) noexcept {
    return false;
}

void backend_failure_does_not_publish_state() {
    MmsStaticDirectBooleanControlState state{};
    MmsStaticDirectBooleanControlBinding binding{&state, reject_backend, nullptr};
    const auto result = mms_static_direct_boolean_write_oper(
        &binding, make_oper(true, 12U, false));
    CHECK(!result.success);
    CHECK(result.failure_code == 10U);
    CHECK(state.value == 0U);
    CHECK(state.accepted_operations == 0U);
    CHECK(state.rejected_operations == 1U);
}

void read_callbacks_match_mms_types() {
    MmsStaticDirectBooleanControlState state{};
    state.value = 1U;
    std::array<std::uint8_t, 8U> bytes{};

    auto read = mms_static_direct_boolean_read_state(&state, bytes);
    CHECK(read.success());
    CHECK(read.bytes_written == 3U);
    CHECK(bytes[0] == 0x83U && bytes[1] == 0x01U && bytes[2] == 0xFFU);

    bytes.fill(0U);
    read = mms_static_direct_normal_read_ctl_model(nullptr, bytes);
    CHECK(read.success());
    CHECK(read.bytes_written == 3U);
    CHECK(bytes[0] == 0x86U && bytes[1] == 0x01U && bytes[2] == 0x01U);

    read = mms_static_control_read_unavailable(nullptr, bytes);
    CHECK(!read.success());
}

} // namespace

int main() {
    try {
        valid_oper_updates_live_state();
        test_oper_is_non_mutating();
        unsupported_check_bits_fail_closed();
        invalid_shape_and_values_are_rejected();
        backend_failure_does_not_publish_state();
        read_callbacks_match_mms_types();
        std::cout << "MMS static direct-control tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
