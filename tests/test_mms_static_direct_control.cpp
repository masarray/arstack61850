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

struct TestClock final {
    std::uint64_t now_ms{};
};

[[nodiscard]] std::uint64_t test_now_ms(const void* context) noexcept {
    return context == nullptr ? 0U : static_cast<const TestClock*>(context)->now_ms;
}

[[nodiscard]] MmsStaticRequestAccessContext access_for(const std::uint64_t association_id) {
    static constexpr std::array<std::uint8_t, 1U> owner{0xA5U};
    return {association_id, owner};
}

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

[[nodiscard]] std::vector<std::uint8_t> make_cancel(
    const bool value,
    const std::uint8_t ctl_num,
    const bool test,
    const std::uint8_t origin_category = 2U) {
    constexpr std::array<std::uint8_t, 3U> origin_id{'H', 'M', 'I'};
    auto cancel = MmsDataValue::structure({
        MmsDataValue::boolean(value),
        MmsDataValue::structure({
            MmsDataValue::unsigned_integer(origin_category),
            MmsDataValue::octet_string(origin_id),
        }),
        MmsDataValue::unsigned_integer(ctl_num),
        MmsDataValue::utc_time(Iec61850UtcTime{}),
        MmsDataValue::boolean(test),
    });
    const auto required = MmsDataSpanCodec::encoded_size(cancel);
    CHECK(required.has_value());
    std::vector<std::uint8_t> bytes(*required);
    const auto encoded = MmsDataSpanCodec::encode_into(cancel, bytes);
    CHECK(encoded.success());
    return bytes;
}

[[nodiscard]] MmsStaticDirectBooleanControlBinding make_binding(
    MmsStaticDirectBooleanControlState& state,
    MmsStaticDirectBooleanSharedState& shared,
    const MmsStaticControlModel model,
    const std::uint64_t association_id,
    TestClock& clock) {
    MmsStaticDirectBooleanControlBinding binding;
    binding.state = &state;
    binding.shared_state = &shared;
    binding.model = model;
    binding.association_id = association_id;
    binding.selection_reference = "LD0/GGIO1.SPCSO1";
    binding.sbo_timeout_ms = 100U;
    binding.now_ms = test_now_ms;
    binding.now_context = &clock;
    return binding;
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

void sbo_normal_enforces_owner_cancel_and_timeout() {
    TestClock clock{10U};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state_a{};
    MmsStaticDirectBooleanControlState state_b{};
    auto a = make_binding(state_a, shared, MmsStaticControlModel::sbo_normal, 11U, clock);
    auto b = make_binding(state_b, shared, MmsStaticControlModel::sbo_normal, 22U, clock);
    std::array<std::uint8_t, 64U> selected{};

    const auto first = mms_static_sbo_normal_read(&a, selected);
    CHECK(first.success());
    CHECK(first.bytes_written > 2U);
    CHECK(selected[0] == 0x8AU);
    CHECK(state_a.selected);

    selected.fill(0U);
    const auto contended = mms_static_sbo_normal_read(&b, selected);
    CHECK(contended.success());
    CHECK(contended.bytes_written == 2U);
    CHECK(selected[0] == 0x8AU && selected[1] == 0U);

    const auto denied = mms_static_boolean_write_oper_contextual(
        &b, make_oper(true, 7U, false), access_for(22U));
    CHECK(!denied.success);
    CHECK(denied.failure_code == 2U);

    const auto cancelled = mms_static_boolean_write_cancel_contextual(
        &a, make_cancel(false, 8U, false), access_for(11U));
    CHECK(cancelled.success);
    CHECK(shared.selected_association_id.load() == 0U);

    CHECK(mms_static_sbo_normal_read(&a, selected).success());
    clock.now_ms = 111U;
    selected.fill(0U);
    const auto takeover = mms_static_sbo_normal_read(&b, selected);
    CHECK(takeover.success());
    CHECK(selected[1] != 0U);
    CHECK(shared.selected_association_id.load() == 22U);

    const auto operated = mms_static_boolean_write_oper_contextual(
        &b, make_oper(true, 9U, false), access_for(22U));
    CHECK(operated.success);
    CHECK(shared.value.load() == 1U);
    CHECK(shared.selected_association_id.load() == 0U);
}

void direct_enhanced_queues_one_termination() {
    TestClock clock{};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::direct_enhanced, 31U, clock);

    const auto first = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(true, 12U, false), access_for(31U));
    CHECK(first.success);
    CHECK(state.pending_termination);
    CHECK(state.termination_command.control_number == 12U);
    CHECK(shared.value.load() == 1U);

    const auto second = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(false, 13U, false), access_for(31U));
    CHECK(!second.success);
    CHECK(second.failure_code == 2U);
    CHECK(shared.value.load() == 1U);
}

void sbo_enhanced_requires_exact_selected_sequence() {
    TestClock clock{50U};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::sbo_enhanced, 41U, clock);

    const auto selected = mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 21U, false), access_for(41U));
    CHECK(selected.success);
    CHECK(state.selected && state.selected_with_value);
    CHECK(shared.selected_association_id.load() == 41U);

    const auto mismatch = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(false, 21U, false), access_for(41U));
    CHECK(!mismatch.success);
    CHECK(mismatch.failure_code == 11U);
    CHECK(shared.value.load() == 0U);

    const auto operated = mms_static_boolean_write_oper_contextual(
        &binding, make_oper(true, 21U, false), access_for(41U));
    CHECK(operated.success);
    CHECK(shared.value.load() == 1U);
    CHECK(state.pending_termination);
    CHECK(shared.selected_association_id.load() == 0U);
}

void sbo_enhanced_cancel_and_association_close_release_owner() {
    TestClock clock{};
    MmsStaticDirectBooleanSharedState shared{};
    MmsStaticDirectBooleanControlState state{};
    auto binding = make_binding(
        state, shared, MmsStaticControlModel::sbo_enhanced, 51U, clock);

    CHECK(mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 30U, false), access_for(51U)).success);
    CHECK(mms_static_boolean_write_cancel_contextual(
        &binding, make_cancel(true, 30U, false), access_for(51U)).success);
    CHECK(shared.selected_association_id.load() == 0U);
    CHECK(shared.value.load() == 0U);

    CHECK(mms_static_boolean_write_sbow_contextual(
        &binding, make_oper(true, 31U, false), access_for(51U)).success);
    mms_static_control_on_association_closed(binding);
    CHECK(shared.selected_association_id.load() == 0U);
    CHECK(!state.selected);
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

    MmsStaticDirectBooleanControlState configured_state{};
    MmsStaticDirectBooleanControlBinding configured_binding;
    configured_binding.state = &configured_state;
    configured_binding.model = MmsStaticControlModel::sbo_enhanced;
    bytes.fill(0U);
    read = mms_static_control_read_ctl_model(&configured_binding, bytes);
    CHECK(read.success());
    CHECK(bytes[0] == 0x86U && bytes[1] == 0x01U && bytes[2] == 0x04U);

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
        sbo_normal_enforces_owner_cancel_and_timeout();
        direct_enhanced_queues_one_termination();
        sbo_enhanced_requires_exact_selected_sequence();
        sbo_enhanced_cancel_and_association_close_release_owner();
        read_callbacks_match_mms_types();
        std::cout << "MMS static direct-control tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
