// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association_compat.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::acse {
namespace {

[[nodiscard]] bool read_positive_integer(
    const asn1::BerTlvView& tlv,
    std::uint32_t& value) noexcept {
    value = 0U;
    const auto decoded = asn1::BerSpanReader::read_uint32(tlv);
    if (!decoded || *decoded == 0U) {
        return false;
    }
    value = *decoded;
    return true;
}

[[nodiscard]] bool decode_mode_selector_compat(
    const asn1::BerTlvView& item,
    std::uint32_t& mode) noexcept {
    if (item.tag_class != asn1::BerClass::context_specific ||
        item.tag_number != 0 || !item.constructed) {
        return false;
    }
    asn1::BerTlvView inner;
    return asn1::BerSpanReader::try_read_exact(item.value, inner) &&
        inner.tag_class == asn1::BerClass::context_specific &&
        inner.tag_number == 0 && !inner.constructed &&
        read_positive_integer(inner, mode);
}

[[nodiscard]] bool count_contexts(
    const std::span<const std::uint8_t> list,
    std::size_t& count) noexcept {
    count = 0U;
    std::size_t offset = 0U;
    while (offset < list.size()) {
        if (count >= osi::PresentationSpanCodec::maximum_contexts) {
            count = 0U;
            return false;
        }
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(list, offset, definition) ||
            definition.tag_class != asn1::BerClass::universal ||
            definition.tag_number != 16 || !definition.constructed) {
            count = 0U;
            return false;
        }
        ++count;
    }
    return count > 0U;
}

[[nodiscard]] bool decode_pdv_compat(
    const asn1::BerTlvView& outer,
    osi::PresentationPdvView& pdv) noexcept {
    pdv = {};
    if (outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 1 || !outer.constructed) {
        return false;
    }

    asn1::BerTlvView sequence;
    if (!asn1::BerSpanReader::try_read_exact(outer.value, sequence) ||
        sequence.tag_class != asn1::BerClass::universal ||
        sequence.tag_number != 16 || !sequence.constructed) {
        return false;
    }

    bool saw_context = false;
    bool saw_payload = false;
    bool saw_transfer_syntax = false;
    std::size_t offset = 0U;
    while (offset < sequence.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(sequence.value, offset, field)) {
            pdv = {};
            return false;
        }
        if (field.tag_class == asn1::BerClass::universal &&
            field.tag_number == 2 && !field.constructed) {
            if (saw_context || !read_positive_integer(field, pdv.context_id)) {
                pdv = {};
                return false;
            }
            saw_context = true;
        } else if (field.tag_class == asn1::BerClass::universal &&
                   field.tag_number == 6 && !field.constructed) {
            // transfer-syntax-name is optional in PDV-list. The legacy server
            // ignored it during association negotiation.
            if (saw_transfer_syntax || field.value.empty() ||
                field.value.size() > osi::PresentationSpanCodec::maximum_oid_bytes) {
                pdv = {};
                return false;
            }
            saw_transfer_syntax = true;
        } else if (field.tag_class == asn1::BerClass::context_specific &&
                   field.tag_number == 0 && field.constructed) {
            if (saw_payload || field.value.empty()) {
                pdv = {};
                return false;
            }
            pdv.single_asn1_type = field.value;
            saw_payload = true;
        } else {
            // Other PDV-list presentation-data-value alternatives are not MMS
            // single-ASN1-type and therefore cannot establish this profile.
            pdv = {};
            return false;
        }
    }
    return saw_context && saw_payload;
}

[[nodiscard]] bool decode_cp_compat(
    const std::span<const std::uint8_t> bytes,
    osi::PresentationCpView& cp) noexcept {
    cp = {};
    cp.mode_selector = 1U;
    if (bytes.empty() || bytes.size() > osi::PresentationSpanCodec::maximum_ppdu_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::universal ||
        outer.tag_number != 17 || !outer.constructed) {
        return false;
    }

    bool saw_mode = false;
    bool saw_normal = false;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, item)) {
            cp = {};
            return false;
        }

        if (item.tag_class == asn1::BerClass::context_specific &&
            item.tag_number == 0 && item.constructed) {
            if (saw_mode || !decode_mode_selector_compat(item, cp.mode_selector)) {
                cp = {};
                return false;
            }
            saw_mode = true;
            continue;
        }
        if (item.tag_class != asn1::BerClass::context_specific ||
            item.tag_number != 2 || !item.constructed) {
            // The old request inspector ignored Presentation decorations outside
            // normal-mode-parameters. BER framing has already been validated.
            continue;
        }
        if (saw_normal) {
            cp = {};
            return false;
        }

        bool saw_calling = false;
        bool saw_called = false;
        bool saw_contexts = false;
        bool saw_user_data = false;
        std::size_t normal_offset = 0U;
        while (normal_offset < item.value.size()) {
            asn1::BerTlvView normal;
            if (!asn1::BerSpanReader::try_read_tlv(item.value, normal_offset, normal)) {
                cp = {};
                return false;
            }

            if (normal.tag_class == asn1::BerClass::context_specific &&
                normal.tag_number == 1 && !normal.constructed) {
                if (saw_calling ||
                    normal.value.size() > osi::PresentationSpanCodec::maximum_selector_bytes) {
                    cp = {};
                    return false;
                }
                cp.calling_selector = normal.value;
                saw_calling = true;
            } else if (normal.tag_class == asn1::BerClass::context_specific &&
                       normal.tag_number == 2 && !normal.constructed) {
                if (saw_called ||
                    normal.value.size() > osi::PresentationSpanCodec::maximum_selector_bytes) {
                    cp = {};
                    return false;
                }
                cp.called_selector = normal.value;
                saw_called = true;
            } else if (normal.tag_class == asn1::BerClass::context_specific &&
                       normal.tag_number == 4 && normal.constructed) {
                if (saw_contexts || !count_contexts(normal.value, cp.context_count)) {
                    cp = {};
                    return false;
                }
                cp.context_definition_list = normal.value;
                saw_contexts = true;
            } else if (normal.tag_class == asn1::BerClass::application &&
                       normal.tag_number == 1 && normal.constructed) {
                if (saw_user_data || !decode_pdv_compat(normal, cp.user_data)) {
                    cp = {};
                    return false;
                }
                saw_user_data = true;
            } else {
                // Optional normal-mode Presentation fields are not required to
                // negotiate the ACSE/MMS contexts. Preserve the legacy server's
                // tolerant engineering-client behavior.
                continue;
            }
        }

        if (!saw_contexts || !saw_user_data) {
            cp = {};
            return false;
        }
        saw_normal = true;
    }

    return saw_normal && cp.mode_selector != 0U;
}

