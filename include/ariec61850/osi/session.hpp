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

class SessionFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class SessionSpduKind {
    unknown,
    connect,
    accept,
    reject,
    refuse,
    abort,
    data_transfer,
};

struct SessionParameter final {
    std::uint8_t code{};
    std::vector<std::uint8_t> value;

    friend bool operator==(const SessionParameter&, const SessionParameter&) = default;
};

struct SessionSpdu final {
    SessionSpduKind kind{SessionSpduKind::unknown};
    std::uint8_t code{};
    std::uint8_t length_indicator{};
    std::vector<SessionParameter> parameters;
    std::vector<std::uint8_t> user_data;

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> parameter(
        std::uint8_t parameter_code) const noexcept;

    friend bool operator==(const SessionSpdu&, const SessionSpdu&) = default;
};

struct SessionDataTransfer final {
    bool has_give_tokens_prefix{};
    std::vector<std::uint8_t> presentation_payload;

    friend bool operator==(const SessionDataTransfer&, const SessionDataTransfer&) = default;
};

class SessionCodec final {
public:
    static constexpr std::uint8_t connect_code = 0x0DU;
    static constexpr std::uint8_t accept_code = 0x0EU;
    static constexpr std::uint8_t reject_code = 0x0AU;
    static constexpr std::uint8_t refuse_code = 0x0CU;
    static constexpr std::uint8_t abort_code = 0x19U;
    static constexpr std::uint8_t data_transfer_code = 0x01U;
    static constexpr std::uint8_t user_data_parameter = 0xC1U;
    static constexpr std::size_t maximum_spdu_bytes = 257U;
    static constexpr std::size_t maximum_parameter_count = 64U;

    [[nodiscard]] static SessionSpduKind kind_from_code(std::uint8_t code) noexcept;

    [[nodiscard]] static bool try_decode_prefix(
        std::span<const std::uint8_t> bytes,
        SessionSpdu& spdu,
        std::size_t& consumed,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes,
        SessionSpdu& spdu,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static SessionSpdu decode(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static std::vector<std::uint8_t> encode(
        std::uint8_t code,
        std::span<const SessionParameter> parameters,
        std::span<const std::uint8_t> user_data = {});

    [[nodiscard]] static std::vector<SessionParameter> default_parameters();

    [[nodiscard]] static std::vector<std::uint8_t> encode_connect(
        std::span<const std::uint8_t> presentation_payload,
        std::span<const SessionParameter> parameters = {});

    [[nodiscard]] static std::vector<std::uint8_t> encode_accept(
        std::span<const std::uint8_t> presentation_payload,
        std::span<const SessionParameter> parameters = {});

    [[nodiscard]] static std::vector<std::uint8_t> encode_accept_mirroring(
        const SessionSpdu& connect,
        std::span<const std::uint8_t> presentation_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_data_transfer(
        std::span<const std::uint8_t> presentation_payload,
        bool include_give_tokens_prefix = true);

    [[nodiscard]] static bool try_decode_data_transfer(
        std::span<const std::uint8_t> bytes,
        SessionDataTransfer& transfer,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static SessionDataTransfer decode_data_transfer(
        std::span<const std::uint8_t> bytes);
};

} // namespace ar::iec61850::osi
