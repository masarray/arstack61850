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

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
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
        (request.scope == MmsNameScopeKind::vmd_specific ||
         request.scope == MmsNameScopeKind::aa_specific)) {
        // Proven IEDScout discovery probes both VMD-specific and AA-specific
        // NamedVariableList scopes before walking domain-specific DataSets.
        // Return one deterministic item name per DataSet and de-duplicate names
        // shared by multiple domains at this scope.
        for (const auto& data_set : data_sets.data_sets()) {
            if (!append_unique(names, count, data_set.item)) {
                return names.size() + 1U;
            }
        }
        return count;
    }

    if (request.object_class == MmsNameListObjectClass::named_variable_list &&
        request.scope == MmsNameScopeKind::domain_specific) {
        for (const auto& data_set : data_sets.data_sets()) {
            if (span_equals(request.domain_id, data_set.domain)) {
                if (!append_unique(names, count, data_set.item)) {
                    return names.size() + 1U;
                }
            }
        }
        return count;
    }

    return names.size() + 1U;
}

// IEDScout performs deep per-domain directory walks and repeatedly supplies the
// last identifier from the previous response as continueAfter. Do not first
// materialize the complete directory into maximum_identifiers storage: a real
// SCL model can contain thousands of MMS variables and would otherwise fail
// before pagination is even applied. Keep only one bounded response page and
// detect moreFollows from the next eligible object.
[[nodiscard]] MmsStaticDispatchResult dispatch_domain_named_variable_page(
    const MmsStaticObjectTable& objects,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const MmsGetNameListRequestView& request,
    const std::span<std::uint8_t> response) noexcept {
    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers> page{};
    std::size_t page_count = 0U;
    bool continuation_found = request.continue_after.empty();
    bool emit = continuation_found;
    bool more_follows = false;

    for (const auto& object : objects.objects()) {
        if (!span_equals(request.domain_id, object.domain) ||
            (!policy.advertise_flattened_child_aliases &&
             is_flattened_child_with_root(objects, object.domain, object.item))) {
            continue;
        }

        if (!emit) {
            if (span_equals(request.continue_after, object.item)) {
                continuation_found = true;
                emit = true;
            }
            continue;
        }

        if (page_count < policy.maximum_names_per_response) {
            page[page_count++] = object.item;
            continue;
        }

        more_follows = true;
        break;
    }

    if (!continuation_found) {
        return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
    }

    if (page_count == 0U) {
        const std::span<const std::string_view> empty;
        return make_encoded(
            confirmed,
            MmsServiceSpanCodec::encode_get_name_list_response_into(
                confirmed.invoke_id, empty, false, response));
    }

    auto encoded_count = page_count;
    while (encoded_count > 0U) {
        const auto encoded = MmsServiceSpanCodec::encode_get_name_list_response_into(
            confirmed.invoke_id,
            std::span<const std::string_view>{page}.first(encoded_count),
            more_follows || encoded_count < page_count,
            response);
        if (encoded.success()) {
            return make_encoded(confirmed, encoded);
        }
        if (encoded.status != wire::EncodeStatus::buffer_too_small || encoded_count == 1U) {
            return make_encoded(confirmed, encoded);
        }
        --encoded_count;
    }

    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
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

    if (request.object_class == MmsNameListObjectClass::named_variable &&
        request.scope == MmsNameScopeKind::domain_specific) {
        return dispatch_domain_named_variable_page(
            objects, policy, confirmed, request, response);
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

struct ReadCompatibilityRequest final {
    MmsReadRequestView variables{};
    MmsObjectNameView variable_list_name{};
    bool uses_variable_list_name{};
};

[[nodiscard]] bool populate_relaxed_variable_list(
    const MmsConfirmedPduView& confirmed,
    const bool specification_with_result,
    const std::span<const std::uint8_t> variable_list,
    MmsReadRequestView& request) noexcept {
    std::size_t offset = 0U;
    std::size_t count = 0U;
    while (offset < variable_list.size()) {
        if (count >= MmsServiceSpanCodec::maximum_variables) {
            return false;
        }
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(variable_list, offset, definition) ||
            definition.tag_class != asn1::BerClass::universal ||
            definition.tag_number != 16 || !definition.constructed) {
            return false;
        }
        ++count;
    }
    if (count == 0U) {
        return false;
    }
    request = {};
    request.invoke_id = confirmed.invoke_id;
    request.specification_with_result = specification_with_result;
    request.variable_list = variable_list;
    request.variable_count = count;
    return true;
}

// The proven ARIEC61850 server accepts three Read discovery forms used by
// engineering clients: the normal explicit variableAccessSpecification wrapper,
// variableListName (DataSet Read), and an unwrapped listOfVariable compatibility
// form. The public span codec remains strict; this adapter deliberately widens
// only the server-facing dispatcher and preserves bounded parsing.
[[nodiscard]] bool try_decode_read_compatibility_request(
    const MmsConfirmedPduView& confirmed,
    ReadCompatibilityRequest& request) noexcept {
    request = {};
    if (MmsServiceSpanCodec::try_decode_read_request(confirmed, request.variables)) {
        return true;
    }
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 4 || !confirmed.service_constructed) {
        return false;
    }

    bool specification_with_result = false;
    bool have_flag = false;
    bool have_specification = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            return false;
        }

        if (field.tag_number == 0 && !field.constructed) {
            if (have_flag || field.value.size() != 1U) {
                return false;
            }
            specification_with_result = field.value[0] != 0U;
            have_flag = true;
            continue;
        }

        if (have_specification) {
            return false;
        }

        if (field.tag_number == 1 && field.constructed) {
            // Standard/observed form: variableAccessSpecification [1] explicit
            // wrapper containing either listOfVariable [0] or variableListName [1].
            asn1::BerTlvView specification;
            if (!asn1::BerSpanReader::try_read_exact(field.value, specification) ||
                specification.tag_class != asn1::BerClass::context_specific) {
                return false;
            }
            if (specification.tag_number == 0 && specification.constructed) {
                if (!populate_relaxed_variable_list(
                        confirmed,
                        specification_with_result,
                        specification.value,
                        request.variables)) {
                    return false;
                }
                have_specification = true;
                continue;
            }
            if (specification.tag_number == 1 && specification.constructed) {
                if (!MmsServiceSpanCodec::try_decode_object_name_view(
                        specification.value, request.variable_list_name)) {
                    return false;
                }
                request.variables.invoke_id = confirmed.invoke_id;
                request.variables.specification_with_result = specification_with_result;
                request.uses_variable_list_name = true;
                have_specification = true;
                continue;
            }
            return false;
        }

        if (field.tag_number == 0 && field.constructed) {
            // Compatibility form used by the proven server: unwrapped
            // listOfVariable [0] directly in Read-Request.
            if (!populate_relaxed_variable_list(
                    confirmed,
                    specification_with_result,
                    field.value,
                    request.variables)) {
                return false;
            }
            have_specification = true;
            continue;
        }

        return false;
    }

    return have_specification;
}