[[nodiscard]] bool decode_external_compat(
    const asn1::BerTlvView& user_information,
    AcseExternalView& external) noexcept {
    external = {};
    if (!user_information.constructed) {
        return false;
    }

    std::size_t ui_offset = 0U;
    asn1::BerTlvView outer;
    bool saw_external = false;
    while (ui_offset < user_information.value.size()) {
        if (!asn1::BerSpanReader::try_read_tlv(
                user_information.value, ui_offset, outer)) {
            return false;
        }
        if (outer.tag_class == asn1::BerClass::universal &&
            outer.tag_number == 8 && outer.constructed) {
            if (saw_external) {
                return false;
            }
            saw_external = true;
        } else {
            continue;
        }

        std::size_t offset = 0U;
        while (offset < outer.value.size()) {
            asn1::BerTlvView field;
            if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, field)) {
                external = {};
                return false;
            }
            if (field.tag_class == asn1::BerClass::universal &&
                field.tag_number == 6 && !field.constructed &&
                external.direct_reference.empty()) {
                if (field.value.empty() || field.value.size() > AcseSpanCodec::maximum_oid_bytes) {
                    external = {};
                    return false;
                }
                external.direct_reference = field.value;
            } else if (field.tag_class == asn1::BerClass::universal &&
                       field.tag_number == 2 && !field.constructed &&
                       external.indirect_reference == 0U) {
                const auto value = asn1::BerSpanReader::read_uint32(field);
                if (!value) {
                    external = {};
                    return false;
                }
                external.indirect_reference = *value;
            } else if (field.tag_class == asn1::BerClass::context_specific &&
                       field.tag_number == 0 && field.constructed) {
                if (!external.single_asn1_type.empty() || field.value.empty()) {
                    external = {};
                    return false;
                }
                external.single_asn1_type = field.value;
            }
        }
    }
    return saw_external && !external.single_asn1_type.empty();
}

[[nodiscard]] bool decode_aarq_compat(
    const std::span<const std::uint8_t> bytes,
    AcseAarqView& aarq) noexcept {
    aarq = {};
    if (bytes.empty() || bytes.size() > AcseSpanCodec::maximum_acse_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 0 || !outer.constructed) {
        return false;
    }

    bool saw_user_information = false;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, field)) {
            aarq = {};
            return false;
        }
        if (field.tag_class != asn1::BerClass::context_specific) {
            continue;
        }
        if (field.tag_number == 30 && field.constructed) {
            if (saw_user_information ||
                !decode_external_compat(field, aarq.user_information)) {
                aarq = {};
                return false;
            }
            saw_user_information = true;
        }
    }

    if (!saw_user_information) {
        aarq = {};
        return false;
    }

    asn1::BerTlvView initiate;
    return asn1::BerSpanReader::try_read_exact(
               aarq.user_information.single_asn1_type, initiate) &&
        initiate.tag_class == asn1::BerClass::context_specific &&
        initiate.tag_number == 8 && initiate.constructed;
}

} // namespace

bool AcseAssociationCompat::try_decode_request_view(
    const std::span<const std::uint8_t> bytes,
    AssociationRequestView& request) noexcept {
    request = {};

    // Prefer the strict codec. Compatibility is a bounded fallback for live
    // engineering clients, not a replacement for the protocol validator.
    if (AcseSpanCodec::try_decode_association_request_view(bytes, request)) {
        return true;
    }

    if (!osi::SessionSpanCodec::try_decode_view(bytes, request.session) ||
        request.session.kind != osi::SessionWireKind::connect ||
        !decode_cp_compat(request.session.user_data, request.presentation) ||
        !decode_aarq_compat(
            request.presentation.user_data.single_asn1_type, request.aarq)) {
        request = {};
        return false;
    }

    if (!request.presentation.try_context_id_for_abstract_syntax(
            osi::PresentationSpanCodec::acse_abstract_syntax_name(),
            request.acse_presentation_context_id) ||
        !request.presentation.try_context_id_for_abstract_syntax(
            osi::PresentationSpanCodec::mms_abstract_syntax_name(),
            request.mms_presentation_context_id) ||
        request.acse_presentation_context_id == 0U ||
        request.mms_presentation_context_id == 0U ||
        request.presentation.user_data.context_id != request.acse_presentation_context_id) {
        request = {};
        return false;
    }

    return true;
}

} // namespace ar::iec61850::acse
