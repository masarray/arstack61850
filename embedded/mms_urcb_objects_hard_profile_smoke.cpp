// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/mms/static_urcb_objects.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kTrueData{0x83U, 0x01U, 0xFFU};
constexpr std::array<std::uint8_t, 3U> kFalseData{0x83U, 0x01U, 0x00U};
constexpr std::array<std::uint8_t, 10U> kCustomReportId{
    0x8AU, 0x08U,
    'C', 'U', 'S', 'T', 'O', 'M', '0', '1'};

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
}

[[nodiscard]] mms::MmsObjectNameView object_name(
    const std::string_view domain,
    const std::string_view item) noexcept {
    return {
        mms::MmsObjectNameViewKind::domain_specific,
        as_bytes(domain),
        as_bytes(item)};
}

[[nodiscard]] std::uint64_t fake_now_ms(const void* context) noexcept {
    return context == nullptr
        ? std::uint64_t{0U}
        : *static_cast<const std::uint64_t*>(context);
}

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

[[nodiscard]] std::optional<std::size_t> definition_size(
    const std::string_view domain,
    const std::string_view item) noexcept {
    const auto domain_tlv = asn1::BerSpanWriter::tlv_size(26, domain.size());
    const auto item_tlv = asn1::BerSpanWriter::tlv_size(26, item.size());
    if (!domain_tlv || !item_tlv ||
        *item_tlv > std::numeric_limits<std::size_t>::max() - *domain_tlv) {
        return std::nullopt;
    }
    const auto object_content = *domain_tlv + *item_tlv;
    const auto object = asn1::BerSpanWriter::tlv_size(1, object_content);
    if (!object) {
        return std::nullopt;
    }
    const auto name_wrapper = asn1::BerSpanWriter::tlv_size(0, *object);
    if (!name_wrapper) {
        return std::nullopt;
    }
    return asn1::BerSpanWriter::tlv_size(16, *name_wrapper);
}

[[nodiscard]] bool write_definition(
    asn1::BerSpanWriter& writer,
    const std::string_view domain,
    const std::string_view item) noexcept {
    const auto domain_tlv = asn1::BerSpanWriter::tlv_size(26, domain.size());
    const auto item_tlv = asn1::BerSpanWriter::tlv_size(26, item.size());
    if (!domain_tlv || !item_tlv) {
        return false;
    }
    const auto object_content = *domain_tlv + *item_tlv;
    const auto object = asn1::BerSpanWriter::tlv_size(1, object_content);
    const auto name_wrapper = object
        ? asn1::BerSpanWriter::tlv_size(0, *object)
        : std::optional<std::size_t>{};
    if (!object || !name_wrapper) {
        return false;
    }
    return writer.write_tlv_header(
               asn1::BerClass::universal, true, 16, *name_wrapper) &&
        writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *object) &&
        writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, object_content) &&
        writer.write_tlv(
            asn1::BerClass::universal, false, 26, as_bytes(domain)) &&
        writer.write_tlv(
            asn1::BerClass::universal, false, 26, as_bytes(item));
}

[[nodiscard]] wire::EncodeResult encode_single_read_request(
    const std::uint32_t invoke_id,
    const std::string_view domain,
    const std::string_view item,
    const std::span<std::uint8_t> destination) noexcept {
    const auto definition = definition_size(domain, item);
    const auto list = definition
        ? asn1::BerSpanWriter::tlv_size(0, *definition)
        : std::optional<std::size_t>{};
    const auto specification = list
        ? asn1::BerSpanWriter::tlv_size(1, *list)
        : std::optional<std::size_t>{};
    if (!definition || !list || !specification || *specification > 256U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::array<std::uint8_t, 256U> service{};
    asn1::BerSpanWriter writer{service};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *list) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *definition) ||
        !write_definition(writer, domain, item) || writer.size() != *specification) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    return mms::MmsPduSpanCodec::encode_confirmed_request_into(
        invoke_id,
        static_cast<std::int32_t>(mms::MmsWireConfirmedService::read),
        true,
        std::span<const std::uint8_t>{service}.first(writer.size()),
        destination);
}

