// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_dispatcher.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool span_equals(
    const std::span<const std::uint8_t> bytes,
    const std::string_view text) noexcept {
    if (bytes.size() != text.size()) return false;
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

[[nodiscard]] std::string_view as_text(
    const std::span<const std::uint8_t> bytes) noexcept {
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] bool valid_mms_data(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView data;
    if (!asn1::BerSpanReader::try_read_exact(encoded, data) ||
        data.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (data.tag_number == 1 || data.tag_number == 2) return data.constructed;
    return data.tag_number >= 3 && data.tag_number <= 17 && !data.constructed;
}

[[nodiscard]] bool valid_mms_type_specification(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView type;
    return !encoded.empty() &&
        asn1::BerSpanReader::try_read_exact(encoded, type) &&
        type.tag_class == asn1::BerClass::context_specific;
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
        if (names[index] == value) return true;
    }
    if (count >= names.size()) return false;
    names[count++] = value;
    return true;
}

[[nodiscard]] bool is_flattened_child_with_root(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view item) noexcept {
    const auto separator = item.find('$');
    if (separator == std::string_view::npos || separator == 0U) return false;
    const auto root = item.substr(0U, separator);
    for (const auto& candidate : objects.objects()) {
        if (candidate.domain == domain && candidate.item == root) return true;
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
            if (!append_unique(names, count, object.domain)) return names.size() + 1U;
        }
        for (const auto& data_set : data_sets.data_sets()) {
            if (!append_unique(names, count, data_set.domain)) return names.size() + 1U;
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
            if (!append_unique(names, count, object.item)) return names.size() + 1U;
        }
        return count;
    }

    if (request.object_class == MmsNameListObjectClass::named_variable_list &&
        (request.scope == MmsNameScopeKind::vmd_specific ||
         request.scope == MmsNameScopeKind::aa_specific)) {
        for (const auto& data_set : data_sets.data_sets()) {
            if (!append_unique(names, count, data_set.item)) return names.size() + 1U;
        }
        return count;
    }

    if (request.object_class == MmsNameListObjectClass::named_variable_list &&
        request.scope == MmsNameScopeKind::domain_specific) {
        for (const auto& data_set : data_sets.data_sets()) {
            if (span_equals(request.domain_id, data_set.domain) &&
                !append_unique(names, count, data_set.item)) {
                return names.size() + 1U;
            }
        }
        return count;
    }

    return names.size() + 1U;
}

[[nodiscard]] bool insert_sorted_directory_name(
    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers>& page,
    std::size_t& page_count,
    const std::size_t page_capacity,
    const std::string_view value,
    bool& more_follows) noexcept {
    const auto begin = page.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(page_count);
    const auto position = std::lower_bound(begin, end, value);
    if (position != end && *position == value) return true;

    if (page_count < page_capacity) {
        std::move_backward(position, end, end + 1);
        *position = value;
        ++page_count;
        return true;
    }

    more_follows = true;
    if (page_capacity == 0U || position == end) return true;
    std::move_backward(position, end - 1, end);
    *position = value;
    return true;
}

