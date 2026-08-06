// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/osi/presentation.hpp"
#include "ariec61850/osi/session.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::acse {

class AcseFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AcseExternal final {
    std::vector<std::uint8_t> direct_reference;
    std::uint32_t indirect_reference{};
    std::vector<std::uint8_t> single_asn1_type;

    friend bool operator==(const AcseExternal&, const AcseExternal&) = default;
};

struct AcseAarq final {
    std::vector<std::uint8_t> application_context_name;
    std::vector<std::uint8_t> called_ap_title;
    std::optional<std::uint32_t> called_ae_qualifier;
    std::vector<std::uint8_t> calling_ap_title;
    std::optional<std::uint32_t> calling_ae_qualifier;
    std::optional<AcseExternal> user_information;

    friend bool operator==(const AcseAarq&, const AcseAarq&) = default;
};

struct AcseAare final {
    std::vector<std::uint8_t> application_context_name;
    std::uint32_t result{};
    std::uint32_t result_source_diagnostic{};
    std::optional<AcseExternal> user_information;

    [[nodiscard]] bool accepted() const noexcept { return result == 0U; }

    friend bool operator==(const AcseAare&, const AcseAare&) = default;
};

struct AssociationRequestEnvelope final {
    osi::SessionSpdu session;
    osi::PresentationCp presentation;
    AcseAarq aarq;
    std::uint32_t acse_presentation_context_id{};
    std::uint32_t mms_presentation_context_id{};

    friend bool operator==(const AssociationRequestEnvelope&,
                           const AssociationRequestEnvelope&) = default;
};

struct AssociationResponseEnvelope final {
    osi::SessionSpdu session;
    osi::PresentationCpa presentation;
    AcseAare aare;

    friend bool operator==(const AssociationResponseEnvelope&,
                           const AssociationResponseEnvelope&) = default;
};

struct AssociationResponseProfile final {
    std::vector<std::uint8_t> payload;
    std::uint32_t mms_presentation_context_id{3U};
    std::uint32_t maximum_mms_pdu_size{65'000U};
    std::uint32_t maximum_outstanding_calling{10U};
    std::uint32_t maximum_outstanding_called{10U};
    std::uint32_t data_structure_nesting_level{5U};
};

class AcseAssociationCodec final {
public:
    static constexpr std::size_t maximum_acse_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_oid_bytes = 64U;

    [[nodiscard]] static const std::vector<std::uint8_t>&
        mms_application_context_name();
    [[nodiscard]] static const std::vector<std::uint8_t>&
        balanced_called_ap_title();
    [[nodiscard]] static const std::vector<std::uint8_t>&
        balanced_calling_ap_title();

    [[nodiscard]] static std::vector<std::uint8_t> default_mms_initiate_request();
    [[nodiscard]] static std::vector<std::uint8_t> default_mms_initiate_response();

    [[nodiscard]] static AcseAarq default_balanced_aarq();
    [[nodiscard]] static AcseAare default_accept_aare();

    [[nodiscard]] static std::vector<std::uint8_t> encode_aarq(const AcseAarq& aarq);
    [[nodiscard]] static bool try_decode_aarq(
        std::span<const std::uint8_t> bytes,
        AcseAarq& aarq,
        std::string* error = nullptr) noexcept;
    [[nodiscard]] static AcseAarq decode_aarq(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_aare(const AcseAare& aare);
    [[nodiscard]] static bool try_decode_aare(
        std::span<const std::uint8_t> bytes,
        AcseAare& aare,
        std::string* error = nullptr) noexcept;
    [[nodiscard]] static AcseAare decode_aare(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> build_default_association_request();

    [[nodiscard]] static bool try_decode_association_request(
        std::span<const std::uint8_t> bytes,
        AssociationRequestEnvelope& request,
        std::string* error = nullptr) noexcept;
    [[nodiscard]] static AssociationRequestEnvelope decode_association_request(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static AssociationResponseProfile build_accept_response(
        const AssociationRequestEnvelope& request,
        const AcseAare& aare = default_accept_aare());

    [[nodiscard]] static AssociationResponseProfile build_accept_response(
        std::span<const std::uint8_t> encoded_request,
        const AcseAare& aare = default_accept_aare());

    [[nodiscard]] static bool try_decode_association_response(
        std::span<const std::uint8_t> bytes,
        AssociationResponseEnvelope& response,
        std::string* error = nullptr) noexcept;
    [[nodiscard]] static AssociationResponseEnvelope decode_association_response(
        std::span<const std::uint8_t> bytes);
};

} // namespace ar::iec61850::acse
