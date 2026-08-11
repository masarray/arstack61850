// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {
using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};

constexpr std::array<std::uint8_t, 184U> kAssociationRequest{
    0x0DU,0xB6U,0x05U,0x06U,0x13U,0x01U,0x00U,0x16U,0x01U,0x02U,0x14U,0x02U,
    0x00U,0x02U,0x33U,0x02U,0x00U,0x01U,0x34U,0x02U,0x00U,0x01U,0xC1U,0xA0U,
    0x31U,0x81U,0x9DU,0xA0U,0x03U,0x80U,0x01U,0x01U,0xA2U,0x81U,0x95U,0x81U,
    0x04U,0x00U,0x00U,0x00U,0x01U,0x82U,0x04U,0x00U,0x00U,0x00U,0x01U,0xA4U,
    0x23U,0x30U,0x0FU,0x02U,0x01U,0x01U,0x06U,0x04U,0x52U,0x01U,0x00U,0x01U,
    0x30U,0x04U,0x06U,0x02U,0x51U,0x01U,0x30U,0x10U,0x02U,0x01U,0x03U,0x06U,
    0x05U,0x28U,0xCAU,0x22U,0x02U,0x01U,0x30U,0x04U,0x06U,0x02U,0x51U,0x01U,
    0x61U,0x62U,0x30U,0x60U,0x02U,0x01U,0x01U,0xA0U,0x5BU,0x60U,0x59U,0xA1U,
    0x07U,0x06U,0x05U,0x28U,0xCAU,0x22U,0x02U,0x03U,0xA2U,0x07U,0x06U,0x05U,
    0x29U,0x01U,0x87U,0x67U,0x01U,0xA3U,0x03U,0x02U,0x01U,0x0CU,0xA6U,0x06U,
    0x06U,0x04U,0x29U,0x01U,0x87U,0x67U,0xA7U,0x03U,0x02U,0x01U,0x0CU,0xBEU,
    0x33U,0x28U,0x31U,0x06U,0x02U,0x51U,0x01U,0x02U,0x01U,0x03U,0xA0U,0x28U,
    0xA8U,0x26U,0x80U,0x03U,0x00U,0xFDU,0xE8U,0x81U,0x01U,0x0AU,0x82U,0x01U,
    0x0AU,0x83U,0x01U,0x05U,0xA4U,0x16U,0x80U,0x01U,0x01U,0x81U,0x03U,0x05U,
    0xF1U,0x00U,0x82U,0x0CU,0x03U,0xEEU,0x1CU,0x00U,0x00U,0x04U,0x08U,0x00U,
    0x00U,0x79U,0xEFU,0x18U};

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

[[nodiscard]] std::uint64_t read_now(const void* context) noexcept {
    return context == nullptr ? 0U : *static_cast<const std::uint64_t*>(context);
}

struct LifecycleSink final {
    mms::MmsStaticBrcbControl* control{};
    std::size_t calls{};
    std::uint64_t last_association{};
    std::uint64_t last_now{};
};

void on_association_closed(
    void* raw_context,
    const std::uint64_t association_id,
    const std::uint64_t now_ms) noexcept {
    auto* sink = static_cast<LifecycleSink*>(raw_context);
    if (sink == nullptr) {
        return;
    }
    ++sink->calls;
    sink->last_association = association_id;
    sink->last_now = now_ms;
    if (sink->control != nullptr) {
        sink->control->on_association_closed(association_id, now_ms);
    }
}

[[nodiscard]] mms::MmsStaticBrcbClientIdentity identity(
    const std::uint64_t association,
    const std::array<std::uint8_t, 2U>& owner) noexcept {
    mms::MmsStaticBrcbClientIdentity result;
    result.association_id = association;
    result.owner[0] = owner[0];
    result.owner[1] = owner[1];
    result.owner_size = owner.size();
    return result;
}

[[nodiscard]] mms::MmsStaticConnectionPolicy policy(
    const std::uint64_t association,
    const std::array<std::uint8_t, 2U>& owner,
    std::uint64_t& now,
    LifecycleSink& sink) noexcept {
    mms::MmsStaticConnectionPolicy result;
    result.association_id = association;
    result.owner[0] = owner[0];
    result.owner[1] = owner[1];
    result.owner_size = owner.size();
    result.now_ms = read_now;
    result.now_context = &now;
    result.association_closed = on_association_closed;
    result.association_closed_context = &sink;
    return result;
}

[[nodiscard]] bool establish(
    mms::MmsStaticConnectionRuntime& connection,
    std::span<std::uint8_t> request,
    std::span<std::uint8_t> response,
    std::span<std::uint8_t> workspace,
    std::span<std::uint8_t> scratch) noexcept {
    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{osi::CotpSpanCodec::destination_tsap_parameter, destination_tsap}};

    const auto cr = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, scratch);
    if (!cr.success()) {
        return false;
    }
    const auto cr_tpkt = osi::TpktSpanCodec::encode_into(
        scratch.first(cr.bytes_written), request);
    if (!cr_tpkt.success()) {
        return false;
    }
    auto result = connection.process_tcp_window(
        request.first(cr_tpkt.bytes_written), response, workspace);
    if (!result.response_ready() ||
        connection.state() != mms::MmsStaticConnectionState::awaiting_association) {
        return false;
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(kAssociationRequest, scratch);
    if (!cotp.success()) {
        return false;
    }
    const auto association = osi::TpktSpanCodec::encode_into(
        scratch.first(cotp.bytes_written), request);
    if (!association.success()) {
        return false;
    }
    result = connection.process_tcp_window(
        request.first(association.bytes_written), response, workspace);
    return result.response_ready() &&
        connection.state() == mms::MmsStaticConnectionState::established &&
        connection.association_active() &&
        connection.mms_presentation_context_id() == 3U;
}