enum class ReadObjectStatus : std::uint8_t {
    ok,
    workspace_too_small,
    backend_failure,
};

struct ReadObjectResult final {
    ReadObjectStatus status{ReadObjectStatus::ok};
    std::size_t required_bytes{};
};

[[nodiscard]] ReadObjectResult read_object_into_result(
    const MmsStaticObjectEntry* object,
    const MmsStaticDispatchPolicy& policy,
    const std::span<std::uint8_t> workspace,
    std::size_t& workspace_offset,
    MmsReadAccessResultInput& result) noexcept {
    if (object == nullptr) {
        result = MmsReadAccessResultInput{
            false, {}, policy.missing_object_failure_code};
        return {};
    }

    const auto remaining = workspace.subspan(workspace_offset);
    const auto read = object->read(object->context, remaining);
    if (read.status == wire::EncodeStatus::buffer_too_small) {
        return {
            ReadObjectStatus::workspace_too_small,
            workspace_offset + read.required_bytes};
    }
    if (!read.success()) {
        result = MmsReadAccessResultInput{
            false, {}, policy.backend_failure_code};
        return {};
    }
    if (read.bytes_written > remaining.size() ||
        !valid_mms_data(remaining.first(read.bytes_written))) {
        return {ReadObjectStatus::backend_failure, 0U};
    }

    result = MmsReadAccessResultInput{
        true,
        remaining.first(read.bytes_written),
        0U};
    workspace_offset += read.bytes_written;
    return {};
}

