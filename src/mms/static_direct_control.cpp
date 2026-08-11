// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_direct_control.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool context_tag(
    const asn1::BerTlvView& tlv,
    const std::int32_t tag,
    const bool constructed) noexcept {
    return tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == tag && tlv.constructed == constructed;
}

[[nodiscard]] bool decode_boolean(
    const asn1::BerTlvView& tlv,
    bool& value) noexcept {
    if (!context_tag(tlv, 3, false) || tlv.value.size() != 1U) {
        return false;
    }
    value = tlv.value.front() != 0U;
    return true;
}

[[nodiscard]] bool read_next(
    const std::span<const std::uint8_t> source,
    std::size_t& offset,
    asn1::BerTlvView& tlv) noexcept {
    return asn1::BerSpanReader::try_read_tlv(source, offset, tlv);
}

[[nodiscard]] bool decode_origin(
    const asn1::BerTlvView& origin,
    std::uint8_t& category) noexcept {
    if (!context_tag(origin, 2, true)) {
        return false;
    }
    std::size_t offset = 0U;
    asn1::BerTlvView or_cat;
    asn1::BerTlvView or_ident;
    if (!read_next(origin.value, offset, or_cat) ||
        !context_tag(or_cat, 6, false)) {
        return false;
    }
    const auto category_value = asn1::BerSpanReader::read_unsigned_integer(or_cat);
    if (!category_value || *category_value > 0xFFU) {
        return false;
    }
    if (!read_next(origin.value, offset, or_ident) ||
        !context_tag(or_ident, 9, false) ||
        or_ident.value.size() > 64U ||
        offset != origin.value.size()) {
        return false;
    }
    category = static_cast<std::uint8_t>(*category_value);
    return true;
}

[[nodiscard]] bool decode_check(
    const asn1::BerTlvView& check,
    bool& synchro,
    bool& interlock) noexcept {
    if (!context_tag(check, 4, false) || check.value.size() != 2U ||
        check.value[0] != 6U || (check.value[1] & 0x3FU) != 0U) {
        return false;
    }
    synchro = (check.value[1] & 0x80U) != 0U;
    interlock = (check.value[1] & 0x40U) != 0U;
    return true;
}

} // namespace

bool try_decode_static_direct_boolean_operate(
    const std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept {
    decoded = {};
    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(encoded_data, outer) ||
        !context_tag(outer, 2, true)) {
        return false;
    }

    std::size_t offset = 0U;
    asn1::BerTlvView ctl_val;
    asn1::BerTlvView origin;
    asn1::BerTlvView ctl_num;
    asn1::BerTlvView timestamp;
    asn1::BerTlvView test;
    asn1::BerTlvView check;

    if (!read_next(outer.value, offset, ctl_val) ||
        !decode_boolean(ctl_val, decoded.control_value) ||
        !read_next(outer.value, offset, origin) ||
        !decode_origin(origin, decoded.origin_category) ||
        !read_next(outer.value, offset, ctl_num) ||
        !context_tag(ctl_num, 6, false)) {
        return false;
    }
    const auto control_number = asn1::BerSpanReader::read_unsigned_integer(ctl_num);
    if (!control_number || *control_number > 0xFFU) {
        return false;
    }
    decoded.control_number = static_cast<std::uint8_t>(*control_number);

    if (!read_next(outer.value, offset, timestamp) ||
        !context_tag(timestamp, 17, false) || timestamp.value.size() != 8U ||
        !read_next(outer.value, offset, test) ||
        !decode_boolean(test, decoded.test) ||
        !read_next(outer.value, offset, check) ||
        !decode_check(check, decoded.synchro_check, decoded.interlock_check) ||
        offset != outer.value.size()) {
        return false;
    }
    return true;
}

wire::EncodeResult mms_static_direct_boolean_read_state(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto* state = static_cast<const MmsStaticDirectBooleanControlState*>(context);
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = state->value != 0U ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

wire::EncodeResult mms_static_direct_normal_read_ctl_model(
    const void*,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x86U; // MMS unsigned
    destination[1] = 0x01U;
    destination[2] = 0x01U; // ctlModel = Direct-with-normal-security
    return {wire::EncodeStatus::ok, required, required};
}

wire::EncodeResult mms_static_control_read_unavailable(
    const void*,
    const std::span<std::uint8_t>) noexcept {
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

MmsStaticWriteResult mms_static_direct_boolean_write_oper(
    void* context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    auto* binding = static_cast<MmsStaticDirectBooleanControlBinding*>(context);
    if (binding == nullptr || binding->state == nullptr) {
        return {false, 10U};
    }

    MmsStaticDirectBooleanOperate operate;
    if (!try_decode_static_direct_boolean_operate(encoded_data, operate)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.malformed_failure_code};
    }
    if (operate.origin_category > 8U || operate.control_number == 0U ||
        (operate.test && !binding->policy.allow_test) ||
        (operate.synchro_check && !binding->policy.allow_synchro_check) ||
        (operate.interlock_check && !binding->policy.allow_interlock_check)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.invalid_value_failure_code};
    }

    if (!operate.test && binding->apply != nullptr &&
        !binding->apply(binding->apply_context, operate.control_value)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.backend_failure_code};
    }

    if (!operate.test) {
        binding->state->value = operate.control_value ? 1U : 0U;
    }
    binding->state->last_control_number = operate.control_number;
    binding->state->last_test = operate.test;
    ++binding->state->accepted_operations;
    return {true, 0U};
}

} // namespace ar::iec61850::mms