[[nodiscard]] bool send_disconnect_request(
    mms::MmsStaticConnectionRuntime& connection,
    std::span<std::uint8_t> request,
    std::span<std::uint8_t> response,
    std::span<std::uint8_t> workspace) noexcept {
    constexpr std::array<std::uint8_t, 7U> cotp_disconnect{
        0x06U, 0x80U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U};
    const auto tpkt = osi::TpktSpanCodec::encode_into(cotp_disconnect, request);
    if (!tpkt.success()) {
        return false;
    }
    const auto result = connection.process_tcp_window(
        request.first(tpkt.bytes_written), response, workspace);
    return result.status == mms::MmsStaticConnectionStatus::closed &&
        result.state == mms::MmsStaticConnectionState::closed &&
        result.consumed_bytes == tpkt.bytes_written &&
        !connection.association_active();
}

} // namespace

int main() {
    bool value = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "X1", kBooleanType, read_boolean, &value, false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_set_table};

    const mms::MmsStaticBrcbDefinition definition{
        "LD0",
        "LLN0$BR$Events",
        "LD0/LLN0$BR$Events",
        "LD0",
        "LLN0$Events",
        1U,
        {0x7FU, 0x80U},
        0U,
        0x70U};
    std::array<std::uint8_t, 512U> slot_storage{};
    std::array<mms::MmsStaticBrcbSlot, 1U> slots{
        mms::MmsStaticBrcbSlot{slot_storage}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, object_table, data_set_table};
    if (!reports.initialize()) {
        return 1;
    }
    mms::MmsStaticBrcbControl control{reports};
    LifecycleSink sink{&control};

    const std::array<std::uint8_t, 2U> owner_a{0xAAU, 0x01U};
    const std::array<std::uint8_t, 2U> owner_b{0xBBU, 0x01U};
    const auto a1 = identity(101U, owner_a);
    const auto a2 = identity(102U, owner_a);
    const auto a3 = identity(103U, owner_a);
    const auto b1 = identity(201U, owner_b);

    std::array<std::uint8_t, 2048U> request{};
    std::array<std::uint8_t, 2048U> response{};
    std::array<std::uint8_t, 2048U> workspace{};
    std::array<std::uint8_t, 2048U> scratch{};

    std::uint64_t now = 100U;
    mms::MmsStaticConnectionRuntime first{
        dispatcher, policy(a1.association_id, owner_a, now, sink)};
    if (!establish(first, request, response, workspace, scratch) ||
        control.reserve(a1, 5U, now) != mms::MmsStaticBrcbControlStatus::ok ||
        control.set_report_enabled(a1, true, now) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 2;
    }

    // TCP EOF/socket/local close must notify exactly once and retain a timed
    // reservation while always disabling report transmission.
    now = 1'000U;
    first.close_transport();
    auto state = control.state(now);
    if (first.state() != mms::MmsStaticConnectionState::closed ||
        first.association_active() || sink.calls != 1U ||
        sink.last_association != 101U || sink.last_now != now ||
        reports.enabled() || !state.reserved || state.owner_connected ||
        state.association_id != 0U || state.reservation_expires_at_ms != 6'000U) {
        return 3;
    }
    first.close_transport();
    if (sink.calls != 1U) {
        return 4;
    }

    now = 2'000U;
    if (control.reserve(b1, 5U, now) !=
        mms::MmsStaticBrcbControlStatus::object_access_denied) {
        return 5;
    }

    // The same stable Owner may reconnect with a new ephemeral association.
    now = 2'500U;
    if (control.reserve(a2, 5U, now) != mms::MmsStaticBrcbControlStatus::ok) {
        return 6;
    }
    mms::MmsStaticConnectionRuntime second{
        dispatcher, policy(a2.association_id, owner_a, now, sink)};
    if (!establish(second, request, response, workspace, scratch) ||
        control.set_report_enabled(a2, true, now) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 7;
    }

    // Runtime reuse/reset is also a transport teardown and must deliver one
    // lifecycle event before allowing a fresh COTP connection.
    now = 3'000U;
    second.reset();
    state = control.state(now);
    if (sink.calls != 2U || sink.last_association != 102U ||
        sink.last_now != now || reports.enabled() ||
        second.state() != mms::MmsStaticConnectionState::awaiting_cotp_connect ||
        second.association_active() || !state.reserved || state.owner_connected ||
        state.reservation_expires_at_ms != 8'000U) {
        return 8;
    }
    second.reset();
    if (sink.calls != 2U) {
        return 9;
    }

    // Reclaim with ResvTms=0 and prove an incoming COTP DR drives immediate
    // release through the exact same generic lifecycle bridge.
    now = 3'500U;
    if (control.reserve(a3, 0U, now) != mms::MmsStaticBrcbControlStatus::ok) {
        return 10;
    }
    mms::MmsStaticConnectionRuntime third{
        dispatcher, policy(a3.association_id, owner_a, now, sink)};
    if (!establish(third, request, response, workspace, scratch) ||
        control.set_report_enabled(a3, true, now) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 11;
    }

    now = 3'600U;
    if (!send_disconnect_request(third, request, response, workspace)) {
        return 12;
    }
    state = control.state(now);
    if (sink.calls != 3U || sink.last_association != 103U ||
        sink.last_now != now || reports.enabled() || state.reserved ||
        state.owner_connected || !state.owner.empty()) {
        return 13;
    }
    third.close_transport();
    if (sink.calls != 3U) {
        return 14;
    }

    return 0;
}
