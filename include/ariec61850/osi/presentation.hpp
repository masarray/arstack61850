// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::osi {

class PresentationFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PresentationContextDefinition final {
    std::uint32_t id{};
    std::vector<std::uint8_t> abstract_syntax_name;
    std::vector<std::vector<std::uint8_t>> transfer_syntax_names;

    friend bool operator==(const PresentationContextDefinition&,
                           const PresentationContextDefinition&) = default;
};

enum class PresentationContextResultCode : std::uint8_t {
    acceptance = 0U,
    user_rejection = 1U,
    provider_rejection = 2U,
};

struct PresentationContextResult final {
    PresentationContextResultCode result{PresentationContextResultCode::acceptance};
    std::vector<std::uint8_t> transfer_syntax_name;

    friend bool operator==(const PresentationContextResult&,
                           const PresentationContextResult&) = default;
};

struct PresentationPdv final {
    std::uint32_t context_id{};
    std::vector<std::uint8_t> single_asn1_type;

    friend bool operator==(const PresentationPdv&, const PresentationPdv&) = default;
};

struct PresentationCp final {
    std::uint32_t mode_selector{1U};
    std::vector<std::uint8_t> calling_selector;
    std::vector<std::uint8_t> called_selector;
    std::vector<PresentationContextDefinition> contexts;
    PresentationPdv user_data;

    [[nodiscard]] std::uint32_t context_id_for_abstract_syntax(
        std::span<const std::uint8_t> object_identifier_value) const noexcept;

    friend bool operator==(const PresentationCp&, const PresentationCp&) = default;
};

struct PresentationCpa final {
    std::uint32_t mode_selector{1U};
    std::vector<PresentationContextResult> context_results;
    PresentationPdv user_data;

    friend bool operator==(const PresentationCpa&, const PresentationCpa&) = default;
};

class PresentationCodec final {
public:
    static constexpr std::size_t maximum_ppdu_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_contexts = 64U;
    static constexpr std::size_t maximum_oid_bytes = 64U;
    static constexpr std::size_t maximum_selector_bytes = 64U;

    [[nodiscard]] static const std::vector<std::uint8_t>& acse_abstract_syntax_name();
    [[nodiscard]] static const std::vector<std::uint8_t>& mms_abstract_syntax_name();
    [[nodiscard]] static const std::vector<std::uint8_t>& ber_transfer_syntax_name();

    [[nodiscard]] static std::vector<PresentationContextDefinition> default_contexts();

    [[nodiscard]] static std::vector<std::uint8_t> encode_fully_encoded_data(
        std::uint32_t context_id,
        std::span<const std::uint8_t> single_asn1_type);

    [[nodiscard]] static bool try_decode_fully_encoded_data(
        std::span<const std::uint8_t> bytes,
        PresentationPdv& pdv,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static PresentationPdv decode_fully_encoded_data(
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_cp(
        std::span<const PresentationContextDefinition> contexts,
        std::uint32_t acse_context_id,
        std::span<const std::uint8_t> acse_aarq,
        std::span<const std::uint8_t> calling_selector = {},
        std::span<const std::uint8_t> called_selector = {},
        std::uint32_t mode_selector = 1U);

    [[nodiscard]] static bool try_decode_cp(
        std::span<const std::uint8_t> bytes,
        PresentationCp& cp,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static PresentationCp decode_cp(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_cpa(
        std::span<const PresentationContextResult> results,
        std::uint32_t acse_context_id,
        std::span<const std::uint8_t> acse_aare,
        std::uint32_t mode_selector = 1U);

    [[nodiscard]] static std::vector<std::uint8_t> encode_cpa_accepting(
        const PresentationCp& request,
        std::span<const std::uint8_t> acse_aare);

    [[nodiscard]] static bool try_decode_cpa(
        std::span<const std::uint8_t> bytes,
        PresentationCpa& cpa,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static PresentationCpa decode_cpa(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_p_data(
        std::span<const std::uint8_t> abstract_syntax_payload,
        std::uint32_t presentation_context_id = 3U,
        bool include_give_tokens_prefix = true);

    [[nodiscard]] static bool try_decode_p_data(
        std::span<const std::uint8_t> bytes,
        PresentationPdv& pdv,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static PresentationPdv decode_p_data(
        std::span<const std::uint8_t> bytes);
};

} // namespace ar::iec61850::osi