[[nodiscard]] MmsStaticDispatchResult dispatch_indexed_domain_named_variable_page(
    const std::span<const MmsStaticDirectoryEntry> directory,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const MmsGetNameListRequestView& request,
    const std::span<std::uint8_t> response) noexcept {
    // The host IED-simulator directory intentionally preserves SCL/IEDScout
    // declaration order instead of lexical item order.  Continuation therefore
    // follows the exact emitted sequence rather than lower_bound() semantics.
    const auto domain = as_text(request.domain_id);
    const auto continuation = as_text(request.continue_after);
    bool emit = continuation.empty();
    bool continuation_found = emit;
    bool more_follows = false;

    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers> page{};
    std::size_t page_count{};
    for (const auto& entry : directory) {
        if (entry.domain != domain) continue;
        if (!emit) {
            if (entry.item == continuation) {
                continuation_found = true;
                emit = true;
            }
            continue;
        }
        if (!continuation.empty() && entry.item == continuation) continue;
        if (page_count < policy.maximum_names_per_response) {
            page[page_count++] = entry.item;
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
        if (encoded.success()) return make_encoded(confirmed, encoded);
        if (encoded.status != wire::EncodeStatus::buffer_too_small || encoded_count == 1U) {
            return make_encoded(confirmed, encoded);
        }
        --encoded_count;
    }
    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
}

[[nodiscard]] MmsStaticDispatchResult dispatch_domain_named_variable_page(
    const MmsStaticObjectTable& objects,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const MmsGetNameListRequestView& request,
    const std::span<std::uint8_t> response) noexcept {
    std::array<std::string_view, MmsServiceSpanCodec::maximum_identifiers> page{};
    std::size_t page_count = 0U;
    bool continuation_found = request.continue_after.empty();
    bool more_follows = false;

    if (!policy.advertise_flattened_child_aliases) {
        bool emit = continuation_found;
        for (const auto& object : objects.objects()) {
            if (!span_equals(request.domain_id, object.domain) ||
                is_flattened_child_with_root(objects, object.domain, object.item)) {
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
    } else {
        const auto continue_after = as_text(request.continue_after);
        for (const auto& object : objects.objects()) {
            if (!span_equals(request.domain_id, object.domain)) continue;

            std::size_t prefix_end = object.item.find('$');
            while (true) {
                const auto prefix = object.item.substr(
                    0U,
                    prefix_end == std::string_view::npos
                        ? object.item.size()
                        : prefix_end);
                if (!prefix.empty()) {
                    if (!continue_after.empty() && prefix == continue_after) {
                        continuation_found = true;
                    }
                    if (continue_after.empty() || prefix > continue_after) {
                        if (!insert_sorted_directory_name(
                                page,
                                page_count,
                                policy.maximum_names_per_response,
                                prefix,
                                more_follows)) {
                            return make_status(
                                MmsStaticDispatchStatus::backend_failure,
                                confirmed);
                        }
                    }
                }

                if (prefix_end == std::string_view::npos) break;
                if (prefix_end + 1U >= object.item.size()) break;
                prefix_end = object.item.find('$', prefix_end + 1U);
            }
        }
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
        if (encoded.success()) return make_encoded(confirmed, encoded);
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
    const std::span<const MmsStaticDirectoryEntry> directory,
    const MmsStaticDispatchPolicy& policy,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response) noexcept {
    MmsGetNameListRequestView request;
    if (!MmsServiceSpanCodec::try_decode_get_name_list_request(confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }

    if (request.object_class == MmsNameListObjectClass::named_variable &&
        request.scope == MmsNameScopeKind::domain_specific) {
        if (!directory.empty() && policy.advertise_flattened_child_aliases) {
            return dispatch_indexed_domain_named_variable_page(
                directory, policy, confirmed, request, response);
        }
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
        if (!found) return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
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
        if (encoded.success()) return make_encoded(confirmed, encoded);
        if (encoded.status != wire::EncodeStatus::buffer_too_small || page_count == 1U) {
            return make_encoded(confirmed, encoded);
        }
        --page_count;
    }
    return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
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
        if (count >= MmsServiceSpanCodec::maximum_variables) return false;
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(variable_list, offset, definition) ||
            definition.tag_class != asn1::BerClass::universal ||
            definition.tag_number != 16 || !definition.constructed) {
            return false;
        }
        ++count;
    }
    if (count == 0U) return false;
    request = {};
    request.invoke_id = confirmed.invoke_id;
    request.specification_with_result = specification_with_result;
    request.variable_list = variable_list;
    request.variable_count = count;
    return true;
}

[[nodiscard]] bool try_decode_read_compatibility_request(
    const MmsConfirmedPduView& confirmed,
    ReadCompatibilityRequest& request) noexcept {
    request = {};
    if (MmsServiceSpanCodec::try_decode_read_request(confirmed, request.variables)) return true;
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
            if (have_flag || field.value.size() != 1U) return false;
            specification_with_result = field.value[0] != 0U;
            have_flag = true;
            continue;
        }
        if (have_specification) return false;

        if (field.tag_number == 1 && field.constructed) {
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

constexpr std::size_t kMaximumSyntheticReadDepth = 16U;
constexpr std::array<std::string_view, 12U> kUrcbAttributeOrder{
    "RptID", "RptEna", "Resv", "DatSet", "ConfRev", "OptFlds",
    "BufTm", "SqNum", "TrgOps", "IntgPd", "GI", "Owner"};
constexpr std::array<std::string_view, 15U> kBrcbAttributeOrder{
    "RptID", "RptEna", "DatSet", "ConfRev", "OptFlds", "BufTm", "SqNum",
    "TrgOps", "IntgPd", "GI", "PurgeBuf", "EntryID", "TimeofEntry",
    "ResvTms", "Owner"};

enum class SyntheticReadStatus : std::uint8_t {
    ok,
    not_found,
    value_unavailable,
    workspace_too_small,
    backend_failure,
};

struct SyntheticReadResult final {
    SyntheticReadStatus status{SyntheticReadStatus::not_found};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
};

struct SyntheticMeasureResult final {
    SyntheticReadStatus status{SyntheticReadStatus::not_found};
    std::size_t content_bytes{};
    std::size_t encoded_bytes{};
};

struct SyntheticChild final {
    std::string_view prefix{};
    const MmsStaticObjectEntry* exact{};
    std::size_t declaration_order{std::numeric_limits<std::size_t>::max()};
    bool found{};
};

struct SyntheticChildRank final {
    bool known{};
    std::size_t value{};
};

[[nodiscard]] bool is_descendant_item(
    const std::string_view item,
    const std::string_view prefix) noexcept {
    return item.size() > prefix.size() &&
        item.compare(0U, prefix.size(), prefix) == 0 &&
        item[prefix.size()] == '$';
}

[[nodiscard]] std::string_view immediate_child_prefix(
    const std::string_view item,
    const std::string_view prefix) noexcept {
    if (!is_descendant_item(item, prefix)) return {};
    const auto next = item.find('$', prefix.size() + 1U);
    return item.substr(0U, next == std::string_view::npos ? item.size() : next);
}

[[nodiscard]] std::span<const std::string_view> semantic_child_order(
    const std::string_view prefix) noexcept {
    if (prefix.find("$RP$") != std::string_view::npos) {
        return {kUrcbAttributeOrder};
    }
    if (prefix.find("$BR$") != std::string_view::npos) {
        return {kBrcbAttributeOrder};
    }
    return {};
}

[[nodiscard]] SyntheticChildRank synthetic_child_rank(
    const std::string_view prefix,
    const std::string_view child) noexcept {
    if (!is_descendant_item(child, prefix)) return {};
    const auto suffix = child.substr(prefix.size() + 1U);
    const auto order = semantic_child_order(prefix);
    for (std::size_t index = 0U; index < order.size(); ++index) {
        if (suffix == order[index]) return {true, index};
    }
    return {};
}

[[nodiscard]] bool synthetic_child_less(
    const std::string_view prefix,
    const std::string_view left,
    const std::string_view right) noexcept {
    const auto left_rank = synthetic_child_rank(prefix, left);
    const auto right_rank = synthetic_child_rank(prefix, right);
    if (left_rank.known || right_rank.known) {
        if (left_rank.known && right_rank.known) {
            return left_rank.value < right_rank.value;
        }
        return left_rank.known;
    }
    return left < right;
}

[[nodiscard]] bool use_declaration_order(
    const std::string_view prefix) noexcept {
    // SCL declaration order is positional inside constructed Data Objects/Data
    // Attributes (LN$FC$DO...). LN roots and FC namespaces are discovery
    // namespaces rather than positional values.
    return static_cast<std::size_t>(
        std::count(
            prefix.begin(),
            prefix.end(),
            static_cast<char>(0x24))) >= 2U;
}

[[nodiscard]] bool declaration_order_less(
    const std::size_t left_order,
    const std::string_view left,
    const std::size_t right_order,
    const std::string_view right) noexcept {
    constexpr auto unspecified = std::numeric_limits<std::size_t>::max();
    if (left_order != unspecified || right_order != unspecified) {
        if (left_order == unspecified) return false;
        if (right_order == unspecified) return true;
        if (left_order != right_order) return left_order < right_order;
    }
    return left < right;
}

[[nodiscard]] SyntheticChild next_synthetic_child(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view prefix,
    const std::string_view after,
    const bool have_after) noexcept {
    SyntheticChild result;
    const bool semantic_order = !semantic_child_order(prefix).empty();
    const bool declaration_order = !semantic_order && use_declaration_order(prefix);

    auto after_order = std::numeric_limits<std::size_t>::max();
    if (have_after && declaration_order) {
        for (const auto& candidate : objects.objects()) {
            if (candidate.domain != domain || !is_descendant_item(candidate.item, prefix)) {
                continue;
            }
            const auto child = immediate_child_prefix(candidate.item, prefix);
            if (child == after) {
                after_order = std::min(after_order, candidate.declaration_order);
            }
        }
    }

    for (const auto& candidate : objects.objects()) {
        if (candidate.domain != domain || !is_descendant_item(candidate.item, prefix)) {
            continue;
        }
        const auto child = immediate_child_prefix(candidate.item, prefix);
        if (child.empty() || (have_after && child == after)) continue;

        if (have_after) {
            if (semantic_order) {
                if (!synthetic_child_less(prefix, after, child)) continue;
            } else if (declaration_order &&
                after_order != std::numeric_limits<std::size_t>::max() &&
                candidate.declaration_order != std::numeric_limits<std::size_t>::max()) {
                if (candidate.declaration_order <= after_order) continue;
            } else if (!synthetic_child_less(prefix, after, child)) {
                continue;
            }
        }

        if (result.found && child == result.prefix) {
            result.declaration_order =
                std::min(result.declaration_order, candidate.declaration_order);
            if (candidate.item == child) result.exact = &candidate;
            continue;
        }

        const bool better = !result.found ||
            (semantic_order
                ? synthetic_child_less(prefix, child, result.prefix)
                : declaration_order
                    ? declaration_order_less(
                        candidate.declaration_order,
                        child,
                        result.declaration_order,
                        result.prefix)
                    : synthetic_child_less(prefix, child, result.prefix));
        if (!better) continue;

        result.found = true;
        result.prefix = child;
        result.exact = candidate.item == child ? &candidate : nullptr;
        result.declaration_order = candidate.declaration_order;
    }
    return result;
}

[[nodiscard]] SyntheticMeasureResult measure_exact_object(
    const MmsStaticObjectEntry& object) noexcept {
    const auto probe = object.read(object.context, {});
    if (probe.status == wire::EncodeStatus::buffer_too_small && probe.required_bytes > 0U) {
        return {SyntheticReadStatus::ok, 0U, probe.required_bytes};
    }
    if (probe.success() && probe.bytes_written > 0U) {
        return {SyntheticReadStatus::ok, 0U, probe.bytes_written};
    }
    return {SyntheticReadStatus::value_unavailable, 0U, 0U};
}

[[nodiscard]] SyntheticMeasureResult measure_synthetic_subtree(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view prefix,
    const std::size_t depth) noexcept {
    if (depth >= kMaximumSyntheticReadDepth || domain.empty() || prefix.empty()) {
        return {SyntheticReadStatus::backend_failure, 0U, 0U};
    }

    std::size_t content_bytes = 0U;
    std::string_view after;
    bool have_after = false;
    bool found_child = false;
    while (true) {
        const auto child = next_synthetic_child(objects, domain, prefix, after, have_after);
        if (!child.found) break;
        found_child = true;
        const auto measured = child.exact != nullptr
            ? measure_exact_object(*child.exact)
            : measure_synthetic_subtree(objects, domain, child.prefix, depth + 1U);
        if (measured.status != SyntheticReadStatus::ok) return measured;
        if (measured.encoded_bytes >
            std::numeric_limits<std::size_t>::max() - content_bytes) {
            return {SyntheticReadStatus::backend_failure, 0U, 0U};
        }
        content_bytes += measured.encoded_bytes;
        after = child.prefix;
        have_after = true;
    }

    if (!found_child) return {SyntheticReadStatus::not_found, 0U, 0U};
    const auto encoded = asn1::BerSpanWriter::tlv_size(2, content_bytes);
    if (!encoded) return {SyntheticReadStatus::backend_failure, 0U, 0U};
    return {SyntheticReadStatus::ok, content_bytes, *encoded};
}

[[nodiscard]] SyntheticReadResult encode_synthetic_subtree(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view prefix,
    const std::span<std::uint8_t> destination,
    const std::size_t depth) noexcept {
    const auto measured = measure_synthetic_subtree(objects, domain, prefix, depth);
    if (measured.status != SyntheticReadStatus::ok) {
        return {measured.status, 0U, measured.encoded_bytes};
    }
    if (destination.size() < measured.encoded_bytes) {
        return {SyntheticReadStatus::workspace_too_small, 0U, measured.encoded_bytes};
    }

    asn1::BerSpanWriter writer{destination.first(measured.encoded_bytes)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, measured.content_bytes)) {
        return {SyntheticReadStatus::backend_failure, 0U, measured.encoded_bytes};
    }
    std::size_t offset = writer.size();
    std::string_view after;
    bool have_after = false;

    while (true) {
        const auto child = next_synthetic_child(objects, domain, prefix, after, have_after);
        if (!child.found) break;
        if (child.exact != nullptr) {
            const auto read = child.exact->read(
                child.exact->context,
                destination.subspan(offset, measured.encoded_bytes - offset));
            if (read.status == wire::EncodeStatus::buffer_too_small) {
                const auto required = read.required_bytes >
                        std::numeric_limits<std::size_t>::max() - offset
                    ? std::numeric_limits<std::size_t>::max()
                    : offset + read.required_bytes;
                return {SyntheticReadStatus::workspace_too_small, 0U, required};
            }
            if (!read.success()) return {SyntheticReadStatus::value_unavailable, 0U, 0U};
            if (read.bytes_written == 0U ||
                read.bytes_written > measured.encoded_bytes - offset ||
                !valid_mms_data(destination.subspan(offset, read.bytes_written))) {
                return {SyntheticReadStatus::backend_failure, 0U, measured.encoded_bytes};
            }
            offset += read.bytes_written;
        } else {
            const auto nested = encode_synthetic_subtree(
                objects,
                domain,
                child.prefix,
                destination.subspan(offset, measured.encoded_bytes - offset),
                depth + 1U);
            if (nested.status != SyntheticReadStatus::ok) {
                if (nested.status == SyntheticReadStatus::workspace_too_small &&
                    nested.required_bytes <=
                        std::numeric_limits<std::size_t>::max() - offset) {
                    return {nested.status, 0U, offset + nested.required_bytes};
                }
                return nested;
            }
            offset += nested.bytes_written;
        }
        after = child.prefix;
        have_after = true;
    }

    if (offset != measured.encoded_bytes || !valid_mms_data(destination.first(offset))) {
        return {SyntheticReadStatus::backend_failure, 0U, measured.encoded_bytes};
    }
    return {SyntheticReadStatus::ok, offset, offset};
}

enum class SyntheticTypeStatus : std::uint8_t {
    ok,
    not_found,
    workspace_too_small,
    backend_failure,
};

struct SyntheticTypeMeasureResult final {
    SyntheticTypeStatus status{SyntheticTypeStatus::not_found};
    std::size_t component_bytes{};
    std::size_t list_bytes{};
    std::size_t encoded_bytes{};
};

struct SyntheticTypeEncodeResult final {
    SyntheticTypeStatus status{SyntheticTypeStatus::not_found};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
};

[[nodiscard]] bool checked_add(
    std::size_t& total,
    const std::size_t value) noexcept {
    if (value > std::numeric_limits<std::size_t>::max() - total) return false;
    total += value;
    return true;
}

[[nodiscard]] SyntheticTypeMeasureResult measure_synthetic_type_subtree(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view prefix,
    const std::size_t depth) noexcept {
    if (depth >= kMaximumSyntheticReadDepth || domain.empty() || prefix.empty()) {
        return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
    }

    std::size_t component_bytes = 0U;
    std::string_view after;
    bool have_after = false;
    bool found_child = false;
    while (true) {
        const auto child = next_synthetic_child(objects, domain, prefix, after, have_after);
        if (!child.found) break;
        found_child = true;

        std::size_t child_type_bytes = 0U;
        if (child.exact != nullptr) {
            if (!valid_mms_type_specification(child.exact->type_specification)) {
                return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
            }
            child_type_bytes = child.exact->type_specification.size();
        } else {
            const auto nested = measure_synthetic_type_subtree(
                objects, domain, child.prefix, depth + 1U);
            if (nested.status != SyntheticTypeStatus::ok) return nested;
            child_type_bytes = nested.encoded_bytes;
        }

        const auto component_name = child.prefix.substr(prefix.size() + 1U);
        const auto name_tlv = asn1::BerSpanWriter::tlv_size(0, component_name.size());
        const auto type_wrapper = asn1::BerSpanWriter::tlv_size(1, child_type_bytes);
        if (!name_tlv || !type_wrapper) {
            return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
        }
        std::size_t component_content = *name_tlv;
        if (!checked_add(component_content, *type_wrapper)) {
            return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
        }
        const auto component = asn1::BerSpanWriter::tlv_size(16, component_content);
        if (!component || !checked_add(component_bytes, *component)) {
            return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
        }
        after = child.prefix;
        have_after = true;
    }

    if (!found_child) return {SyntheticTypeStatus::not_found, 0U, 0U, 0U};
    const auto list = asn1::BerSpanWriter::tlv_size(1, component_bytes);
    if (!list) return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
    const auto structure = asn1::BerSpanWriter::tlv_size(2, *list);
    if (!structure) return {SyntheticTypeStatus::backend_failure, 0U, 0U, 0U};
    return {SyntheticTypeStatus::ok, component_bytes, *list, *structure};
}

[[nodiscard]] bool write_header(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const asn1::BerClass tag_class,
    const bool constructed,
    const std::int32_t tag_number,
    const std::size_t value_length) noexcept {
    if (offset > destination.size()) return false;
    asn1::BerSpanWriter writer{destination.subspan(offset)};
    if (!writer.write_tlv_header(tag_class, constructed, tag_number, value_length)) {
        return false;
    }
    offset += writer.size();
    return true;
}

[[nodiscard]] bool write_bytes(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::span<const std::uint8_t> source) noexcept {
    if (offset > destination.size() || source.size() > destination.size() - offset) return false;
    std::copy(source.begin(), source.end(), destination.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += source.size();
    return true;
}

[[nodiscard]] SyntheticTypeEncodeResult encode_synthetic_type_subtree(
    const MmsStaticObjectTable& objects,
    const std::string_view domain,
    const std::string_view prefix,
    const std::span<std::uint8_t> destination,
    const std::size_t depth) noexcept {
    const auto measured = measure_synthetic_type_subtree(objects, domain, prefix, depth);
    if (measured.status != SyntheticTypeStatus::ok) {
        return {measured.status, 0U, measured.encoded_bytes};
    }
    if (destination.size() < measured.encoded_bytes) {
        return {SyntheticTypeStatus::workspace_too_small, 0U, measured.encoded_bytes};
    }

    std::size_t offset = 0U;
    if (!write_header(
            destination, offset, asn1::BerClass::context_specific, true, 2,
            measured.list_bytes) ||
        !write_header(
            destination, offset, asn1::BerClass::context_specific, true, 1,
            measured.component_bytes)) {
        return {SyntheticTypeStatus::backend_failure, 0U, measured.encoded_bytes};
    }

    std::string_view after;
    bool have_after = false;
    while (true) {
        const auto child = next_synthetic_child(objects, domain, prefix, after, have_after);
        if (!child.found) break;

        std::size_t child_type_bytes = 0U;
        SyntheticTypeMeasureResult nested_measure;
        if (child.exact != nullptr) {
            child_type_bytes = child.exact->type_specification.size();
        } else {
            nested_measure = measure_synthetic_type_subtree(
                objects, domain, child.prefix, depth + 1U);
            if (nested_measure.status != SyntheticTypeStatus::ok) {
                return {nested_measure.status, 0U, nested_measure.encoded_bytes};
            }
            child_type_bytes = nested_measure.encoded_bytes;
        }

        const auto component_name = child.prefix.substr(prefix.size() + 1U);
        const auto name_tlv = asn1::BerSpanWriter::tlv_size(0, component_name.size());
        const auto type_wrapper = asn1::BerSpanWriter::tlv_size(1, child_type_bytes);
        if (!name_tlv || !type_wrapper) {
            return {SyntheticTypeStatus::backend_failure, 0U, measured.encoded_bytes};
        }
        std::size_t component_content = *name_tlv;
        if (!checked_add(component_content, *type_wrapper) ||
            !write_header(
                destination, offset, asn1::BerClass::universal, true, 16,
                component_content) ||
            !write_header(
                destination, offset, asn1::BerClass::context_specific, false, 0,
                component_name.size()) ||
            !write_bytes(destination, offset, as_bytes(component_name)) ||
            !write_header(
                destination, offset, asn1::BerClass::context_specific, true, 1,
                child_type_bytes)) {
            return {SyntheticTypeStatus::backend_failure, 0U, measured.encoded_bytes};
        }

        if (child.exact != nullptr) {
            if (!write_bytes(destination, offset, child.exact->type_specification)) {
                return {SyntheticTypeStatus::backend_failure, 0U, measured.encoded_bytes};
            }
        } else {
            const auto nested = encode_synthetic_type_subtree(
                objects,
                domain,
                child.prefix,
                destination.subspan(offset),
                depth + 1U);
            if (nested.status != SyntheticTypeStatus::ok) {
                return nested;
            }
            offset += nested.bytes_written;
        }
        after = child.prefix;
        have_after = true;
    }

    if (offset != measured.encoded_bytes ||
        !valid_mms_type_specification(destination.first(offset))) {
        return {SyntheticTypeStatus::backend_failure, 0U, measured.encoded_bytes};
    }
    return {SyntheticTypeStatus::ok, offset, offset};
}

[[nodiscard]] MmsStaticDispatchResult dispatch_attributes(
    const MmsStaticObjectTable& objects,
    const MmsConfirmedPduView& confirmed,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    MmsVariableAccessAttributesRequestView request;
    if (!MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
            confirmed, request)) {
        return make_status(MmsStaticDispatchStatus::malformed_request, confirmed);
    }

    const auto* exact = objects.find(request.name);
    const auto request_item = as_text(request.name.item);

    // Exact non-LN objects carry the authoritative positional MMS
    // TypeSpecification. Read uses the same exact object, so GVAA must describe
    // its Data with identical component order. Re-synthesizing ordinary
    // descendants can reorder IEC 61850 structures such as {stVal,q,t}.
    //
    // Non-LLN0 Logical Node roots are also precompiled from the SCL declaration
    // tree. The simulator patches configured control-service contracts into
    // those roots before exposing the object table, so rebuilding a large CSWI/
    // XSWI tree for every GVAA request is both unnecessary and pathological.
    // IEDScout's golden CSWI1 GVAA is a bounded ~1.5 kB response; serving the
    // precompiled root keeps that behavior deterministic. LLN0 remains the
    // exception because RP/BR/SG service objects are composed per association.
    const bool exact_logical_node_root =
        exact != nullptr &&
        request.name.kind == MmsObjectNameViewKind::domain_specific &&
        request_item.find(static_cast<char>(0x24)) == std::string_view::npos;
    if (exact != nullptr &&
        request.name.kind == MmsObjectNameViewKind::domain_specific &&
        (request_item.find(static_cast<char>(0x24)) != std::string_view::npos ||
         (exact_logical_node_root && request_item != "LLN0"))) {
        return make_encoded(
            confirmed,
            MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
                confirmed.invoke_id,
                exact->mms_deletable,
                exact->type_specification,
                response));
    }

    if (request.name.kind == MmsObjectNameViewKind::domain_specific &&
        !request.name.domain.empty() && !request.name.item.empty()) {
        // A Logical Node/root object may have been encoded before dynamic
        // service objects (RP/BR/SG/CO aliases) were composed into the final
        // per-association table. Build GVAA from that final table first whenever
        // descendants exist so discovery sees the complete final hierarchy.
        const auto synthetic = encode_synthetic_type_subtree(
            objects,
            as_text(request.name.domain),
            as_text(request.name.item),
            workspace,
            0U);
        switch (synthetic.status) {
        case SyntheticTypeStatus::ok:
            return make_encoded(
                confirmed,
                MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
                    confirmed.invoke_id,
                    exact != nullptr ? exact->mms_deletable : false,
                    workspace.first(synthetic.bytes_written),
                    response));
        case SyntheticTypeStatus::workspace_too_small:
            return make_status(
                MmsStaticDispatchStatus::workspace_too_small,
                confirmed,
                synthetic.required_bytes);
        case SyntheticTypeStatus::backend_failure:
            return make_status(MmsStaticDispatchStatus::backend_failure, confirmed);
        case SyntheticTypeStatus::not_found:
            break;
        }
    }

    if (exact != nullptr) {
        return make_encoded(
            confirmed,
            MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
                confirmed.invoke_id,
                exact->mms_deletable,
                exact->type_specification,
                response));
    }
    return make_status(MmsStaticDispatchStatus::object_not_found, confirmed);
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
        result = MmsReadAccessResultInput{false, {}, policy.missing_object_failure_code};
        return {};
    }
    const auto remaining = workspace.subspan(workspace_offset);
    const auto read = object->read(object->context, remaining);
    if (read.status == wire::EncodeStatus::buffer_too_small) {
        return {ReadObjectStatus::workspace_too_small, workspace_offset + read.required_bytes};
    }
    if (!read.success()) {
        result = MmsReadAccessResultInput{false, {}, policy.backend_failure_code};
        return {};
    }
    if (read.bytes_written > remaining.size() ||
        !valid_mms_data(remaining.first(read.bytes_written))) {
        return {ReadObjectStatus::backend_failure, 0U};
    }
    result = MmsReadAccessResultInput{true, remaining.first(read.bytes_written), 0U};
    workspace_offset += read.bytes_written;
    return {};
}