[[nodiscard]] MmsStaticDispatchResult dispatch_read(
    const MmsStaticObjectTable& objects,
    const MmsStaticDataSetTable& data_sets,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    ReadCompatibilityRequest request;
    if (!try_decode_read_compatibility_request(confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }

    // Proven IEDScout path from ARIEC61850: accept specificationWithResult during
    // discovery, but keep the interoperable Read-Response shape to
    // listOfAccessResult only. Do not reject the request and do not synthesize a
    // variableAccessSpecification echo that the proven server does not emit.
    std::array<MmsReadAccessResultInput, MmsServiceSpanCodec::maximum_variables> results{};
    std::size_t result_count = 0U;
    std::size_t workspace_offset = 0U;

    if (request.uses_variable_list_name) {
        const auto* data_set = data_sets.find(request.variable_list_name);
        if (data_set == nullptr) {
            // Match the proven ARIEC behavior: a missing DataSet is a Read
            // AccessResult failure, not a Confirmed-Error for the whole request.
            results[0] = MmsReadAccessResultInput{
                false, {}, policy.missing_object_failure_code};
            result_count = 1U;
        } else {
            result_count = data_set->members.size();
            for (std::size_t index = 0U; index < result_count; ++index) {
                const auto& member = data_set->members[index];
                const MmsObjectNameView name{
                    MmsObjectNameViewKind::domain_specific,
                    as_bytes(member.domain),
                    as_bytes(member.item)};
                const auto read = read_object_into_result(
                    objects.find(name),
                    policy,
                    workspace,
                    workspace_offset,
                    results[index]);
                if (read.status == ReadObjectStatus::workspace_too_small) {
                    return make_status(
                        MmsStaticDispatchStatus::workspace_too_small,
                        confirmed,
                        read.required_bytes);
                }
                if (read.status == ReadObjectStatus::backend_failure) {
                    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
                }
            }
        }
    } else {
        result_count = request.variables.variable_count;
        for (std::size_t index = 0U; index < result_count; ++index) {
            MmsObjectNameView name;
            const MmsStaticObjectEntry* object = nullptr;
            if (request.variables.try_variable(index, name)) {
                object = objects.find(name);
            }
            // Golden behavior keeps an AccessResult slot even when one variable
            // specification cannot be resolved. This preserves decoder alignment.
            const auto read = read_object_into_result(
                object,
                policy,
                workspace,
                workspace_offset,
                results[index]);
            if (read.status == ReadObjectStatus::workspace_too_small) {
                return make_status(
                    MmsStaticDispatchStatus::workspace_too_small,
                    confirmed,
                    read.required_bytes);
            }
            if (read.status == ReadObjectStatus::backend_failure) {
                return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
            }
        }
    }

    if (result_count == 0U || result_count > results.size()) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }

    return make_encoded(
        confirmed,
        MmsServiceSpanCodec::encode_read_response_into(
            confirmed.invoke_id,
            std::span<const MmsReadAccessResultInput>{results}.first(result_count),
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
        return dispatch_read(objects_, data_sets_, policy_, request, response, workspace);
    case MmsWireConfirmedService::write:
        return dispatch_write(objects_, policy_, request, response, access);
    default:
        return make_status(MmsStaticDispatchStatus::unsupported_service, request);
    }
}

} // namespace ar::iec61850::mms
