// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::asn1 {

enum class BerClass : std::uint8_t {
    universal = 0,
    application = 1,
    context_specific = 2,
    private_class = 3
};

class BerFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct BerTlv final {
    std::uint8_t encoded_tag{};
    BerClass tag_class{BerClass::universal};
    bool constructed{};
    std::int32_t tag_number{};
    std::vector<std::uint8_t> value;
};

class BerReader final {
public:
    static bool try_read_tlv(
        std::span<const std::uint8_t> source,
        std::size_t& offset,
        BerTlv& tlv) noexcept;

    [[nodiscard]] static std::vector<BerTlv> read_children(
        std::span<const std::uint8_t> source);

    [[nodiscard]] static std::string read_ascii_string(const BerTlv& tlv);
    [[nodiscard]] static std::optional<bool> read_boolean(const BerTlv& tlv) noexcept;
    [[nodiscard]] static std::optional<std::uint64_t> read_unsigned_integer(const BerTlv& tlv) noexcept;
    [[nodiscard]] static std::optional<std::int64_t> read_signed_integer(const BerTlv& tlv) noexcept;
    [[nodiscard]] static std::optional<std::uint16_t> read_uint16(const BerTlv& tlv) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> read_uint32(const BerTlv& tlv) noexcept;
    [[nodiscard]] static std::uint32_t read_uint32_big_endian(std::span<const std::uint8_t> source);
};

class BerWriter final {
public:
    [[nodiscard]] std::size_t length() const noexcept { return buffer_.size(); }

    void write_tlv(BerClass tag_class, bool constructed, std::int32_t tag_number,
                   std::span<const std::uint8_t> value);
    void write_tlv(std::uint8_t encoded_tag, std::span<const std::uint8_t> value);
    void write_raw(std::span<const std::uint8_t> bytes);

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return buffer_; }
    [[nodiscard]] std::vector<std::uint8_t> to_vector() const { return buffer_; }

    [[nodiscard]] static std::vector<std::uint8_t> encode_tlv(
        BerClass tag_class, bool constructed, std::int32_t tag_number,
        std::span<const std::uint8_t> value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_tlv(
        std::uint8_t encoded_tag, std::span<const std::uint8_t> value);
    [[nodiscard]] static std::uint8_t encode_identifier(
        BerClass tag_class, bool constructed, std::int32_t tag_number);
    [[nodiscard]] static std::vector<std::uint8_t> encode_ascii(const std::string& value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_boolean(bool value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_unsigned_integer(std::uint64_t value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_signed_integer(std::int64_t value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_single_precision_float(float value);
    [[nodiscard]] static std::vector<std::uint8_t> encode_utc_time(
        std::chrono::system_clock::time_point value, std::uint8_t quality = 0);

private:
    void write_identifier(BerClass tag_class, bool constructed, std::int32_t tag_number);
    void write_length(std::size_t length);
    void write_byte(std::uint8_t value);
    void write_bytes(std::span<const std::uint8_t> value);

    std::vector<std::uint8_t> buffer_;
};

} // namespace ar::iec61850::asn1
