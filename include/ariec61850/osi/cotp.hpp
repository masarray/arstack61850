// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::osi {

class CotpFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class CotpTpduKind {
    unknown,
    connection_request,
    connection_confirm,
    data,
    disconnect_request,
    error,
};

struct CotpParameter final {
    std::uint8_t code{};
    std::vector<std::uint8_t> value;

    friend bool operator==(const CotpParameter&, const CotpParameter&) = default;
};

struct CotpTpdu final {
    CotpTpduKind kind{CotpTpduKind::unknown};
    std::uint8_t code{};
    std::uint8_t length_indicator{};
    std::uint16_t destination_reference{};
    std::uint16_t source_reference{};
    std::uint8_t class_or_reason{};
    bool end_of_transmission{};
    std::uint8_t tpdu_number{};
    std::vector<CotpParameter> parameters;
    std::vector<std::uint8_t> user_data;

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> parameter(
        std::uint8_t parameter_code) const noexcept;

    friend bool operator==(const CotpTpdu&, const CotpTpdu&) = default;
};

class CotpFrameCodec final {
public:
    static constexpr std::uint8_t connection_request_code = 0xE0U;
    static constexpr std::uint8_t connection_confirm_code = 0xD0U;
    static constexpr std::uint8_t disconnect_request_code = 0x80U;
    static constexpr std::uint8_t data_code = 0xF0U;
    static constexpr std::uint8_t error_code = 0x70U;

    static constexpr std::uint8_t tpdu_size_parameter = 0xC0U;
    static constexpr std::uint8_t source_tsap_parameter = 0xC1U;
    static constexpr std::uint8_t destination_tsap_parameter = 0xC2U;

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes,
        CotpTpdu& tpdu,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static CotpTpdu decode(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode_default_connection_request();

    [[nodiscard]] static std::vector<std::uint8_t> encode_connection_request(
        std::uint16_t source_reference,
        std::span<const CotpParameter> parameters,
        std::uint16_t destination_reference = 0U,
        std::uint8_t transport_class = 0U);

    [[nodiscard]] static std::vector<std::uint8_t> encode_connection_confirm(
        std::uint16_t destination_reference,
        std::uint16_t source_reference,
        std::uint8_t maximum_tpdu_size_code = 0x0AU);

    [[nodiscard]] static std::vector<std::uint8_t> encode_connection_confirm(
        const CotpTpdu& connection_request,
        std::uint16_t source_reference,
        std::uint8_t maximum_tpdu_size_code = 0x0AU);

    [[nodiscard]] static std::vector<std::uint8_t> encode_data(
        std::span<const std::uint8_t> user_data,
        bool end_of_transmission = true,
        std::uint8_t tpdu_number = 0U);

    [[nodiscard]] static std::vector<std::vector<std::uint8_t>> encode_data_segments(
        std::span<const std::uint8_t> user_data,
        std::uint8_t tpdu_size_code);

    [[nodiscard]] static std::vector<std::uint8_t> encode_disconnect_request(
        std::uint16_t destination_reference,
        std::uint16_t source_reference,
        std::uint8_t reason = 0U,
        std::span<const CotpParameter> parameters = {});

    [[nodiscard]] static std::size_t tpdu_size_bytes(std::uint8_t tpdu_size_code);
    [[nodiscard]] static CotpTpduKind kind_from_code(std::uint8_t code) noexcept;
};

class CotpDataReassembler final {
public:
    static constexpr std::size_t default_maximum_bytes = 64U * 1024U * 1024U;
    static constexpr std::size_t default_maximum_fragments = 1'048'576U;
    static constexpr std::size_t default_maximum_empty_nonfinal_fragments = 1'024U;

    explicit CotpDataReassembler(
        std::size_t maximum_bytes = default_maximum_bytes,
        std::size_t maximum_fragments = default_maximum_fragments,
        std::size_t maximum_empty_nonfinal_fragments =
            default_maximum_empty_nonfinal_fragments);

    void append(const CotpTpdu& tpdu);
    void append_encoded(std::span<const std::uint8_t> encoded_tpdu);
    [[nodiscard]] std::vector<std::uint8_t> complete() const;
    void reset() noexcept;

    [[nodiscard]] bool is_complete() const noexcept { return complete_; }
    [[nodiscard]] std::size_t fragment_count() const noexcept { return fragment_count_; }
    [[nodiscard]] std::size_t reassembled_bytes() const noexcept { return buffer_.size(); }

private:
    std::size_t maximum_bytes_{};
    std::size_t maximum_fragments_{};
    std::size_t maximum_empty_nonfinal_fragments_{};
    std::size_t fragment_count_{};
    std::size_t empty_nonfinal_fragments_{};
    bool complete_{};
    std::vector<std::uint8_t> buffer_;
};

} // namespace ar::iec61850::osi