[[nodiscard]] ReadObjectResult read_name_into_result(
    const MmsStaticObjectTable& objects,
    const MmsObjectNameView& name,
    const MmsStaticDispatchPolicy& policy,
    const std::span<std::uint8_t> workspace,
    std::size_t& workspace_offset,
    MmsReadAccessResultInput& result) noexcept {
    if (const auto* exact = objects.find(name); exact != nullptr) {
        return read_object_into_result(exact, policy, workspace, workspace_offset, result);
    }
    if (name.kind != MmsObjectNameViewKind::domain_specific ||
        name.domain.empty() || name.item.empty()) {
        result = MmsReadAccessResultInput{false, {}, policy.missing_object_failure_code};
        return {};
    }

    const auto remaining = workspace.subspan(workspace_offset);
    const auto synthetic = encode_synthetic_subtree(
        objects,
        as_text(name.domain),
        as_text(name.item),
        remaining,
        0U);
    switch (synthetic.status) {
    case SyntheticReadStatus::ok:
        if (synthetic.bytes_written == 0U ||
            synthetic.bytes_written > remaining.size() ||
            !valid_mms_data(remaining.first(synthetic.bytes_written))) {
            return {ReadObjectStatus::backend_failure, 0U};
        }
        result = MmsReadAccessResultInput{
            true, remaining.first(synthetic.bytes_written), 0U};
        workspace_offset += synthetic.bytes_written;
        return {};
    case SyntheticReadStatus::not_found:
        result = MmsReadAccessResultInput{false, {}, policy.missing_object_failure_code};
        return {};
    case SyntheticReadStatus::value_unavailable:
        result = MmsReadAccessResultInput{false, {}, policy.backend_failure_code};
        return {};
    case SyntheticReadStatus::workspace_too_small:
        return {
            ReadObjectStatus::workspace_too_small,
            synthetic.required_bytes >
                    std::numeric_limits<std::size_t>::max() - workspace_offset
                ? std::numeric_limits<std::size_t>::max()
                : workspace_offset + synthetic.required_bytes};
    case SyntheticReadStatus::backend_failure:
        return {ReadObjectStatus::backend_failure, 0U};
    }
    return {ReadObjectStatus::backend_failure, 0U};
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

    std::array<MmsReadAccessResultInput, MmsServiceSpanCodec::maximum_variables> results{};
    std::size_t result_count = 0U;
    std::size_t workspace_offset = 0U;

    if (request.uses_variable_list_name) {
        const auto* data_set = data_sets.find(request.variable_list_name);
        if (data_set == nullptr) {
            results[0] = MmsReadAccessResultInput{false, {}, policy.missing_object_failure_code};
            result_count = 1U;
        } else {
            result_count = data_set->members.size();
            for (std::size_t index = 0U; index < result_count; ++index) {
                const auto& member = data_set->members[index];
                const MmsObjectNameView name{
                    MmsObjectNameViewKind::domain_specific,
                    as_bytes(member.domain),
                    as_bytes(member.item)};
                const auto read = read_name_into_result(
                    objects, name, policy, workspace, workspace_offset, results[index]);
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
            if (!request.variables.try_variable(index, name)) {
                results[index] = MmsReadAccessResultInput{
                    false, {}, policy.missing_object_failure_code};
                continue;
            }
            const auto read = read_name_into_result(
                objects, name, policy, workspace, workspace_offset, results[index]);
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

[[nodiscard]] bool decode_write_boolean(
    const std::span<const std::uint8_t> encoded,
    bool& value) noexcept {
    value = false;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 3 || tlv.constructed || tlv.value.size() != 1U) {
        return false;
    }
    value = tlv.value[0] != 0U;
    return true;
}

[[nodiscard]] std::uint8_t write_phase(
    const MmsStaticObjectEntry& object,
    const std::span<const std::uint8_t> value) noexcept {
    switch (object.write_semantic) {
    case MmsStaticWriteSemantic::rcb_enable: {
        bool enable = true;
        if (decode_write_boolean(value, enable) && !enable) return 0U;
        return 2U;
    }
    case MmsStaticWriteSemantic::rcb_configuration:
        return 1U;
    case MmsStaticWriteSemantic::rcb_general_interrogation:
        return 3U;
    case MmsStaticWriteSemantic::ordinary:
        return 1U;
    }
    return 1U;
}

void build_write_execution_order(
    const std::span<const MmsStaticObjectEntry* const> resolved,
    const std::span<const std::span<const std::uint8_t>> values,
    const std::span<std::size_t> order) noexcept {
    for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
    for (std::size_t first = 0U; first < order.size(); ++first) {
        const auto* object = resolved[first];
        if (object == nullptr || object->write_transaction_group == nullptr) continue;
        bool seen = false;
        for (std::size_t previous = 0U; previous < first; ++previous) {
            const auto* candidate = resolved[previous];
            if (candidate != nullptr &&
                candidate->write_transaction_group == object->write_transaction_group) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        std::array<std::size_t, MmsServiceSpanCodec::maximum_variables> positions{};
        std::array<std::size_t, MmsServiceSpanCodec::maximum_variables> members{};
        std::size_t count = 0U;
        for (std::size_t index = first; index < order.size(); ++index) {
            const auto* candidate = resolved[index];
            if (candidate != nullptr &&
                candidate->write_transaction_group == object->write_transaction_group) {
                positions[count] = index;
                members[count] = index;
                ++count;
            }
        }
        for (std::size_t i = 1U; i < count; ++i) {
            const auto member = members[i];
            const auto phase = write_phase(*resolved[member], values[member]);
            std::size_t j = i;
            while (j > 0U &&
                   write_phase(*resolved[members[j - 1U]], values[members[j - 1U]]) > phase) {
                members[j] = members[j - 1U];
                --j;
            }
            members[j] = member;
        }
        for (std::size_t i = 0U; i < count; ++i) order[positions[i]] = members[i];
    }
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
    std::array<std::size_t, MmsServiceSpanCodec::maximum_variables> execution_order{};
    build_write_execution_order(
        std::span<const MmsStaticObjectEntry* const>{resolved}.first(request.variable_count),
        std::span<const std::span<const std::uint8_t>>{values}.first(request.variable_count),
        std::span<std::size_t>{execution_order}.first(request.variable_count));
    for (std::size_t execution = 0U; execution < request.variable_count; ++execution) {
        const auto index = execution_order[execution];
        const auto* object = resolved[index];
        if (object == nullptr) {
            results[index] = MmsWriteAccessResultInput{false, policy.missing_object_failure_code};
            continue;
        }
        if (!object->writable()) {
            results[index] = MmsWriteAccessResultInput{false, policy.access_denied_failure_code};
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
    if (!model_valid_ || !policy_valid(policy_)) {
        return make_status(MmsStaticDispatchStatus::invalid_object_table, request);
    }
    if (request.kind != MmsWirePduKind::confirmed_request) {
        return make_status(MmsStaticDispatchStatus::malformed_request, request);
    }

    switch (request.service()) {
    case MmsWireConfirmedService::get_name_list:
        return dispatch_get_name_list(
            objects_, data_sets_, directory_, policy_, request, response);
    case MmsWireConfirmedService::get_variable_access_attributes:
        return dispatch_attributes(objects_, request, response, workspace);
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
