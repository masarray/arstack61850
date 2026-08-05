// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ByteVector = std::vector<std::uint8_t>;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

ByteVector from_hex(const std::string& text) {
    if ((text.size() % 2U) != 0U) {
        throw std::invalid_argument("Hex text must contain an even number of characters.");
    }
    ByteVector result;
    result.reserve(text.size() / 2U);
    for (std::size_t index = 0; index < text.size(); index += 2U) {
        result.push_back(static_cast<std::uint8_t>(
            std::stoul(text.substr(index, 2U), nullptr, 16)));
    }
    return result;
}

std::string to_hex(const ByteVector& bytes) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

void iec61850_utc_time_matches_csharp_vector() {
    using namespace ar::iec61850::mms;
    const auto timestamp = std::chrono::system_clock::time_point{
        std::chrono::seconds{1'781'233'445}} + std::chrono::milliseconds{250};
    const Iec61850UtcTime utc{timestamp, 0x0AU};
    const auto encoded = utc.to_bytes();
    CHECK(to_hex(encoded) == "6A2B77254000000A");
    CHECK(Iec61850UtcTime::from_bytes(encoded) == utc);

    bool rejected = false;
    try {
        static_cast<void>(Iec61850UtcTime::from_bytes(ByteVector{0x00U}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void mms_data_codec_matches_csharp_golden_vector() {
    using namespace ar::iec61850::mms;
    const auto timestamp = std::chrono::system_clock::time_point{
        std::chrono::seconds{1'781'233'445}} + std::chrono::milliseconds{250};
    const std::vector<MmsDataValue> values{
        MmsDataValue::boolean(true),
        MmsDataValue::integer(-3),
        MmsDataValue::unsigned_integer(42U),
        MmsDataValue::floating_point(12.5F),
        MmsDataValue::visible_string("OK"),
        MmsDataValue::binary_time(ByteVector{0x12U, 0x34U, 0x56U, 0x78U}),
        MmsDataValue::utc_time(Iec61850UtcTime{timestamp, 0x0AU}),
        MmsDataValue::structure({
            MmsDataValue::boolean(false),
            MmsDataValue::unsigned_integer(7U)})};

    const auto encoded = MmsDataCodec::encode_all(values);
    CHECK(to_hex(encoded) ==
          "8301018501FD86012A870508414800008A024F4B8C041234567891086A2B77254000000AA206830100860107");

    const auto decoded = MmsDataCodec::decode_all(encoded);
    CHECK(decoded.size() == values.size());
    CHECK(decoded[0].kind() == MmsDataKind::boolean);
    CHECK(std::get<bool>(decoded[0].value()));
    CHECK(std::get<std::int64_t>(decoded[1].value()) == -3);
    CHECK(std::get<std::uint64_t>(decoded[2].value()) == 42U);
    CHECK(std::get<float>(decoded[3].value()) == 12.5F);
    CHECK(std::get<std::string>(decoded[4].value()) == "OK");
    CHECK(decoded[5].raw_value() == ByteVector({0x12U, 0x34U, 0x56U, 0x78U}));
    CHECK(std::get<Iec61850UtcTime>(decoded[6].value()) ==
          Iec61850UtcTime(timestamp, 0x0AU));
    CHECK(decoded[7].kind() == MmsDataKind::structure);
    CHECK(decoded[7].children().size() == 2U);
    CHECK(!std::get<bool>(decoded[7].children()[0].value()));
    CHECK(std::get<std::uint64_t>(decoded[7].children()[1].value()) == 7U);
}

void mms_data_codec_preserves_bit_strings_and_unknown_tags() {
    using namespace ar::iec61850::mms;
    const auto bit_encoded = MmsDataCodec::encode(
        MmsDataValue::bit_string(3U, ByteVector{0xA8U}));
    CHECK(to_hex(bit_encoded) == "840203A8");
    const auto bit_decoded = MmsDataCodec::decode_all(bit_encoded);
    CHECK(bit_decoded.size() == 1U);
    CHECK(bit_decoded[0].raw_value() == ByteVector({0x03U, 0xA8U}));

    const auto unknown_encoded = MmsDataCodec::encode(
        MmsDataValue::unknown(45, ByteVector{0xDEU, 0xADU}));
    CHECK(to_hex(unknown_encoded) == "9F2D02DEAD");
    const auto unknown_decoded = MmsDataCodec::decode_all(unknown_encoded);
    CHECK(unknown_decoded.size() == 1U);
    CHECK(unknown_decoded[0].kind() == MmsDataKind::unknown);
    CHECK(unknown_decoded[0].unknown_tag_number().value() == 45);
    CHECK(unknown_decoded[0].raw_value() == ByteVector({0xDEU, 0xADU}));
}

void mms_data_codec_rejects_truncated_nested_tlv() {
    using namespace ar::iec61850::mms;
    bool rejected = false;
    try {
        static_cast<void>(MmsDataCodec::decode_all(from_hex("A2048301")));
    } catch (const ar::iec61850::asn1::BerFormatError&) {
        rejected = true;
    }
    CHECK(rejected);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"IEC 61850 UTC time", iec61850_utc_time_matches_csharp_vector},
        {"MMS data golden vector", mms_data_codec_matches_csharp_golden_vector},
        {"MMS bit string and unknown", mms_data_codec_preserves_bit_strings_and_unknown_tags},
        {"MMS malformed nested TLV", mms_data_codec_rejects_truncated_nested_tlv}};

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "Passed " << passed << "/" << tests.size() << " MMS tests.\n";
    return 0;
}
