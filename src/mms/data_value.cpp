// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_value.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

MmsDataValue::MmsDataValue(
    const MmsDataKind kind,
    ScalarValue value,
    std::vector<MmsDataValue> children,
    std::vector<std::uint8_t> raw_value,
    const std::optional<std::int32_t> unknown_tag_number)
    : kind_{kind},
      value_{std::move(value)},
      children_{std::move(children)},
      raw_value_{std::move(raw_value)},
      unknown_tag_number_{unknown_tag_number} {}

MmsDataValue MmsDataValue::array(std::vector<MmsDataValue> values) {
    return MmsDataValue{MmsDataKind::array, {}, std::move(values)};
}

MmsDataValue MmsDataValue::structure(std::vector<MmsDataValue> values) {
    return MmsDataValue{MmsDataKind::structure, {}, std::move(values)};
}

MmsDataValue MmsDataValue::boolean(const bool value) {
    return MmsDataValue{MmsDataKind::boolean, value};
}

MmsDataValue MmsDataValue::bit_string(
    const std::uint8_t unused_bits, const std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> raw(data.size() + 1U);
    raw[0] = unused_bits;
    std::copy(data.begin(), data.end(), raw.begin() + 1);
    return MmsDataValue{MmsDataKind::bit_string, {}, {}, std::move(raw)};
}

MmsDataValue MmsDataValue::integer(const std::int64_t value) {
    return MmsDataValue{MmsDataKind::integer, value};
}

MmsDataValue MmsDataValue::unsigned_integer(const std::uint64_t value) {
    return MmsDataValue{MmsDataKind::unsigned_integer, value};
}

MmsDataValue MmsDataValue::floating_point(const float value) {
    return MmsDataValue{MmsDataKind::floating_point, value};
}

MmsDataValue MmsDataValue::floating_point(const double value) {
    return MmsDataValue{MmsDataKind::floating_point, value};
}

MmsDataValue MmsDataValue::octet_string(const std::span<const std::uint8_t> value) {
    return MmsDataValue{
        MmsDataKind::octet_string, {}, {}, {value.begin(), value.end()}};
}

MmsDataValue MmsDataValue::visible_string(std::string value) {
    return MmsDataValue{MmsDataKind::visible_string, std::move(value)};
}

MmsDataValue MmsDataValue::mms_string(std::string value) {
    return MmsDataValue{MmsDataKind::mms_string, std::move(value)};
}

MmsDataValue MmsDataValue::utc_time(Iec61850UtcTime value) {
    return MmsDataValue{MmsDataKind::utc_time, value};
}

MmsDataValue MmsDataValue::binary_time(const std::span<const std::uint8_t> value) {
    return MmsDataValue{
        MmsDataKind::binary_time, {}, {}, {value.begin(), value.end()}};
}

MmsDataValue MmsDataValue::unknown(
    const std::int32_t tag_number, const std::span<const std::uint8_t> raw_value) {
    return MmsDataValue{
        MmsDataKind::unknown,
        {},
        {},
        {raw_value.begin(), raw_value.end()},
        tag_number};
}

} // namespace ar::iec61850::mms
