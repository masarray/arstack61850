// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_direct_control.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <algorithm>
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
    MmsStaticDirectBooleanOperate& decoded) noexcept {
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
        or_ident.value.size() > decoded.origin_identifier.size() ||
        offset != origin.value.size()) {
        return false;
    }
    decoded.origin_category = static_cast<std::uint8_t>(*category_value);
    decoded.origin_identifier_size = or_ident.value.size();
    std::copy(or_ident.value.begin(), or_ident.value.end(), decoded.origin_identifier.begin());
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

[[nodiscard]] bool decode_command(
    const std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded,
    const bool require_check) noexcept {
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

    if (!read_next(outer.value, offset, ctl_val) ||
        !decode_boolean(ctl_val, decoded.control_value) ||
        !read_next(outer.value, offset, origin) ||
        !decode_origin(origin, decoded) ||
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
        !context_tag(timestamp, 17, false) || timestamp.value.size() != decoded.timestamp.size()) {
        return false;
    }
    std::copy(timestamp.value.begin(), timestamp.value.end(), decoded.timestamp.begin());
    if (!read_next(outer.value, offset, test) ||
        !decode_boolean(test, decoded.test)) {
        return false;
    }

    if (require_check) {
        asn1::BerTlvView check;
        if (!read_next(outer.value, offset, check) ||
            !decode_check(check, decoded.synchro_check, decoded.interlock_check)) {
            return false;
        }
    }
    return offset == outer.value.size();
}

[[nodiscard]] std::uint64_t binding_now_ms(
    const MmsStaticDirectBooleanControlBinding& binding) noexcept {
    return binding.now_ms == nullptr ? 0U : binding.now_ms(binding.now_context);
}

[[nodiscard]] bool selection_expired(
    const MmsStaticDirectBooleanControlBinding& binding,
    const std::uint64_t now) noexcept {
    if (binding.shared_state == nullptr) return true;
    const auto deadline = binding.shared_state->selection_deadline_ms.load(
        std::memory_order_acquire);
    return deadline != 0U && now >= deadline;
}

void clear_local_selection(MmsStaticDirectBooleanControlBinding& binding) noexcept {
    if (binding.state == nullptr) return;
    binding.state->selected = false;
    binding.state->selected_with_value = false;
    binding.state->selected_command = {};
}

