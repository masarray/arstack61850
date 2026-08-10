// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::iec61850::osi {

struct PresentationPdvView final {
    std::uint32_t context_id{};
    std::span<const std::uint8_t> single_asn1_type{};
};

struct PresentationCpView final {
    std::uint32_t mode_selector{1U};
    std::span<const std::uint8_t> calling_selector{};
    std::span<const std::uint8_t> called_selector{};
    std::span<const std::uint8_t> context_definition_list{};
    std::size_t context_count{};
    PresentationPdvView user_data{};

    [[nodiscard]] bool try_context_id_for_abstract_syntax(
        std::span<const std::uint8_t> object_identifier_value,
        std::uint32_t& context_id) const noexcept;
};

class PresentationSpanCodec final {
public:
    static constexpr std::size_t maximum_ppdu_bytes = 1U * 1024U * 1024U;
    static constexpr std::size_t maximum_contexts = 64U;
    static constexpr std::size_t maximum_oid_bytes = 64U;
    static constexpr std::size_t maximum_selector_bytes = 64U;

    [[nodiscard]] static std::span<const std::uint8_t>
        acse_abstract_syntax_name() noexcept;
    [[nodiscard]] static std::span<const std::uint8_t>
        mms_abstract_syntax_name() noexcept;
    [[nodiscard]] static std::span<const std::uint8_t>
        ber_transfer_syntax_name() noexcept;

    [[nodiscard]] static std::optional<std::size_t> fully_encoded_data_size(
        std::uint32_t context_id,
        std::size_t single_asn1_type_bytes) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_fully_encoded_data_into(
        std::uint32_t context_id,
        std::span<const std::uint8_t> single_asn1_type,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static bool try_decode_fully_encoded_data_view(
        std::span<const std::uint8_t> bytes,
        PresentationPdvView& pdv) noexcept;

    [[nodiscard]] static bool try_decode_cp_view(
        std::span<const std::uint8_t> bytes,
        PresentationCpView& cp) noexcept;

    [[nodiscard]] static std::optional<std::size_t> cpa_accepting_size(
        const PresentationCpView& request,
        std::size_t acse_aare_bytes) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_cpa_accepting_into(
        const PresentationCpView& request,
        std::span<const std::uint8_t> acse_aare,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_p_data_into(
        std::span<const std::uint8_t> abstract_syntax_payload,
        std::span<std::uint8_t> destination,
        std::uint32_t presentation_context_id = 3U,
        bool include_give_tokens_prefix = true) noexcept;

    [[nodiscard]] static bool try_decode_p_data_view(
        std::span<const std::uint8_t> bytes,
        PresentationPdvView& pdv) noexcept;
};

} // namespace ar::iec61850::osi
