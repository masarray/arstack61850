// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::iec61850::acse {

struct AcseExternalView final {
    std::span<const std::uint8_t> direct_reference{};
    std::uint32_t indirect_reference{};
    std::span<const std::uint8_t> single_asn1_type{};
};

struct AcseAarqView final {
    std::span<const std::uint8_t> application_context_name{};
    std::span<const std::uint8_t> called_ap_title{};
    std::optional<std::uint32_t> called_ae_qualifier{};
    std::span<const std::uint8_t> calling_ap_title{};
    std::optional<std::uint32_t> calling_ae_qualifier{};
    AcseExternalView user_information{};
};

struct AssociationRequestView final {
    osi::SessionSpduView session{};
    osi::PresentationCpView presentation{};
    AcseAarqView aarq{};
    std::uint32_t acse_presentation_context_id{};
    std::uint32_t mms_presentation_context_id{};
};

class AcseSpanCodec final {
public:
    static constexpr std::size_t maximum_acse_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_oid_bytes = 64U;

    [[nodiscard]] static std::span<const std::uint8_t>
        mms_application_context_name() noexcept;
    [[nodiscard]] static std::span<const std::uint8_t>
        default_mms_initiate_request() noexcept;
    [[nodiscard]] static std::span<const std::uint8_t>
        default_mms_initiate_response() noexcept;

    [[nodiscard]] static bool try_decode_aarq_view(
        std::span<const std::uint8_t> bytes,
        AcseAarqView& aarq) noexcept;

    [[nodiscard]] static bool try_decode_association_request_view(
        std::span<const std::uint8_t> bytes,
        AssociationRequestView& request) noexcept;

    [[nodiscard]] static constexpr std::size_t default_accept_aare_size() noexcept {
        return 76U;
    }

    [[nodiscard]] static wire::EncodeResult encode_default_accept_aare_into(
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static std::optional<std::size_t> accept_response_size(
        const AssociationRequestView& request) noexcept;

    [[nodiscard]] static wire::EncodeResult build_accept_response_into(
        const AssociationRequestView& request,
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::acse