void release_selection(MmsStaticDirectBooleanControlBinding& binding) noexcept {
    if (binding.shared_state != nullptr && binding.association_id != 0U) {
        auto owner = binding.association_id;
        if (binding.shared_state->selected_association_id.compare_exchange_strong(
                owner,
                0U,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            binding.shared_state->selection_deadline_ms.store(0U, std::memory_order_release);
        }
    }
    clear_local_selection(binding);
}

[[nodiscard]] bool acquire_selection(
    MmsStaticDirectBooleanControlBinding& binding) noexcept {
    if (binding.shared_state == nullptr || binding.association_id == 0U ||
        binding.sbo_timeout_ms == 0U) {
        return false;
    }

    const auto now = binding_now_ms(binding);
    for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
        auto owner = binding.shared_state->selected_association_id.load(
            std::memory_order_acquire);
        if (owner == binding.association_id) {
            binding.shared_state->selection_deadline_ms.store(
                now + binding.sbo_timeout_ms,
                std::memory_order_release);
            return true;
        }
        if (owner != 0U) {
            if (!selection_expired(binding, now)) return false;
            if (!binding.shared_state->selected_association_id.compare_exchange_strong(
                    owner,
                    0U,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                continue;
            }
            binding.shared_state->selection_deadline_ms.store(0U, std::memory_order_release);
            continue;
        }
        auto empty = std::uint64_t{0U};
        if (binding.shared_state->selected_association_id.compare_exchange_strong(
                empty,
                binding.association_id,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            binding.shared_state->selection_deadline_ms.store(
                now + binding.sbo_timeout_ms,
                std::memory_order_release);
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool owns_live_selection(
    MmsStaticDirectBooleanControlBinding& binding) noexcept {
    if (binding.shared_state == nullptr || binding.association_id == 0U) return false;
    const auto owner = binding.shared_state->selected_association_id.load(
        std::memory_order_acquire);
    if (owner != binding.association_id) return false;
    if (!selection_expired(binding, binding_now_ms(binding))) return true;
    release_selection(binding);
    return false;
}

[[nodiscard]] bool same_command(
    const MmsStaticDirectBooleanOperate& left,
    const MmsStaticDirectBooleanOperate& right,
    const bool compare_check) noexcept {
    if (left.control_value != right.control_value ||
        left.origin_category != right.origin_category ||
        left.origin_identifier_size != right.origin_identifier_size ||
        left.control_number != right.control_number ||
        left.timestamp != right.timestamp ||
        left.test != right.test) {
        return false;
    }
    if (!std::equal(
            left.origin_identifier.begin(),
            left.origin_identifier.begin() + static_cast<std::ptrdiff_t>(left.origin_identifier_size),
            right.origin_identifier.begin())) {
        return false;
    }
    return !compare_check ||
        (left.synchro_check == right.synchro_check &&
         left.interlock_check == right.interlock_check);
}

[[nodiscard]] bool valid_command(
    const MmsStaticDirectBooleanControlBinding& binding,
    const MmsStaticDirectBooleanOperate& command) noexcept {
    return command.origin_category <= 8U && command.control_number != 0U &&
        (!command.test || binding.policy.allow_test) &&
        (!command.synchro_check || binding.policy.allow_synchro_check) &&
        (!command.interlock_check || binding.policy.allow_interlock_check);
}

[[nodiscard]] wire::EncodeResult encode_unsigned_model(
    const std::uint8_t model,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x86U;
    destination[1] = 0x01U;
    destination[2] = model;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult encode_visible_string(
    const std::string_view value,
    const std::span<std::uint8_t> destination) noexcept {
    std::array<std::uint8_t, 3U> length{};
    std::size_t length_bytes{};
    if (value.size() < 0x80U) {
        length[0] = static_cast<std::uint8_t>(value.size());
        length_bytes = 1U;
    } else if (value.size() <= 0xFFU) {
        length[0] = 0x81U;
        length[1] = static_cast<std::uint8_t>(value.size());
        length_bytes = 2U;
    } else if (value.size() <= 0xFFFFU) {
        length[0] = 0x82U;
        length[1] = static_cast<std::uint8_t>((value.size() >> 8U) & 0xFFU);
        length[2] = static_cast<std::uint8_t>(value.size() & 0xFFU);
        length_bytes = 3U;
    } else {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = 1U + length_bytes + value.size();
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x8AU; // MMS VisibleString data
    std::copy_n(length.begin(), length_bytes, destination.begin() + 1);
    std::copy(value.begin(), value.end(), destination.begin() +
        static_cast<std::ptrdiff_t>(1U + length_bytes));
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] MmsStaticWriteResult association_guard(
    const MmsStaticDirectBooleanControlBinding* binding,
    const MmsStaticRequestAccessContext& access) noexcept {
    if (binding == nullptr || binding->state == nullptr) return {false, 10U};
    if (!access.valid() || binding->association_id == 0U ||
        access.association_id != binding->association_id) {
        return {false, 3U};
    }
    return {true, 0U};
}

[[nodiscard]] MmsStaticWriteResult apply_operate(
    MmsStaticDirectBooleanControlBinding& binding,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    if (binding.state == nullptr) return {false, 10U};

    MmsStaticDirectBooleanOperate operate;
    if (!try_decode_static_direct_boolean_operate(encoded_data, operate)) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.malformed_failure_code};
    }
    if (!valid_command(binding, operate)) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.invalid_value_failure_code};
    }

    const auto requires_selection =
        binding.model == MmsStaticControlModel::sbo_normal ||
        binding.model == MmsStaticControlModel::sbo_enhanced;
    if (requires_selection && !owns_live_selection(binding)) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.temporarily_unavailable_code};
    }
    if (binding.model == MmsStaticControlModel::sbo_enhanced &&
        (!binding.state->selected_with_value ||
         !same_command(binding.state->selected_command, operate, true))) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.invalid_value_failure_code};
    }
    const auto enhanced =
        binding.model == MmsStaticControlModel::direct_enhanced ||
        binding.model == MmsStaticControlModel::sbo_enhanced;
    if (enhanced && binding.state->pending_termination) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.temporarily_unavailable_code};
    }

    if (!operate.test && binding.apply != nullptr &&
        !binding.apply(binding.apply_context, operate.control_value)) {
        ++binding.state->rejected_operations;
        return {false, binding.policy.backend_failure_code};
    }

    if (!operate.test) {
        if (binding.shared_state != nullptr) {
            binding.shared_state->value.store(
                operate.control_value ? 1U : 0U,
                std::memory_order_release);
        }
        binding.state->value = operate.control_value ? 1U : 0U;
    }
    binding.state->last_control_number = operate.control_number;
    binding.state->last_test = operate.test;
    ++binding.state->accepted_operations;

    if (requires_selection) release_selection(binding);
    if (enhanced) {
        binding.state->termination_command = operate;
        binding.state->pending_termination = true;
    }
    return {true, 0U};
}

} // namespace

