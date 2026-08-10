// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_object_table.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool visible_ascii(const std::string_view value) noexcept {
    if (value.empty() || value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

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

[[nodiscard]] bool valid_type_specification(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView type;
    return !encoded.empty() &&
        asn1::BerSpanReader::try_read_exact(encoded, type) &&
        type.tag_class == asn1::BerClass::context_specific;
}

} // namespace

bool MmsStaticObjectTable::valid() const noexcept {
    if (objects_.empty() || objects_.size() > maximum_objects) {
        return false;
    }
    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        const auto& object = objects_[index];
        if (!visible_ascii(object.domain) || !visible_ascii(object.item) ||
            !valid_type_specification(object.type_specification) ||
            object.read == nullptr) {
            return false;
        }
        for (std::size_t other = index + 1U; other < objects_.size(); ++other) {
            if (objects_[other].domain == object.domain &&
                objects_[other].item == object.item) {
                return false;
            }
        }
    }
    return true;
}

const MmsStaticObjectEntry* MmsStaticObjectTable::find(
    const MmsObjectNameView& name) const noexcept {
    if (name.kind != MmsObjectNameViewKind::domain_specific ||
        name.domain.empty() || name.item.empty()) {
        return nullptr;
    }
    for (const auto& object : objects_) {
        if (span_equals(name.domain, object.domain) &&
            span_equals(name.item, object.item)) {
            return &object;
        }
    }
    return nullptr;
}

bool MmsStaticObjectTable::try_resolve_read_request(
    const MmsReadRequestView& request,
    const std::span<const MmsStaticObjectEntry*> resolved,
    std::size_t& resolved_count) const noexcept {
    resolved_count = 0U;
    if (request.variable_count == 0U ||
        request.variable_count > resolved.size() ||
        request.variable_count > MmsServiceSpanCodec::maximum_variables) {
        return false;
    }
    for (std::size_t index = 0U; index < request.variable_count; ++index) {
        MmsObjectNameView name;
        if (!request.try_variable(index, name)) {
            resolved_count = 0U;
            return false;
        }
        resolved[index] = find(name);
        ++resolved_count;
    }
    return true;
}

} // namespace ar::iec61850::mms
