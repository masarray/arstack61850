// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/mms/utc_time.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsDataKind : std::uint8_t {
    array,
    structure,
    boolean,
    bit_string,
    integer,
    unsigned_integer,
    floating_point,
    octet_string,
    visible_string,
    binary_time,
    bcd,
    boolean_array,
    object_id,
    mms_string,
    utc_time,
    unknown
};

class MmsDataValue final {
public:
    using ScalarValue = std::variant<
        std::monostate,
        bool,
        std::int64_t,
        std::uint64_t,
        float,
        double,
        std::string,
        Iec61850UtcTime>;

    [[nodiscard]] static MmsDataValue array(std::vector<MmsDataValue> values);
    [[nodiscard]] static MmsDataValue structure(std::vector<MmsDataValue> values);
    [[nodiscard]] static MmsDataValue boolean(bool value);
    [[nodiscard]] static MmsDataValue bit_string(
        std::uint8_t unused_bits, std::span<const std::uint8_t> data);
    [[nodiscard]] static MmsDataValue integer(std::int64_t value);
    [[nodiscard]] static MmsDataValue unsigned_integer(std::uint64_t value);
    [[nodiscard]] static MmsDataValue floating_point(float value);
    [[nodiscard]] static MmsDataValue floating_point(double value);
    [[nodiscard]] static MmsDataValue octet_string(std::span<const std::uint8_t> value);
    [[nodiscard]] static MmsDataValue visible_string(std::string value);
    [[nodiscard]] static MmsDataValue mms_string(std::string value);
    [[nodiscard]] static MmsDataValue utc_time(Iec61850UtcTime value);
    [[nodiscard]] static MmsDataValue binary_time(std::span<const std::uint8_t> value);
    [[nodiscard]] static MmsDataValue unknown(
        std::int32_t tag_number, std::span<const std::uint8_t> raw_value);

    [[nodiscard]] MmsDataKind kind() const noexcept { return kind_; }
    [[nodiscard]] const ScalarValue& value() const noexcept { return value_; }
    [[nodiscard]] const std::vector<MmsDataValue>& children() const noexcept { return children_; }
    [[nodiscard]] const std::vector<std::uint8_t>& raw_value() const noexcept { return raw_value_; }
    [[nodiscard]] std::optional<std::int32_t> unknown_tag_number() const noexcept {
        return unknown_tag_number_;
    }

private:
    explicit MmsDataValue(
        MmsDataKind kind,
        ScalarValue value = {},
        std::vector<MmsDataValue> children = {},
        std::vector<std::uint8_t> raw_value = {},
        std::optional<std::int32_t> unknown_tag_number = std::nullopt);

    MmsDataKind kind_;
    ScalarValue value_;
    std::vector<MmsDataValue> children_;
    std::vector<std::uint8_t> raw_value_;
    std::optional<std::int32_t> unknown_tag_number_;
};

} // namespace ar::iec61850::mms