[[nodiscard]] wire::EncodeResult encode_single_write_request(
    const std::uint32_t invoke_id,
    const std::string_view domain,
    const std::string_view item,
    const std::span<const std::uint8_t> encoded_data,
    const std::span<std::uint8_t> destination) noexcept {
    const auto definition = definition_size(domain, item);
    const auto list = definition
        ? asn1::BerSpanWriter::tlv_size(0, *definition)
        : std::optional<std::size_t>{};
    const auto data_list = asn1::BerSpanWriter::tlv_size(0, encoded_data.size());
    if (!definition || !list || !data_list ||
        *data_list > std::numeric_limits<std::size_t>::max() - *list ||
        *list + *data_list > 256U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::array<std::uint8_t, 256U> service{};
    asn1::BerSpanWriter writer{service};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *definition) ||
        !write_definition(writer, domain, item) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, encoded_data.size()) ||
        !writer.write_bytes(encoded_data) || writer.size() != *list + *data_list) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    return mms::MmsPduSpanCodec::encode_confirmed_request_into(
        invoke_id,
        static_cast<std::int32_t>(mms::MmsWireConfirmedService::write),
        true,
        std::span<const std::uint8_t>{service}.first(writer.size()),
        destination);
}

} // namespace

int main() {
    bool relay_state = true;
    bool alarm_state = false;
    const std::array<mms::MmsStaticObjectEntry, 2U> base_objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType, read_boolean, &relay_state, false},
        mms::MmsStaticObjectEntry{
            "LD0", "A1", kBooleanType, read_boolean, &alarm_state, false}};
    const mms::MmsStaticObjectTable base_table{base_objects};

    const std::array<mms::MmsStaticDataSetMember, 2U> members{
        mms::MmsStaticDataSetMember{"LD0", "R1"},
        mms::MmsStaticDataSetMember{"LD0", "A1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_sets{data_set_entries};

    const std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{
        mms::MmsStaticUrcbDefinition{
            "LD0",
            "LLN0$RP$Events",
            "LD0/LLN0$RP$Events",
            "LD0",
            "LLN0$Events",
            7U,
            {0x7CU, 0x80U},
            0U,
            0x08U,
            1'000U}};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    mms::MmsStaticUrcbRuntime runtime{
        definitions, states, base_table, data_sets};
    if (!runtime.initialize()) {
        return 1;
    }

    std::uint64_t now_ms = 100U;
    std::array<mms::MmsStaticObjectEntry, 13U> object_storage{};
    std::array<mms::MmsStaticUrcbObjectContext, 11U> context_storage{};
    std::array<char, 512U> name_storage{};
    mms::MmsStaticUrcbObjectBank bank{
        runtime,
        base_objects,
        object_storage,
        context_storage,
        name_storage,
        fake_now_ms,
        &now_ms};
    if (bank.required_object_capacity() != object_storage.size() ||
        bank.required_context_capacity() != context_storage.size() ||
        bank.required_name_bytes() == 0U ||
        bank.required_name_bytes() > name_storage.size() ||
        !bank.initialize() || !bank.valid() ||
        bank.object_count() != object_storage.size() || !bank.table().valid()) {
        return 2;
    }

    const auto rpt_ena_name = object_name("LD0", "LLN0$RP$Events$RptEna");
    const auto rpt_id_name = object_name("LD0", "LLN0$RP$Events$RptID");
    const auto gi_name = object_name("LD0", "LLN0$RP$Events$GI");
    const auto sq_num_name = object_name("LD0", "LLN0$RP$Events$SqNum");
    const auto* rpt_ena = bank.table().find(rpt_ena_name);
    const auto* rpt_id = bank.table().find(rpt_id_name);
    const auto* gi = bank.table().find(gi_name);
    const auto* sq_num = bank.table().find(sq_num_name);
    if (rpt_ena == nullptr || rpt_id == nullptr || gi == nullptr || sq_num == nullptr ||
        !rpt_ena->writable() || !rpt_id->writable() || !gi->writable() ||
        sq_num->writable()) {
        return 3;
    }

    std::array<std::uint8_t, 64U> read_buffer{};
    auto read = rpt_ena->read(rpt_ena->context, read_buffer);
    if (!read.success() || read.bytes_written != kFalseData.size() ||
        !std::equal(
            read_buffer.begin(),
            read_buffer.begin() + static_cast<std::ptrdiff_t>(read.bytes_written),
            kFalseData.begin())) {
        return 4;
    }
    std::array<std::uint8_t, 2U> tiny{};
    read = rpt_ena->read(rpt_ena->context, tiny);
    if (read.status != wire::EncodeStatus::buffer_too_small || read.required_bytes != 3U) {
        return 5;
    }

    const auto report_id_write = rpt_id->write(rpt_id->write_context, kCustomReportId);
    const auto* state = runtime.state(0U);
    if (!report_id_write.success || state == nullptr || state->report_id() != "CUSTOM01") {
        return 6;
    }

    mms::MmsStaticApplicationDispatcher dispatcher{bank.table(), data_sets};
    std::array<std::uint8_t, 512U> request{};
    std::array<std::uint8_t, 512U> response{};
    std::array<std::uint8_t, 512U> workspace{};

    const auto enable_request = encode_single_write_request(
        21U,
        "LD0",
        "LLN0$RP$Events$RptEna",
        kTrueData,
        request);
    if (!enable_request.success()) {
        return 7;
    }
    auto dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(enable_request.bytes_written),
        response,
        workspace);
    mms::MmsWriteResponseView write_response;
    mms::MmsWriteAccessResultView write_result;
    state = runtime.state(0U);
    if (!dispatched.success() || dispatched.service != mms::MmsWireConfirmedService::write ||
        !mms::MmsServiceSpanCodec::try_decode_write_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            write_response) ||
        !write_response.try_result(0U, write_result) || !write_result.success ||
        state == nullptr || !state->enabled || !state->integrity_armed ||
        state->next_integrity_due_ms != 1'100U) {
        return 8;
    }

    const auto read_request = encode_single_read_request(
        22U,
        "LD0",
        "LLN0$RP$Events$RptEna",
        request);
    if (!read_request.success()) {
        return 9;
    }
    dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(read_request.bytes_written),
        response,
        workspace);
    mms::MmsReadResponseView read_response;
    mms::MmsReadAccessResultView access;
    if (!dispatched.success() ||
        !mms::MmsServiceSpanCodec::try_decode_read_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            read_response) ||
        !read_response.try_result(0U, access) || !access.success ||
        access.encoded_data.size() != kTrueData.size() ||
        !std::equal(access.encoded_data.begin(), access.encoded_data.end(), kTrueData.begin())) {
        return 10;
    }

    const auto denied_request = encode_single_write_request(
        23U,
        "LD0",
        "LLN0$RP$Events$RptID",
        kCustomReportId,
        request);
    if (!denied_request.success()) {
        return 11;
    }
    dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(denied_request.bytes_written),
        response,
        workspace);
    if (!dispatched.success() ||
        !mms::MmsServiceSpanCodec::try_decode_write_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            write_response) ||
        !write_response.try_result(0U, write_result) || write_result.success ||
        write_result.failure_code != 3U) {
        return 12;
    }

    const auto gi_request = encode_single_write_request(
        24U,
        "LD0",
        "LLN0$RP$Events$GI",
        kTrueData,
        request);
    if (!gi_request.success()) {
        return 13;
    }
    dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(gi_request.bytes_written),
        response,
        workspace);
    state = runtime.state(0U);
    if (!dispatched.success() ||
        !mms::MmsServiceSpanCodec::try_decode_write_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            write_response) ||
        !write_response.try_result(0U, write_result) || !write_result.success ||
        state == nullptr || !state->general_interrogation_pending) {
        return 14;
    }

    mms::MmsStaticUrcbEmissionPlan plan;
    if (!runtime.next_due(now_ms, plan) ||
        plan.reason != mms::MmsStaticUrcbReportReason::general_interrogation ||
        plan.sequence_number != 1U) {
        return 15;
    }

    now_ms = 200U;
    const auto disable_request = encode_single_write_request(
        25U,
        "LD0",
        "LLN0$RP$Events$RptEna",
        kFalseData,
        request);
    if (!disable_request.success()) {
        return 16;
    }
    dispatched = dispatcher.dispatch(
        std::span<const std::uint8_t>{request}.first(disable_request.bytes_written),
        response,
        workspace);
    state = runtime.state(0U);
    if (!dispatched.success() || state == nullptr || state->enabled ||
        state->general_interrogation_pending || state->integrity_armed) {
        return 17;
    }

    const auto gi_while_disabled = gi->write(gi->write_context, kTrueData);
    if (gi_while_disabled.success || gi_while_disabled.failure_code != 2U) {
        return 18;
    }

    read = sq_num->read(sq_num->context, read_buffer);
    if (!read.success() || read.bytes_written != 3U ||
        read_buffer[0] != 0x86U || read_buffer[1] != 0x01U || read_buffer[2] != 0x00U) {
        return 19;
    }

    // Storage shortages fail closed without publishing a partial object table.
    std::array<mms::MmsStaticObjectEntry, 12U> short_objects{};
    mms::MmsStaticUrcbObjectBank short_bank{
        runtime,
        base_objects,
        short_objects,
        context_storage,
        name_storage,
        fake_now_ms,
        &now_ms};
    if (short_bank.initialize() || short_bank.valid()) {
        return 20;
    }

    return 0;
}