bool try_decode_static_direct_boolean_operate(
    const std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept {
    return decode_command(encoded_data, decoded, true);
}

bool try_decode_static_boolean_cancel(
    const std::span<const std::uint8_t> encoded_data,
    MmsStaticDirectBooleanOperate& decoded) noexcept {
    return decode_command(encoded_data, decoded, false);
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
    return encode_unsigned_model(1U, destination);
}

wire::EncodeResult mms_static_control_read_ctl_model(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 3U};
    }
    const auto& binding = *static_cast<const MmsStaticDirectBooleanControlBinding*>(context);
    return encode_unsigned_model(static_cast<std::uint8_t>(binding.model), destination);
}

wire::EncodeResult mms_static_sbo_normal_read(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    auto& binding = *const_cast<MmsStaticDirectBooleanControlBinding*>(
        static_cast<const MmsStaticDirectBooleanControlBinding*>(context));
    if (binding.model != MmsStaticControlModel::sbo_normal ||
        binding.state == nullptr || binding.selection_reference.empty()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (!acquire_selection(binding)) {
        return encode_visible_string({}, destination);
    }
    binding.state->selected = true;
    binding.state->selected_with_value = false;
    return encode_visible_string(binding.selection_reference, destination);
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
    if (binding == nullptr) return {false, 10U};
    return apply_operate(*binding, encoded_data);
}

MmsStaticWriteResult mms_static_boolean_write_oper_contextual(
    void* context,
    const std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept {
    auto* binding = static_cast<MmsStaticDirectBooleanControlBinding*>(context);
    const auto guarded = association_guard(binding, access);
    if (!guarded.success) return guarded;
    return apply_operate(*binding, encoded_data);
}

MmsStaticWriteResult mms_static_boolean_write_sbow_contextual(
    void* context,
    const std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept {
    auto* binding = static_cast<MmsStaticDirectBooleanControlBinding*>(context);
    const auto guarded = association_guard(binding, access);
    if (!guarded.success) return guarded;
    if (binding->model != MmsStaticControlModel::sbo_enhanced ||
        binding->state == nullptr) {
        return {false, binding->policy.invalid_value_failure_code};
    }

    MmsStaticDirectBooleanOperate command;
    if (!try_decode_static_direct_boolean_operate(encoded_data, command)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.malformed_failure_code};
    }
    if (!valid_command(*binding, command)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.invalid_value_failure_code};
    }
    if (!acquire_selection(*binding)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.temporarily_unavailable_code};
    }
    binding->state->selected = true;
    binding->state->selected_with_value = true;
    binding->state->selected_command = command;
    return {true, 0U};
}

MmsStaticWriteResult mms_static_boolean_write_cancel_contextual(
    void* context,
    const std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept {
    auto* binding = static_cast<MmsStaticDirectBooleanControlBinding*>(context);
    const auto guarded = association_guard(binding, access);
    if (!guarded.success) return guarded;
    if (binding->model != MmsStaticControlModel::sbo_normal &&
        binding->model != MmsStaticControlModel::sbo_enhanced) {
        return {false, binding->policy.invalid_value_failure_code};
    }

    MmsStaticDirectBooleanOperate command;
    if (!try_decode_static_boolean_cancel(encoded_data, command)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.malformed_failure_code};
    }
    if (!valid_command(*binding, command) || !owns_live_selection(*binding)) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.invalid_value_failure_code};
    }
    if (binding->model == MmsStaticControlModel::sbo_enhanced &&
        (!binding->state->selected_with_value ||
         !same_command(binding->state->selected_command, command, false))) {
        ++binding->state->rejected_operations;
        return {false, binding->policy.invalid_value_failure_code};
    }
    release_selection(*binding);
    return {true, 0U};
}

void mms_static_control_on_association_closed(
    MmsStaticDirectBooleanControlBinding& binding) noexcept {
    release_selection(binding);
    if (binding.state != nullptr) {
        binding.state->pending_termination = false;
        binding.state->termination_command = {};
    }
}

} // namespace ar::iec61850::mms
