// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_dispatcher.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool span_equals(
    const std::span<const std::uint8_t> bytes,
    const std::string_view text) noexcept {
    if (bytes.size() != text.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_mms_data(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView data;
    if (!asn1::BerSpanReader::try_read_exact(encoded, data) ||
        data.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (data.tag_number == 1 || data.tag_number == 2) {
        return data.constructed;
    }
    return data.tag_number >= 3 && data.tag_number <= 17 && !data.constructed;
}

[[nodiscard]] MmsStaticDispatchResult make_status(
    const MmsStaticDispatchStatus status,
    const MmsConfirmedPduView& request,
    const std::size_t required_bytes = 0U) noexcept {
    return MmsStaticDispatchResult{
        status,
        request.service(),
        request.invoke_id,
        0U,
        required_bytes};
}

[[nodiscard]] MmsStaticDispatchResult make_encoded(
    const MmsConfirmedPduView& request,
    const wire::EncodeResult encoded) noexcept {
    if (encoded.success()) {
        return MmsStaticDispatchResult{
            MmsStaticDispatchStatus::response_ready,
            request.service(),
            request.invoke_id,
            encoded.bytes_written,
            encoded.required_bytes};
    }
    if (encoded.status == wire::EncodeStatus::buffer_too_small) {
        return make_status(
            MmsStaticDispatchStatus::response_buffer_too_small,
            request,
            encoded.required_bytes);
    }
    return make_status(MmsStaticDispatchStatus::backend_failure, request);
}

[[nodiscard]] bool policy_valid(const MmsStaticDispatchPolicy& policy) noexcept {
    return policy.maximum_names_per_response > 0U &&
        policy.maximum_names_per_response <= MmsServiceSpanCodec::maximum_identifiers &&
        policy.maximum_write_variables > 0U &&
        policy.maximum_write_variables <= MmsServiceSpanCodec::maximum_variables;
}

[[nodiscard]] bool append_unique(
    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers>& names,
    std::size_t& count,
    const std::string_view value) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        if (names[index] == value) {
            return true;
        }
    }
    if (count >= names.size()) {
        return false;
    }
    names[count++] = value;
    return true;
}

// IEC 61850 engineering clients discover each Logical Node as one MMS
// NamedVariable and then walk its hierarchical TypeSpecification. Static
// profiles also keep flattened leaf aliases in the table so Read/Write can
// resolve exact FC/DO/DA paths. Do not advertise those aliases as additional
// top-level NamedVariables when their root Logical Node object is present.
//
// The fallback is intentional: a generic MMS profile that only supplies flat
// names (and no corresponding root entry) keeps the legacy directory behavior.
[[nodiscard]] bool is_flattened_child_with_root(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view item) noexcept {
    const auto separator = item.find('$');
    if (separator == std::string_view::npos || separator == 0U) {
        return false;
    }
    const auto root = item.substr(0U, separator);
    for (const auto& candidate : objects.objects()) {
        if (candidate.domain == domain && candidate.item == root) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t collect_names(
    const MmsStaticObjectTable& objects,
    const MmsStaticDataSetTable& data_sets,
    const MmsStaticDispatchPolicy& policy,
    const MmsGetNameListRequestView& request,
    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers>& names) noexcept {
    std::size_t count = 0U;
    if (request.object_class == MmsNameListObjectClass::domain &&
        request.scope == MmsNameScopeKind::vmd_specific) {
        for (const auto& object : objects.objects()) {
            if (!append_unique(names, count, object.domain)) {
                return names.size() + 1U;
            }
        }
        for (const auto& data_set : data_sets.data_sets()) {
            if (!append_unique(names, count, data_set.domain)) {
                return names.size() + 1U;
            }
        }
        return count;
    }

    if (request.object_class == MmsNameListObjectClass::named_variable &&
        request.scope == MmsNameScopeKind::domain_specific) {
        for (const auto& object : objects.objects()) {
            if (!span_equals(request.domain_id, object.domain) ||
                (!policy.advertise_flattened_child_aliases &&
                 is_flattened_child_with_root(objects, object.domain, object.item))) {
                continue;
            }
            if (!append_unique(names, count, object.item)) {
                return names.size() + 1U;
            }
        }
        return count;
    }

    if (request.object_class == MmsNameListObjectClass::named_variable_list &&
        request.scope == MmsNameScopeKind::domain_specific) {
        for (const auto& data_set : data_sets.data_sets()) {
            if (span_equals(request.domain_id, data_set.domain)) {
                if (count >= names.size()) {
                    return names.size() + 1U;
                }
                names[count++] = data_set.item;
            }
        }
        return count;
    }

    return names.size() + 1U;
}

[[nodiscard]] MmsStaticDispatchResult dispatch_get_name_list(
    const MmsStaticObjectTable& objects,
    const MmsStaticDataSetTable& data_sets,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response) noexcept {
    MmsGetNameListRequestView request;
    if (!MmsServiceSpanCodec::try_decode_get_name_list_request(confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }

    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers> names{};
    const auto name_count = collect_names(objects, data_sets, policy, request, names);
    if (name_count > names.size()) {
        return make_status(MmsStaticDispatchStatus::unsupported_request, confirmed);
    }

    std::size_t start = 0U;
    if (!request.continue_after.empty()) {
        bool found = false;
        for (std::size_t index = 0U; index < name_count; ++index) {
            if (span_equals(request.continue_after, names[index])) {
                start = index + 1U;
                found = true;
                break;
            }
        }
        if (!found) {
            return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
        }
    }

    const auto available = name_count - start;
    auto page_count = std::min(available, policy.maximum_names_per_response);
    if (available == 0U) {
        const std::span<const std::string_view> empty;
        return make_encoded(
            confirmed,
            MmsServiceSpanCodec::encode_get_name_list_response_into(
                confirmed.invoke_id, empty, false, response));
    }

    while (page_count > 0U) {
        const auto more_follows = start + page_count < name_count;
        const auto encoded = MmsServiceSpanCodec::encode_get_name_list_response_into(
            confirmed.invoke_id,
            std::span<const std::string_view>{names}.subspan(start, page_count),
            more_follows,
            response);
        if (encoded.success()) {
            return make_encoded(confirmed, encoded);
        }
        if (encoded.status != wire::EncodeStatus::buffer_too_small || page_count == 1U) {
            return make_encoded(confirmed, encoded);
        }
        --page_count;
    }
    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
}

[[nodiscard]] MmsStaticDispatchResult dispatch_attributes(
    const MmsStaticObjectTable& objects,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response) noexcept {
    MmsVariableAccessAttributesRequestView request;
    if (!MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
            confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }
    const auto* object = objects.find(request.name);
    if (object == nullptr) {
        return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
    }
    return make_encoded(
        confirmed,
        MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
            confirmed.invoke_id,
            object->mms_deletable,
            object->type_specification,
            response));
}

[[nodiscard]] MmsStaticDispatchResult dispatch_data_set_attributes(
    const MmsStaticDataSetTable& data_sets,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response) noexcept {
    MmsNamedVariableListAttributesRequestView request;
    if (!MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_request(
            confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }
    const auto* data_set = data_sets.find(request.name);
    if (data_set == nullptr) {
        return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
    }

    std::array<MmsNamedVariableListMemberInput, MmsDataSetSpanCodec::maximum_members> members{};
    for (std::size_t index = 0U; index < data_set->members.size(); ++index) {
        members[index] = MmsNamedVariableListMemberInput{
            data_set->members[index].domain,
            data_set->members[index].item};
    }
    return make_encoded(
        confirmed,
        MmsDataSetSpanCodec::encode_get_named_variable_list_attributes_response_into(
            confirmed.invoke_id,
            data_set->mms_deletable,
            std::span<const MmsNamedVariableListMemberInput>{members}.first(
                data_set->members.size()),
            response));
}

[[nodiscard]] MmsStaticDispatchResult dispatch_read(
    const MmsStaticObjectTable& objects,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    MmsReadRequestView request;
    if (!MmsServiceSpanCodec::try_decode_read_request(confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }
    if (request.specification_with_result) {
        // This bounded profile does not encode variableAccessSpecification in
        // Read responses. Reject the optional request form explicitly before
        // invoking any object callbacks instead of returning an incomplete PDU.
        return make_status(MmsStaticDispatchStatus::unsupported_request, confirmed);
    }

    std::array<MmsReadAccessResultInput, MmsServiceSpanCodec::maximum_variables> results{};
    std::size_t workspace_offset = 0U;
    for (std::size_t index = 0U; index < request.variable_count; ++index) {
        MmsObjectNameView name;
        if (!request.try_variable(index, name)) {
            return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
        }
        const auto* object = objects.find(name);
        if (object == nullptr) {
            results[index] = MmsReadAccessResultInput{
                false, {}, policy.missing_object_failure_code};
            continue;
        }

        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->read(object->context, remaining);
        if (read.status == wire::EncodeStatus::buffer_too_small) {
            return make_status(
                MmsStaticDispatchStatus::workspace_too_small,
                confirmed,
                workspace_offset + read.required_bytes);
        }
        if (!read.success()) {
            results[index] = MmsReadAccessResultInput{
                false, {}, policy.backend_failure_code};
            continue;
        }
        if (read.bytes_written > remaining.size() ||
            !valid_mms_data(remaining.first(read.bytes_written))) {
            return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
        }
        results[index] = MmsReadAccessResultInput{
            true,
            remaining.first(read.bytes_written),
            0U};
        workspace_offset += read.bytes_written;
    }

    return make_encoded(
        confirmed,
        MmsServiceSpanCodec::encode_read_response_into(
            confirmed.invoke_id,
            std::span<const MmsReadAccessResultInput>{results}.first(request.variable_count),
            response));
}

[[nodiscard]] MmsStaticDispatchResult dispatch_write(
    const MmsStaticObjectTable& objects,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const MmsStaticRequestAccessContext& access) noexcept {
    MmsWriteRequestView request;
    if (!MmsServiceSpanCodec::try_decode_write_request(confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }
    if (request.variable_count > policy.maximum_write_variables) {
        return make_status(MmsStaticDispatchStatus::unsupported_request, confirmed);
    }

    std::array<const MmsStaticObjectEntry*, MmsServiceSpanCodec::maximum_variables> resolved{};
    std::array<std::span<const std::uint8_t>, MmsServiceSpanCodec::maximum_variables> values{};
    for (std::size_t index = 0U; index < request.variable_count; ++index) {
        MmsObjectNameView name;
        if (!request.try_variable(index, name) || !request.try_value(index, values[index])) {
            return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
        }
        resolved[index] = objects.find(name);
    }

    std::array<MmsWriteAccessResultInput, MmsServiceSpanCodec::maximum_variables> results{};
    for (std::size_t index = 0U; index < request.variable_count; ++index) {
        const auto* object = resolved[index];
        if (object == nullptr) {
            results[index] = MmsWriteAccessResultInput{
                false, policy.missing_object_failure_code};
            continue;
        }
        if (!object->writable()) {
            results[index] = MmsWriteAccessResultInput{
                false, policy.access_denied_failure_code};
            continue;
        }
        const auto applied = object->contextual_write != nullptr
            ? object->contextual_write(object->write_context, values[index], access)
            : object->write(object->write_context, values[index]);
        results[index] = MmsWriteAccessResultInput{
            applied.success,
            applied.success ? 0U : applied.failure_code};
    }

    return make_encoded(
        confirmed,
        MmsServiceSpanCodec::encode_write_response_into(
            confirmed.invoke_id,
            std::span<const MmsWriteAccessResultInput>{results}.first(request.variable_count),
            response));
}

} // namespace

MmsStaticDispatchResult MmsStaticApplicationDispatcher::dispatch(
    const std::span<const std::uint8_t> mms_request,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace,
    const MmsStaticRequestAccessContext& access) const noexcept {
    MmsConfirmedPduView request;
    if (!MmsPduSpanCodec::try_decode_confirmed_request_view(mms_request, request)) {
        return MmsStaticDispatchResult{
            MmsStaticDispatchStatus::malformed_request,
            MmsWireConfirmedService::unknown,
            0U,
            0U,
            0U};
    }
    return dispatch(request, response, workspace, access);
}

MmsStaticDispatchResult MmsStaticApplicationDispatcher::dispatch(
    const MmsConfirmedPduView& request,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace,
    const MmsStaticRequestAccessContext& access) const noexcept {
    if (!objects_.valid() || !data_sets_.valid_against(objects_) || !policy_valid(policy_)) {
        return make_status(MmsStaticDispatchStatus::invalid_object_table, request);
    }
    if (request.kind != MmsWirePduKind::confirmed_request) {
        return make_status(MmsStaticDispatchStatus::malformed_request, request);
    }

    switch (request.service()) {
    case MmsWireConfirmedService::get_name_list:
        return dispatch_get_name_list(objects_, data_sets_, policy_, request, response);
    case MmsWireConfirmedService::get_variable_access_attributes:
        return dispatch_attributes(objects_, request, response);
    case MmsWireConfirmedService::get_named_variable_list_attributes:
        return dispatch_data_set_attributes(data_sets_, request, response);
    case MmsWireConfirmedService::read:
        return dispatch_read(objects_, policy_, request, response, workspace);
    case MmsWireConfirmedService::write:
        return dispatch_write(objects_, policy_, request, response, access);
    default:
        return make_status(MmsStaticDispatchStatus::unsupported_service, request);
    }
}

} // namespace ar::iec61850::mms
